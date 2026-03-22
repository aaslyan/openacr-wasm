// Copyright (C) 2026 AlgoRND
//
// License: GPL
// Target: acr_tui (exe) -- Terminal UI browser for OpenACR schemas
// Exceptions: yes
// Source: cpp/acr_tui/acr_tui.cpp
//
// Generic data-driven TUI browser
// Loads all ssim data as tuples, navigates via DataPath, renders from ui.* schema

#include "include/algo.h"
#include "include/acr_tui.h"
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <dirent.h>

// ============================================================================
// Generic record store — holds all loaded ssim data as tuples
// ============================================================================

struct GRec {
    algo::cstring ctype_tag;   // e.g. "dmmeta.ns", "dmmeta.ctype"
    algo::cstring pkey;        // primary key value
    algo::Tuple   tuple;       // parsed attributes
    GRec* next;                // linked list per ctype
};

static const int MAX_CTYPES = 256;
static const int MAX_RECS = 100000;
static GRec g_recs[MAX_RECS];
static int g_nrecs = 0;

// Index: ctype_tag -> linked list of records
struct CTypeIndex {
    algo::cstring tag;
    GRec* head;
    GRec* tail;
    int count;
};
static CTypeIndex g_ctypes[MAX_CTYPES];
static int g_nctypes = 0;

static CTypeIndex* FindCType(algo::strptr tag) {
    for (int i = 0; i < g_nctypes; i++) {
        if (algo::strptr(g_ctypes[i].tag) == tag) return &g_ctypes[i];
    }
    return NULL;
}

static CTypeIndex* GetOrCreateCType(algo::strptr tag) {
    CTypeIndex* ct = FindCType(tag);
    if (!ct && g_nctypes < MAX_CTYPES) {
        ct = &g_ctypes[g_nctypes++];
        ct->tag = tag;
        ct->head = ct->tail = NULL;
        ct->count = 0;
    }
    return ct;
}

static void LoadRecord(algo::strptr line) {
    if (g_nrecs >= MAX_RECS) return;
    line = algo::Trimmed(line);
    if (!ch_N(line) || line.elems[0] == '#') return;

    algo::Tuple tuple;
    if (!algo::Tuple_ReadStrptrMaybe(tuple, line)) return;

    algo::strptr tag = tuple.head.value;
    if (!ch_N(tag)) return;

    GRec* rec = &g_recs[g_nrecs++];
    rec->ctype_tag = tag;
    rec->tuple = tuple;
    rec->next = NULL;

    // Primary key is first attribute value
    if (tuple.attrs_n > 0) {
        rec->pkey = tuple.attrs_elems[0].value;
    }

    CTypeIndex* ct = GetOrCreateCType(tag);
    if (ct) {
        if (ct->tail) { ct->tail->next = rec; ct->tail = rec; }
        else { ct->head = ct->tail = rec; }
        ct->count++;
    }
}

static void LoadSsimFile(algo::strptr path) {
    tempstr content = algo::FileToString(path, algo::FileFlags());
    algo::StringIter iter(content);
    while (!iter.EofQ()) {
        algo::strptr line = algo::GetLine(iter);
        LoadRecord(line);
    }
}

static void LoadAllData(algo::strptr datadir) {
    // Scan data/ for subdirectories, each subdir has ssim files
    DIR* dir = opendir(datadir.elems ? (char*)algo::Zeroterm(tempstr(datadir)) : "data");
    if (!dir) return;
    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        algo::cstring subdir;
        subdir << datadir << "/" << ent->d_name;
        DIR* sub = opendir((char*)algo::Zeroterm(tempstr(subdir)));
        if (!sub) continue;
        struct dirent* sent;
        while ((sent = readdir(sub)) != NULL) {
            algo::strptr fname(sent->d_name);
            if (algo::EndsWithQ(fname, ".ssim")) {
                algo::cstring filepath;
                filepath << subdir << "/" << fname;
                LoadSsimFile(filepath);
            }
        }
        closedir(sub);
    }
    closedir(dir);
}

// Extract an attribute value by name from a tuple
static algo::strptr TupleGetAttr(const algo::Tuple& t, algo::strptr name) {
    for (u32 i = 0; i < t.attrs_n; i++) {
        if (algo::strptr(t.attrs_elems[i].name) == name) {
            return algo::strptr(t.attrs_elems[i].value);
        }
    }
    // Also check head (the type tag's value is the pkey)
    if (algo::strptr(t.head.name) == name) return algo::strptr(t.head.value);
    return algo::strptr();
}

// ============================================================================
// Terminal I/O
// ============================================================================

static void term_write(const char* s, int n) { (void)!write(1, s, n); }
static void term_str(const char* s) { term_write(s, strlen(s)); }
static void ClearScreen() { term_str("\x1b[2J\x1b[H"); }
static void ResetColor() { term_str("\x1b[0m"); }
static void MoveTo(int row, int col) {
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "\x1b[%d;%dH", row+1, col+1);
    term_write(buf, n);
}
static void PutStr(int row, int col, algo::strptr s, int maxw) {
    MoveTo(row, col);
    for (int i = 0; i < maxw; i++) {
        char c = i < s.n_elems ? s.elems[i] : ' ';
        term_write(&c, 1);
    }
}

// Style from ui.Style records
static void ApplyStyleByName(algo::strptr style_name) {
    acr_tui::FStyle* style = acr_tui::ind_style_Find(style_name);
    if (!style) return;
    algo::cstring s;
    if (style->bold) s << "\x1b[1m";
    algo::strptr fg = style->fg;
    if (fg == "black") s << "\x1b[30m"; else if (fg == "red") s << "\x1b[31m";
    else if (fg == "green") s << "\x1b[32m"; else if (fg == "yellow") s << "\x1b[33m";
    else if (fg == "blue") s << "\x1b[34m"; else if (fg == "magenta") s << "\x1b[35m";
    else if (fg == "cyan") s << "\x1b[36m"; else if (fg == "white") s << "\x1b[37m";
    algo::strptr bg = style->bg;
    if (bg == "black") s << "\x1b[40m"; else if (bg == "red") s << "\x1b[41m";
    else if (bg == "green") s << "\x1b[42m"; else if (bg == "yellow") s << "\x1b[43m";
    else if (bg == "blue") s << "\x1b[44m"; else if (bg == "magenta") s << "\x1b[45m";
    else if (bg == "cyan") s << "\x1b[46m"; else if (bg == "white") s << "\x1b[47m";
    term_write(s.ch_elems, s.ch_n);
}

static struct termios orig_termios;
static int term_rows = 24, term_cols = 80;
static volatile bool need_resize = false;

static void HandleSigwinch(int) { need_resize = true; }
static void UpdateTermSize() {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        term_rows = ws.ws_row; term_cols = ws.ws_col;
    }
    need_resize = false;
}
static void DisableRawMode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    term_str("\x1b[?25h"); ClearScreen();
}
static void EnableRawMode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(DisableRawMode);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG);
    raw.c_iflag &= ~(IXON);
    raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    term_str("\x1b[?25l");
    signal(SIGWINCH, HandleSigwinch);
    UpdateTermSize();
}

// ============================================================================
// Navigation state
// ============================================================================

static int current_level = 0;
static int selected_row = 0;
static int scroll_offset = 0;
static algo::Smallstr100 breadcrumb[16];
static bool running = true;
static algo::Smallstr50 current_path;  // active DataPath
static algo::Smallstr50 all_paths[8];
static int n_paths = 0;
static bool choosing_branch = false;
static algo::Smallstr50 active_step;  // which DataStep we're viewing data from

// Collected branch choices at current level
struct BranchChoice {
    algo::Smallstr50 step_key;
    algo::Smallstr100 source_ctype;
    int item_count;
};
static BranchChoice g_branches[16];
static int g_nbranches = 0;

// ============================================================================
// Get items at current DataStep level, filtered by parent
// ============================================================================

struct ViewItem {
    algo::cstring label;
    algo::cstring detail;
    algo::cstring key;
    bool has_children;
};

static ViewItem g_items[8192];
static int g_nitems = 0;

// Count records matching a step + parent key
static int CountStepItems(acr_tui::FDataStep& step, algo::strptr parent_key) {
    CTypeIndex* ct = FindCType(step.source_ctype);
    if (!ct) return 0;
    if (!ch_N(step.link_field) || !ch_N(parent_key)) return ct->count;
    int n = 0;
    for (GRec* rec = ct->head; rec; rec = rec->next) {
        if (!ch_N(rec->pkey)) continue;
        algo::strptr prefix = algo::Pathcomp(rec->pkey, step.link_field);
        if (prefix == parent_key) n++;
    }
    return n;
}

// Collect branch choices at next level
static void CollectBranches() {
    g_nbranches = 0;
    algo::strptr parent_key;
    if (current_level > 0) parent_key = algo::strptr(breadcrumb[current_level - 1]);

    ind_beg(acr_tui::_db_data_step_curs, ds, acr_tui::_db) {
        if (ds.p_path == current_path && ds.level == current_level && g_nbranches < 16) {
            BranchChoice& bc = g_branches[g_nbranches];
            bc.step_key = ds.data_step;
            bc.source_ctype = ds.source_ctype;
            bc.item_count = CountStepItems(ds, parent_key);
            g_nbranches++;
        }
    }ind_end;
}

static void RefreshItems() {
    g_nitems = 0;

    if (choosing_branch) {
        // Show branch choices as items
        CollectBranches();
        for (int i = 0; i < g_nbranches; i++) {
            ViewItem& item = g_items[g_nitems];
            item.label = g_branches[i].source_ctype;
            algo::cstring detail;
            detail << g_branches[i].item_count << " items";
            item.detail = detail;
            item.key = g_branches[i].step_key;
            item.has_children = g_branches[i].item_count > 0;
            g_nitems++;
        }
        return;
    }

    // Find the active DataStep
    acr_tui::FDataStep* step = NULL;
    if (ch_N(active_step)) {
        ind_beg(acr_tui::_db_data_step_curs, ds, acr_tui::_db) {
            if (ds.data_step == active_step) { step = &ds; break; }
        }ind_end;
    }
    if (!step) {
        // Fallback: find first step at current level
        ind_beg(acr_tui::_db_data_step_curs, ds, acr_tui::_db) {
            if (ds.p_path == current_path && ds.level == current_level) { step = &ds; break; }
        }ind_end;
    }
    if (!step) return;

    // Check if there are next-level steps (for has_children)
    int next_count = 0;
    ind_beg(acr_tui::_db_data_step_curs, ds, acr_tui::_db) {
        if (ds.p_path == current_path && ds.level == current_level + 1) next_count++;
    }ind_end;

    // Parent key for filtering
    algo::strptr parent_key;
    if (current_level > 0) parent_key = algo::strptr(breadcrumb[current_level - 1]);

    // Find records matching source_ctype
    CTypeIndex* ct = FindCType(step->source_ctype);
    if (!ct) return;

    algo::strptr link = step->link_field;  // e.g. ".LL" or "/LL"
    algo::strptr label_name = step->label_field;
    algo::strptr detail_name = step->detail_field;

    for (GRec* rec = ct->head; rec && g_nitems < 8192; rec = rec->next) {
        // Skip empty pkey records
        if (!ch_N(rec->pkey)) continue;

        // Apply filter: extract pathcomp from pkey, match parent
        if (ch_N(link) && ch_N(parent_key)) {
            algo::strptr pkey = rec->pkey;
            algo::strptr prefix = algo::Pathcomp(pkey, link);
            if (prefix != parent_key) continue;
        }

        ViewItem& item = g_items[g_nitems];
        // Extract label from tuple attributes
        item.label = TupleGetAttr(rec->tuple, label_name);
        // If label is empty, use pkey
        if (!ch_N(item.label)) item.label = rec->pkey;
        // For deeper levels, show just the local part (strip parent prefix)
        if (ch_N(link) && ch_N(parent_key)) {
            (void)item.label;
            // Strip prefix using the link pathcomp's complement
            // e.g. if link is ".LL", show ".LR" part
            if (link == ".LL") {
                algo::strptr local = algo::Pathcomp(rec->pkey, ".LR");
                if (ch_N(local)) item.label = local;
            } else if (link == "/LL") {
                algo::strptr local = algo::Pathcomp(rec->pkey, "/LR");
                if (ch_N(local)) item.label = local;
            }
        }

        item.detail = TupleGetAttr(rec->tuple, detail_name);
        item.key = rec->pkey;
        item.has_children = next_count > 0;
        g_nitems++;
    }
}

// ============================================================================
// Message dispatch
// ============================================================================

enum { MSG_NAVIGATE = 1, MSG_ACTIVATE = 2, MSG_CANCEL = 3, MSG_QUIT = 4, MSG_SWITCH_PATH = 5 };
struct UiMsg { u32 type; i32 direction; i32 path_idx; UiMsg() : type(0), direction(0), path_idx(0) {} };

static void DispatchMsg(const UiMsg& msg) {
    switch (msg.type) {
        case MSG_QUIT: running = false; break;
        case MSG_NAVIGATE: {
            int new_sel = selected_row + msg.direction;
            if (new_sel >= 0 && new_sel < g_nitems) selected_row = new_sel;
            int visible = term_rows - 5;
            if (selected_row < scroll_offset) scroll_offset = selected_row;
            if (selected_row >= scroll_offset + visible) scroll_offset = selected_row - visible + 1;
            break;
        }
        case MSG_ACTIVATE:
            if (choosing_branch) {
                // User selected a branch — use it and show data
                if (selected_row < g_nitems) {
                    active_step = g_items[selected_row].key;
                    choosing_branch = false;
                    selected_row = 0;
                    scroll_offset = 0;
                    RefreshItems();
                }
            } else if (selected_row < g_nitems && g_items[selected_row].has_children) {
                breadcrumb[current_level] = algo::strptr(g_items[selected_row].key);
                current_level++;
                selected_row = 0;
                scroll_offset = 0;
                // Check how many branches at next level
                int branch_count = 0;
                ind_beg(acr_tui::_db_data_step_curs, ds, acr_tui::_db) {
                    if (ds.p_path == current_path && ds.level == current_level) branch_count++;
                }ind_end;
                if (branch_count > 1) {
                    // Show branch choice menu
                    choosing_branch = true;
                    active_step = algo::Smallstr50();
                } else {
                    // Single branch — use it directly
                    choosing_branch = false;
                    ind_beg(acr_tui::_db_data_step_curs, ds, acr_tui::_db) {
                        if (ds.p_path == current_path && ds.level == current_level) {
                            active_step = ds.data_step; break;
                        }
                    }ind_end;
                }
                RefreshItems();
            }
            break;
        case MSG_CANCEL:
            if (choosing_branch) {
                // Go back from branch choice to parent level
                choosing_branch = false;
                current_level--;
                selected_row = 0;
                scroll_offset = 0;
                RefreshItems();
            } else if (current_level > 0) {
                current_level--;
                selected_row = 0;
                scroll_offset = 0;
                // Check if this level has branches
                int branch_count = 0;
                ind_beg(acr_tui::_db_data_step_curs, ds, acr_tui::_db) {
                    if (ds.p_path == current_path && ds.level == current_level) branch_count++;
                }ind_end;
                choosing_branch = branch_count > 1;
                if (!choosing_branch) {
                    ind_beg(acr_tui::_db_data_step_curs, ds, acr_tui::_db) {
                        if (ds.p_path == current_path && ds.level == current_level) {
                            active_step = ds.data_step; break;
                        }
                    }ind_end;
                }
                RefreshItems();
            }
            break;
        case MSG_SWITCH_PATH:
            if (msg.path_idx >= 0 && msg.path_idx < n_paths) {
                current_path = all_paths[msg.path_idx];
                current_level = 0;
                selected_row = 0;
                scroll_offset = 0;
                choosing_branch = false;
                active_step = algo::Smallstr50();
                // Set active_step to first step at level 0
                ind_beg(acr_tui::_db_data_step_curs, ds, acr_tui::_db) {
                    if (ds.p_path == current_path && ds.level == 0) { active_step = ds.data_step; break; }
                }ind_end;
                RefreshItems();
            }
            break;
    }
}

// ============================================================================
// Input: read key, translate via keymap
// ============================================================================

static UiMsg ReadInput() {
    UiMsg msg;
    char ch;
    if (read(STDIN_FILENO, &ch, 1) != 1) { msg.type = MSG_QUIT; return msg; }

    algo::Smallstr20 key_name;
    if (ch == 27) {
        char seq[2];
        if (read(STDIN_FILENO, &seq[0], 1) == 1 && seq[0] == '[') {
            if (read(STDIN_FILENO, &seq[1], 1) == 1) {
                if (seq[1] == 'A') key_name = "Up";
                else if (seq[1] == 'B') key_name = "Down";
                else if (seq[1] == 'C') key_name = "Right";
                else if (seq[1] == 'D') key_name = "Left";
            }
        }
        if (!ch_N(key_name)) key_name = "Esc";
    } else if (ch == '\r' || ch == '\n') key_name = "Enter";
    else if (ch == '\t') key_name = "Tab";
    else if (ch == 3) { msg.type = MSG_QUIT; return msg; }
    else ch_Add(key_name, ch);

    // Path switching: 1-9 switches DataPath
    if (ch >= '1' && ch <= '9') {
        int idx = ch - '1';
        if (idx < n_paths) {
            msg.type = MSG_SWITCH_PATH;
            msg.path_idx = idx;
            return msg;
        }
    }

    // Match keymaps from ui.key_map
    ind_beg(acr_tui::_db_key_map_curs, km, acr_tui::_db) {
        if (km.key != key_name) continue;
        // p_widget scope check (empty = global)
        if (ch_N(km.p_widget) > 0) {
            // For now, all keys apply to the browser widget
        }
        algo::strptr action = km.action;
        if (action == "quit")           msg.type = MSG_QUIT;
        else if (action == "navigate_next") { msg.type = MSG_NAVIGATE; msg.direction = 1; }
        else if (action == "navigate_prev") { msg.type = MSG_NAVIGATE; msg.direction = -1; }
        else if (action == "activate")  msg.type = MSG_ACTIVATE;
        else if (action == "cancel")    msg.type = MSG_CANCEL;
        break;
    }ind_end;

    return msg;
}

// ============================================================================
// Renderer — reads widget definitions from ui.* schema
// ============================================================================

static void Render() {
    if (need_resize) UpdateTermSize();
    int W = term_cols;

    ClearScreen();

    // Find title widget and render it
    ind_beg(acr_tui::_db_widget_curs, w, acr_tui::_db) {
        if (w.type == "label" && w.visible) {
            ApplyStyleByName(w.p_style);
            PutStr(w.row, w.col, w.text, W);
            ResetColor();
        }
    }ind_end;

    // Breadcrumb bar (row 1)
    algo::cstring crumb;
    crumb << " " << current_path;
    for (int i = 0; i < current_level; i++) {
        crumb << " \xe2\x96\xb8 " << breadcrumb[i];
    }
    if (choosing_branch) {
        crumb << " \xe2\x96\xb8 [choose relationship]";
    } else if (ch_N(active_step)) {
        ind_beg(acr_tui::_db_data_step_curs, ds, acr_tui::_db) {
            if (ds.data_step == active_step) {
                crumb << " \xe2\x96\xb8 [" << ds.source_ctype << "]";
                break;
            }
        }ind_end;
    }
    term_str("\x1b[33m");
    PutStr(1, 0, crumb, W);
    ResetColor();

    // Separator
    term_str("\x1b[34m");
    MoveTo(2, 0);
    for (int i = 0; i < W; i++) term_str("\xe2\x94\x80");
    ResetColor();

    // Items — use selected style from ui.style_slot or fallback
    int visible = term_rows - 5;
    for (int i = 0; i < visible && (scroll_offset + i) < g_nitems; i++) {
        int idx = scroll_offset + i;
        ViewItem& item = g_items[idx];
        bool sel = idx == selected_row;

        if (sel) {
            // Try to find "selected" style
            ApplyStyleByName("selected");
        }

        algo::cstring line;
        if (item.has_children) line << " \xe2\x96\xb8 ";
        else line << "   ";
        line << item.label;

        if (ch_N(item.detail)) {
            int pad = 35;
            while (ch_N(line) < pad) line << " ";
            if (!sel) term_str("\x1b[90m");
            line << item.detail;
        }

        PutStr(3 + i, 0, line, W);
        ResetColor();
    }

    // Scroll bar
    if (g_nitems > visible && visible > 0) {
        int bar_h = (visible * visible) / g_nitems;
        if (bar_h < 1) bar_h = 1;
        int bar_pos = (scroll_offset * visible) / g_nitems;
        term_str("\x1b[90m");
        for (int i = 0; i < visible; i++) {
            MoveTo(3 + i, W - 1);
            term_str((i >= bar_pos && i < bar_pos + bar_h) ? "\xe2\x96\x88" : "\xe2\x94\x82");
        }
        ResetColor();
    }

    // Bottom separator
    term_str("\x1b[34m");
    MoveTo(term_rows - 2, 0);
    for (int i = 0; i < W; i++) term_str("\xe2\x94\x80");
    ResetColor();

    // Status bar — from statusbar widget
    ind_beg(acr_tui::_db_widget_curs, w, acr_tui::_db) {
        if (w.type == "statusbar" && w.visible) {
            ApplyStyleByName(w.p_style);
            algo::cstring status;
            // Show path tabs
            for (int pi = 0; pi < n_paths; pi++) {
                if (all_paths[pi] == current_path) {
                    status << " [" << (pi+1) << ":" << all_paths[pi] << "]";
                } else {
                    status << "  " << (pi+1) << ":" << all_paths[pi];
                }
            }
            status << "  " << g_nitems << " items  q:quit";
            PutStr(term_rows - 1, 0, status, W);
            ResetColor();
        }
    }ind_end;
}

// ============================================================================
// Main
// ============================================================================

void acr_tui::Main() {
    // Load all ssim data generically from data/ directory
    LoadAllData("data");

    // Collect available DataPaths
    ind_beg(acr_tui::_db_data_path_curs, dp, acr_tui::_db) {
        if (n_paths < 8) {
            all_paths[n_paths++] = dp.data_path;
        }
    }ind_end;
    if (n_paths > 0) current_path = all_paths[0];

    RefreshItems();

    EnableRawMode();

    while (running) {
        Render();
        UiMsg msg = ReadInput();
        if (msg.type != 0) DispatchMsg(msg);
    }
}
