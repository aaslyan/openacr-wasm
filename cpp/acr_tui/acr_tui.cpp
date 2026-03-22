// Copyright (C) 2026 AlgoRND
//
// License: GPL
// Target: acr_tui (exe) -- Terminal UI browser for OpenACR schemas
// Exceptions: yes
// Source: cpp/acr_tui/acr_tui.cpp
//
// Message-driven TUI with DataPath navigation
// Drill in with Enter/Right/l, go back with Esc/Left/h

#include "include/algo.h"
#include "include/acr_tui.h"
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <signal.h>

// ============================================================================
// Message types
// ============================================================================

enum UiMsgType : u32 {
    UIMSG_NAVIGATE  = 2,
    UIMSG_ACTIVATE  = 13,  // drill in
    UIMSG_CANCEL    = 14,  // go back
    UIMSG_QUIT      = 10,
};

struct UiMsg {
    u32 type;
    i32 direction;
    UiMsg() : type(0), direction(0) {}
};

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

static struct termios orig_termios;
static int term_rows = 24, term_cols = 80;
static volatile bool need_resize = false;

static void HandleSigwinch(int) { need_resize = true; }

static void UpdateTermSize() {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        term_rows = ws.ws_row;
        term_cols = ws.ws_col;
    }
    need_resize = false;
}

static void DisableRawMode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    term_str("\x1b[?25h");  // show cursor
    ClearScreen();
}

static void EnableRawMode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(DisableRawMode);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG);
    raw.c_iflag &= ~(IXON);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    term_str("\x1b[?25l");  // hide cursor
    signal(SIGWINCH, HandleSigwinch);
    UpdateTermSize();
}

// ============================================================================
// Navigation state
// ============================================================================

static int current_level = 0;
static int selected_row = 0;
static int scroll_offset = 0;
static algo::Smallstr100 breadcrumb[8];  // selected key at each level
static int msg_count = 0;
static bool running = true;

// ============================================================================
// Data access — get items at current level filtered by parent key
// ============================================================================

struct ListItem {
    algo::cstring label;
    algo::cstring detail;
    algo::cstring key;       // primary key for drill-in
    bool has_children;       // can drill deeper
};

static int GetItems(ListItem* items, int max_items) {
    int n = 0;

    // Find current step
    acr_tui::FDataStep* step = NULL;
    ind_beg(acr_tui::_db_data_step_curs, ds, acr_tui::_db) {
        if (ds.level == current_level) { step = &ds; break; }
    }ind_end;
    if (!step) return 0;

    // Find next step (to know if drill-in is possible)
    bool has_next = false;
    ind_beg(acr_tui::_db_data_step_curs, ds, acr_tui::_db) {
        if (ds.level == current_level + 1) { has_next = true; break; }
    }ind_end;

    algo::strptr parent_key;
    if (current_level > 0) parent_key = algo::strptr(breadcrumb[current_level - 1]);

    // Iterate the appropriate data based on source_ctype
    if (step->source_ctype == "dmmeta.Ns") {
        ind_beg(acr_tui::_db_ns_curs, ns, acr_tui::_db) {
            if (n >= max_items) break;
            items[n].label = ns.ns;
            items[n].detail = ns.comment;
            items[n].key = ns.ns;
            items[n].has_children = has_next;
            n++;
        }ind_end;
    } else if (step->source_ctype == "dmmeta.Ctype") {
        ind_beg(acr_tui::_db_ctype_curs, ct, acr_tui::_db) {
            if (n >= max_items) break;
            // Filter by parent namespace
            algo::strptr ct_ns = algo::Pathcomp(ct.ctype, ".LL");
            if (ch_N(parent_key) > 0 && ct_ns != parent_key) continue;
            algo::strptr ct_name = algo::Pathcomp(ct.ctype, ".LR");
            items[n].label = ct_name;
            items[n].detail = ct.comment;
            items[n].key = ct.ctype;
            items[n].has_children = has_next;
            n++;
        }ind_end;
    } else if (step->source_ctype == "dmmeta.Field") {
        ind_beg(acr_tui::_db_field_curs, fld, acr_tui::_db) {
            if (n >= max_items) break;
            algo::strptr fld_parent = algo::Pathcomp(fld.field, ".LL");
            if (ch_N(parent_key) > 0 && fld_parent != parent_key) continue;
            algo::strptr fld_name = algo::Pathcomp(fld.field, ".LR");
            algo::cstring detail;
            detail << fld.arg << "  " << fld.reftype;
            { algo::cstring dflt_str; algo::CppExpr_Print(fld.dflt, dflt_str);
              if (ch_N(dflt_str)) detail << "  dflt:" << dflt_str; }
            items[n].label = fld_name;
            items[n].detail = detail;
            items[n].key = fld.field;
            items[n].has_children = has_next;
            n++;
        }ind_end;
    } else if (step->source_ctype == "dmmeta.Fconst") {
        ind_beg(acr_tui::_db_fconst_curs, fc, acr_tui::_db) {
            if (n >= max_items) break;
            algo::strptr fc_parent = algo::Pathcomp(fc.fconst, "/LL");
            if (ch_N(parent_key) > 0 && fc_parent != parent_key) continue;
            algo::strptr fc_name = algo::Pathcomp(fc.fconst, "/LR");
            items[n].label = fc_name;
            { algo::cstring val; algo::CppExpr_Print(fc.value, val); items[n].detail = val; }
            items[n].key = fc.fconst;
            items[n].has_children = false;
            n++;
        }ind_end;
    }
    return n;
}

// ============================================================================
// Message dispatch
// ============================================================================

static ListItem g_items[4096];
static int g_nitems = 0;

static void HandleNavigate(const UiMsg& msg) {
    if (msg.direction > 0 && selected_row < g_nitems - 1) {
        selected_row++;
    } else if (msg.direction < 0 && selected_row > 0) {
        selected_row--;
    }
    // Scroll viewport
    int visible = term_rows - 5;
    if (selected_row < scroll_offset) scroll_offset = selected_row;
    if (selected_row >= scroll_offset + visible) scroll_offset = selected_row - visible + 1;
}

static void HandleActivate(const UiMsg&) {
    if (selected_row < g_nitems && g_items[selected_row].has_children) {
        breadcrumb[current_level] = algo::strptr(g_items[selected_row].key);
        current_level++;
        selected_row = 0;
        scroll_offset = 0;
    }
}

static void HandleCancel(const UiMsg&) {
    if (current_level > 0) {
        current_level--;
        selected_row = 0;
        scroll_offset = 0;
    }
}

static void DispatchMsg(const UiMsg& msg) {
    msg_count++;
    switch (msg.type) {
        case UIMSG_QUIT:     running = false; break;
        case UIMSG_NAVIGATE: HandleNavigate(msg); break;
        case UIMSG_ACTIVATE: HandleActivate(msg); break;
        case UIMSG_CANCEL:   HandleCancel(msg); break;
        default: break;
    }
}

// ============================================================================
// Input: read key (handles escape sequences for arrows)
// ============================================================================

static UiMsg ReadInput() {
    UiMsg msg;
    char ch;
    if (read(STDIN_FILENO, &ch, 1) != 1) { msg.type = UIMSG_QUIT; return msg; }

    algo::Smallstr20 key_name;
    if (ch == 27) {
        // Escape sequence or bare Esc
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
    } else if (ch == '\r' || ch == '\n') {
        key_name = "Enter";
    } else if (ch == '\t') {
        key_name = "Tab";
    } else if (ch == 3) {  // ctrl-c
        msg.type = UIMSG_QUIT;
        return msg;
    } else {
        ch_Add(key_name, ch);
    }

    // Match against keymaps
    ind_beg(acr_tui::_db_key_map_curs, km, acr_tui::_db) {
        if (km.key != key_name) continue;
        if (ch_N(km.p_widget) > 0 && algo::strptr(km.p_widget) != "browser") continue;
        if (km.action == "quit")          msg.type = UIMSG_QUIT;
        else if (km.action == "navigate_next") { msg.type = UIMSG_NAVIGATE; msg.direction = 1; }
        else if (km.action == "navigate_prev") { msg.type = UIMSG_NAVIGATE; msg.direction = -1; }
        else if (km.action == "activate") msg.type = UIMSG_ACTIVATE;
        else if (km.action == "cancel")   msg.type = UIMSG_CANCEL;
        break;
    }ind_end;

    return msg;
}

// ============================================================================
// Renderer
// ============================================================================

static void Render() {
    if (need_resize) UpdateTermSize();

    g_nitems = GetItems(g_items, 4096);

    ClearScreen();
    int W = term_cols;

    // Title bar
    term_str("\x1b[1m\x1b[36m");
    algo::cstring title;
    title << "OpenACR Schema Browser";
    PutStr(0, 0, title, W);
    ResetColor();

    // Breadcrumb
    algo::cstring crumb;
    crumb << " ";
    for (int i = 0; i < current_level; i++) {
        if (i > 0) crumb << " \xe2\x96\xb8 ";
        crumb << breadcrumb[i];
    }
    term_str("\x1b[33m");
    PutStr(1, 0, crumb, W);
    ResetColor();

    // Border
    term_str("\x1b[34m");
    MoveTo(2, 0);
    for (int i = 0; i < W; i++) term_str("\xe2\x94\x80");
    ResetColor();

    // Items
    int visible = term_rows - 5;
    for (int i = 0; i < visible && (scroll_offset + i) < g_nitems; i++) {
        int idx = scroll_offset + i;
        ListItem& item = g_items[idx];
        bool sel = idx == selected_row;

        if (sel) term_str("\x1b[30m\x1b[46m");

        // Arrow indicator for drillable items
        algo::cstring line;
        if (item.has_children) {
            line << " \xe2\x96\xb8 ";
        } else {
            line << "   ";
        }
        line << item.label;
        if (ch_N(item.detail)) {
            // Pad label to fixed width then show detail
            int label_w = 30;
            while (ch_N(line) < label_w) line << " ";
            term_str(sel ? "" : "\x1b[90m");  // dim for detail
            line << item.detail;
        }
        PutStr(3 + i, 0, line, W);
        ResetColor();
    }

    // Scroll indicator
    if (g_nitems > visible) {
        int bar_h = visible > 0 ? (visible * visible / g_nitems) : 1;
        if (bar_h < 1) bar_h = 1;
        int bar_pos = visible > 0 ? (scroll_offset * visible / g_nitems) : 0;
        term_str("\x1b[90m");
        for (int i = 0; i < visible; i++) {
            MoveTo(3 + i, W - 1);
            if (i >= bar_pos && i < bar_pos + bar_h) term_str("\xe2\x96\x88");
            else term_str("\xe2\x94\x82");
        }
        ResetColor();
    }

    // Bottom border
    term_str("\x1b[34m");
    MoveTo(term_rows - 2, 0);
    for (int i = 0; i < W; i++) term_str("\xe2\x94\x80");
    ResetColor();

    // Status bar
    term_str("\x1b[37m\x1b[44m");
    algo::cstring status;
    status << " \xe2\x86\x91\xe2\x86\x93:navigate  "
           << "Enter/\xe2\x86\x92:open  "
           << "Esc/\xe2\x86\x90:back  "
           << "q:quit  "
           << g_nitems << " items  "
           << "level:" << current_level;
    PutStr(term_rows - 1, 0, status, W);
    ResetColor();
}

// ============================================================================
// Main
// ============================================================================

void acr_tui::Main() {
    EnableRawMode();

    while (running) {
        Render();
        UiMsg msg = ReadInput();
        if (msg.type != 0) DispatchMsg(msg);
    }
}
