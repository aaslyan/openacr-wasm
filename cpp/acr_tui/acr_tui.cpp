// Copyright (C) 2026 AlgoRND
//
// License: GPL
// Target: acr_tui (exe) -- Terminal UI browser for OpenACR schemas
// Source: cpp/acr_tui/acr_tui.cpp
//
// Terminal frontend — uses shared logic from acr_browse.h

#include "include/acr_browse.h"
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <signal.h>

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
// External tool view (terminal only — pipes to amc_vis / acr)
// ============================================================================

static void RunExternalView() {
    graph_output = algo::cstring();
    graph_scroll = 0;

    algo::cstring cmd;
    algo::strptr key;
    if (selected_row < g_nitems) key = algo::strptr(g_items[selected_row].key);

    acr_tui::FDataStep* step = NULL;
    if (ch_N(active_step)) {
        ind_beg(acr_tui::_db_data_step_curs, ds, acr_tui::_db) {
            if (ds.data_step == active_step) { step = &ds; break; }
        }ind_end;
    }

    if (view_mode == VIEW_GRAPH && step && ch_N(key)) {
        algo::cstring arg;
        if (step->source_ctype == "dmmeta.ctype") arg = key;
        else if (step->source_ctype == "dmmeta.field") arg = algo::Pathcomp(key, ".RL");
        else if (step->source_ctype == "dmmeta.ns") { arg << key << ".%"; }
        else arg = key;
        if (ch_N(arg)) cmd << "amc_vis " << arg << " -xref 2>&1";
    } else if (view_mode == VIEW_RAW && step && ch_N(key)) {
        cmd << "acr -t " << step->source_ctype << ":" << key << " 2>&1";
    }

    if (!ch_N(cmd)) { graph_output = "No visualization available"; return; }

    FILE* pipe = popen((char*)algo::Zeroterm(tempstr(cmd)), "r");
    if (!pipe) { graph_output = "Failed to run command"; return; }
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe)) graph_output << buf;
    pclose(pipe);
}

// ============================================================================
// Input
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

    if (ch == 'v') {
        msg.type = MSG_TOGGLE_VIEW;
        if ((view_mode + 1) % 3 != VIEW_LIST) RunExternalView();
        return msg;
    }

    if (view_mode != VIEW_LIST) {
        if (key_name == "j" || key_name == "Down") { graph_scroll++; return msg; }
        if (key_name == "k" || key_name == "Up") { if (graph_scroll > 0) graph_scroll--; return msg; }
        if (key_name == "Esc" || key_name == "Left" || key_name == "h") { view_mode = VIEW_LIST; return msg; }
        if (key_name == "q") { msg.type = MSG_QUIT; return msg; }
        return msg;
    }

    if (ch >= '1' && ch <= '9') {
        int idx = ch - '1';
        if (idx < n_paths) { msg.type = MSG_SWITCH_PATH; msg.path_idx = idx; return msg; }
    }

    ind_beg(acr_tui::_db_key_map_curs, km, acr_tui::_db) {
        if (km.key != key_name) continue;
        if (ch_N(km.p_widget) > 0) {}
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
// Render
// ============================================================================

static void Render() {
    if (need_resize) UpdateTermSize();
    int W = term_cols;
    ClearScreen();

    // Title
    ind_beg(acr_tui::_db_widget_curs, w, acr_tui::_db) {
        if (w.type == "label" && w.visible) {
            ApplyStyleByName(w.p_style);
            PutStr(w.row, w.col, w.text, W);
            ResetColor();
        }
    }ind_end;

    // Breadcrumb
    algo::cstring crumb = GetBreadcrumb();
    term_str("\x1b[33m");
    PutStr(1, 0, crumb, W);
    ResetColor();

    // Separator
    term_str("\x1b[34m");
    MoveTo(2, 0);
    for (int i = 0; i < W; i++) term_str("\xe2\x94\x80");
    ResetColor();

    int visible = term_rows - 5;

    if (view_mode == VIEW_LIST) {
        for (int i = 0; i < visible && (scroll_offset + i) < g_nitems; i++) {
            int idx = scroll_offset + i;
            ViewItem& item = g_items[idx];
            bool sel = idx == selected_row;
            if (sel) ApplyStyleByName("selected");
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
    } else {
        algo::StringIter iter(graph_output);
        int line_num = 0, row = 0;
        while (!iter.EofQ() && row < visible) {
            algo::strptr line = algo::GetLine(iter);
            if (line_num >= graph_scroll) {
                term_str("\x1b[36m");
                PutStr(3 + row, 0, line, W);
                ResetColor();
                row++;
            }
            line_num++;
        }
    }

    // Bottom separator
    term_str("\x1b[34m");
    MoveTo(term_rows - 2, 0);
    for (int i = 0; i < W; i++) term_str("\xe2\x94\x80");
    ResetColor();

    // Status bar
    ind_beg(acr_tui::_db_widget_curs, w, acr_tui::_db) {
        if (w.type == "statusbar" && w.visible) {
            ApplyStyleByName(w.p_style);
            algo::cstring status;
            for (int pi = 0; pi < n_paths; pi++) {
                if (all_paths[pi] == current_path) status << " [" << (pi+1) << ":" << all_paths[pi] << "]";
                else status << "  " << (pi+1) << ":" << all_paths[pi];
            }
            const char* vname = view_mode == VIEW_LIST ? "list" : view_mode == VIEW_GRAPH ? "GRAPH" : "RAW";
            status << "  v:" << vname << "  " << g_nitems << " items";
            PutStr(term_rows - 1, 0, status, W);
            ResetColor();
        }
    }ind_end;
}

// ============================================================================
// Main
// ============================================================================

void acr_tui::Main() {
    LoadAllData("data");
    InitPaths();
    RefreshItems();

    // Check for -replay mode: read ssim messages from stdin, dump state, no terminal
    bool replay_mode = false;
    // If stdin is not a terminal, assume replay mode
    if (!isatty(STDIN_FILENO)) {
        replay_mode = true;
    }

    if (replay_mode) {
        // Read ssim messages from stdin, dispatch each, print final state
        char buf[4096];
        while (fgets(buf, sizeof(buf), stdin)) {
            algo::strptr line(buf, strlen(buf));
            if (!DispatchSsimMsg(line, 30)) break;
            RefreshItems();
        }
        // Dump final state as ssim to stdout
        algo::cstring state = DumpState();
        (void)!write(1, state.ch_elems, state.ch_n);
        return;
    }

    EnableRawMode();

    while (running) {
        Render();
        UiMsg msg = ReadInput();
        if (msg.type != 0) DispatchMsg(msg, term_rows - 5);
    }
}
