// Copyright (C) 2026 AlgoRND
//
// License: GPL
// Target: acr_tui (exe) -- Terminal UI browser for OpenACR schemas
// Exceptions: yes
// Source: cpp/acr_tui/acr_tui.cpp
//
// Message-driven TUI: keypress → uimsg → dispatch → state update → render

#include "include/algo.h"
#include "include/acr_tui.h"
#include <termios.h>
#include <unistd.h>

// ============================================================================
// Message types (matching uimsg protocol, inline for now until amc generates dispatch)
// ============================================================================

enum UiMsgType : u32 {
    UIMSG_KEYPRESS   = 1,
    UIMSG_NAVIGATE   = 2,
    UIMSG_FOCUS      = 3,
    UIMSG_SCROLL     = 4,
    UIMSG_EXPAND     = 5,
    UIMSG_EDIT       = 6,
    UIMSG_SELECT     = 7,
    UIMSG_INSERT     = 8,
    UIMSG_DELETE     = 9,
    UIMSG_QUIT       = 10,
    UIMSG_REFRESH    = 11,
    UIMSG_COMMAND    = 12,
};

struct UiMsg {
    u32 type;
    u32 length;
    // Payload fields (union-like, used based on type)
    algo::Smallstr50 widget;   // target widget
    i32 direction;             // navigate: 1=next, -1=prev
    bool expand;               // expand: true/false
    u8 key;                    // keypress: raw key
    UiMsg() : type(0), length(sizeof(UiMsg)), direction(0), expand(false), key(0) {}
};

// ============================================================================
// Message log (ring buffer for replay/debug)
// ============================================================================

static UiMsg msg_log[1024];
static int msg_log_n = 0;

static void LogMsg(const UiMsg& msg) {
    msg_log[msg_log_n % 1024] = msg;
    msg_log_n++;
}

// ============================================================================
// UI State
// ============================================================================

static int focus_idx = 0;
static algo::cstring focus_widgets[16];
static int n_focus = 0;
static int selected[16] = {};
static bool expanded[256] = {};

static algo::strptr FocusedWidget() {
    return n_focus > 0 ? algo::strptr(focus_widgets[focus_idx]) : algo::strptr();
}

// ============================================================================
// Message dispatch — each handler is a pure state update, no I/O
// ============================================================================

static bool running = true;

static void HandleQuit(const UiMsg&) {
    running = false;
}

static void HandleNavigate(const UiMsg& msg) {
    if (msg.direction > 0) {
        selected[focus_idx]++;
    } else if (msg.direction < 0 && selected[focus_idx] > 0) {
        selected[focus_idx]--;
    }
}

static void HandleFocus(const UiMsg&) {
    if (n_focus > 0) {
        focus_idx = (focus_idx + 1) % n_focus;
    }
}

static void HandleExpand(const UiMsg&) {
    int idx = selected[focus_idx];
    expanded[idx % 256] = !expanded[idx % 256];
}

static void HandleRefresh(const UiMsg&) {
    // No-op — render loop will pick up state
}

static void DispatchMsg(const UiMsg& msg) {
    LogMsg(msg);
    switch (msg.type) {
        case UIMSG_QUIT:      HandleQuit(msg); break;
        case UIMSG_NAVIGATE:  HandleNavigate(msg); break;
        case UIMSG_FOCUS:     HandleFocus(msg); break;
        case UIMSG_EXPAND:    HandleExpand(msg); break;
        case UIMSG_REFRESH:   HandleRefresh(msg); break;
        default: break;
    }
}

// ============================================================================
// Input translator: keypress → keymap lookup → message
// ============================================================================

static UiMsg TranslateKey(char ch) {
    UiMsg msg;
    // Map raw key to key name
    algo::Smallstr20 key_name;
    if (ch == '\t') key_name = "Tab";
    else if (ch == '\r') key_name = "Enter";
    else if (ch == 27) key_name = "Esc";
    else { ch_Add(key_name, ch); }

    algo::strptr focused = FocusedWidget();

    // Match against keymaps
    ind_beg(acr_tui::_db_key_map_curs, km, acr_tui::_db) {
        if (km.key != key_name) continue;
        // Check widget scope
        if (ch_N(km.p_widget) > 0 && algo::strptr(km.p_widget) != focused) continue;

        // Convert action to message type
        if (km.action == "quit") {
            msg.type = UIMSG_QUIT;
        } else if (km.action == "navigate_next") {
            msg.type = UIMSG_NAVIGATE;
            msg.widget = focused;
            msg.direction = 1;
        } else if (km.action == "navigate_prev") {
            msg.type = UIMSG_NAVIGATE;
            msg.widget = focused;
            msg.direction = -1;
        } else if (km.action == "focus_next") {
            msg.type = UIMSG_FOCUS;
        } else if (km.action == "toggle_expand") {
            msg.type = UIMSG_EXPAND;
            msg.widget = focused;
        } else if (km.action == "refresh") {
            msg.type = UIMSG_REFRESH;
        }
        break;  // first match wins
    }ind_end;

    // Also handle ctrl-c
    if (ch == 3) {
        msg.type = UIMSG_QUIT;
    }

    return msg;
}

// ============================================================================
// Terminal renderer — reads widget tree + state, writes ANSI
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

static void SetColor(algo::strptr fg, algo::strptr bg, bool bold) {
    algo::cstring s;
    if (bold) s << "\x1b[1m";
    if (fg == "black") s << "\x1b[30m"; else if (fg == "red") s << "\x1b[31m";
    else if (fg == "green") s << "\x1b[32m"; else if (fg == "yellow") s << "\x1b[33m";
    else if (fg == "blue") s << "\x1b[34m"; else if (fg == "magenta") s << "\x1b[35m";
    else if (fg == "cyan") s << "\x1b[36m"; else if (fg == "white") s << "\x1b[37m";
    if (bg == "blue") s << "\x1b[44m"; else if (bg == "cyan") s << "\x1b[46m";
    else if (bg == "black") s << "\x1b[40m"; else if (bg == "red") s << "\x1b[41m";
    else if (bg == "green") s << "\x1b[42m"; else if (bg == "yellow") s << "\x1b[43m";
    else if (bg == "magenta") s << "\x1b[45m"; else if (bg == "white") s << "\x1b[47m";
    term_write(s.ch_elems, s.ch_n);
}

static void PutStr(int row, int col, algo::strptr s, int maxw) {
    MoveTo(row, col);
    for (int i = 0; i < maxw; i++) {
        char c = i < s.n_elems ? s.elems[i] : ' ';
        term_write(&c, 1);
    }
}

// Box drawing
static void DrawBorder(int r, int c, int w, int h, algo::strptr title) {
    MoveTo(r, c); term_str("\xe2\x94\x8c");
    for (int i = 1; i < w-1; i++) term_str("\xe2\x94\x80");
    term_str("\xe2\x94\x90");
    if (ch_N(title)) {
        MoveTo(r, c+2); term_str(" ");
        term_write(title.elems, title.n_elems);
        term_str(" ");
    }
    for (int i = 1; i < h-1; i++) {
        MoveTo(r+i, c); term_str("\xe2\x94\x82");
        MoveTo(r+i, c+w-1); term_str("\xe2\x94\x82");
    }
    MoveTo(r+h-1, c); term_str("\xe2\x94\x94");
    for (int i = 1; i < w-1; i++) term_str("\xe2\x94\x80");
    term_str("\xe2\x94\x98");
}

static acr_tui::FStyle* FindStyle(algo::strptr key) {
    return acr_tui::ind_style_Find(key);
}

static void ApplyStyle(acr_tui::FStyle* style) {
    if (style) SetColor(style->fg, style->bg, style->bold);
}

static void RenderLabel(acr_tui::FWidget& w) {
    acr_tui::FStyle* style = FindStyle(w.p_style);
    if (style) ApplyStyle(style);
    PutStr(w.row, w.col, w.text, w.w);
    ResetColor();
}

static void RenderStatusBar(acr_tui::FWidget& w) {
    acr_tui::FStyle* style = FindStyle(w.p_style);
    if (style) ApplyStyle(style);
    algo::cstring text;
    text << w.text << "  [" << FocusedWidget() << "] msgs:" << msg_log_n;
    PutStr(w.row, w.col, text, w.w);
    ResetColor();
}

static void RenderTable(acr_tui::FWidget& w) {
    acr_tui::FStyle* style = FindStyle(w.p_style);
    if (style) ApplyStyle(style);
    bool is_focused = FocusedWidget() == w.widget;
    int r = w.row, c = w.col, wi = w.w, h = w.h;

    DrawBorder(r, c, wi, h, w.title);

    // Collect columns
    acr_tui::FColumn* cols[32];
    int ncols = 0;
    ind_beg(acr_tui::_db_column_curs, col, acr_tui::_db) {
        if (col.p_widget == w.widget && ncols < 32) cols[ncols++] = &col;
    }ind_end;

    acr_tui::FTableCfg* cfg = NULL;
    ind_beg(acr_tui::_db_table_cfg_curs, tc, acr_tui::_db) {
        if (tc.p_widget == w.widget) { cfg = &tc; break; }
    }ind_end;

    int row = r + 1;

    // Header
    if (cfg && cfg->header && ncols > 0) {
        if (style) ApplyStyle(style);
        term_str("\x1b[1m\x1b[4m");
        int coloff = c + 1;
        for (int ci = 0; ci < ncols; ci++) {
            PutStr(row, coloff, cols[ci]->title, cols[ci]->w);
            coloff += cols[ci]->w + 1;
        }
        ResetColor();
        row++;
    }

    // Data: iterate fields for dmmeta.Field binding
    int data_idx = 0;
    ind_beg(acr_tui::_db_field_curs, fld, acr_tui::_db) {
        if (row >= r + h - 1) break;
        bool sel = is_focused && data_idx == selected[focus_idx];
        if (sel) term_str("\x1b[30m\x1b[46m");
        else if (style) ApplyStyle(style);
        int coloff = c + 1;
        for (int ci = 0; ci < ncols; ci++) {
            algo::strptr val;
            if (cols[ci]->field == "field") val = fld.field;
            else if (cols[ci]->field == "arg") val = fld.arg;
            else if (cols[ci]->field == "reftype") val = fld.reftype;
            else if (cols[ci]->field == "comment") val = fld.comment;
            PutStr(row, coloff, val, cols[ci]->w);
            coloff += cols[ci]->w + 1;
        }
        ResetColor();
        row++;
        data_idx++;
    }ind_end;
}

static void RenderTree(acr_tui::FWidget& w) {
    acr_tui::FStyle* style = FindStyle(w.p_style);
    if (style) ApplyStyle(style);
    bool is_focused = FocusedWidget() == w.widget;
    int r = w.row, c = w.col, wi = w.w, h = w.h;

    DrawBorder(r, c, wi, h, w.title);

    int row = r + 1;
    int data_idx = 0;

    ind_beg(acr_tui::_db_ns_curs, ns, acr_tui::_db) {
        if (row >= r + h - 1) break;
        bool is_exp = expanded[data_idx % 256];
        bool sel = is_focused && data_idx == selected[focus_idx];
        if (sel) term_str("\x1b[30m\x1b[46m");
        else if (style) ApplyStyle(style);

        algo::cstring line;
        line << (is_exp ? "\xe2\x96\xbc " : "\xe2\x96\xb6 ") << ns.ns;
        if (ch_N(ns.comment)) line << " - " << ns.comment;
        PutStr(row, c+1, line, wi-2);
        ResetColor();
        row++;
        (void)data_idx;
        data_idx++;

        if (is_exp) {
            ind_beg(acr_tui::_db_ctype_curs, ct, acr_tui::_db) {
                if (row >= r + h - 1) break;
                algo::strptr ct_ns = algo::Pathcomp(ct.ctype, ".LL");
                if (ct_ns != ns.ns) continue;
                algo::strptr ct_name = algo::Pathcomp(ct.ctype, ".LR");
                bool ct_sel = is_focused && data_idx == selected[focus_idx];
                if (ct_sel) term_str("\x1b[30m\x1b[46m");
                else if (style) ApplyStyle(style);
                algo::cstring cline;
                cline << "  \xe2\x94\x9c\xe2\x94\x80 " << ct_name;
                if (ch_N(ct.comment)) cline << " - " << ct.comment;
                PutStr(row, c+1, cline, wi-2);
                ResetColor();
                row++;
                data_idx++;
            }ind_end;
        }
    }ind_end;
}

static void Render() {
    ClearScreen();
    ind_beg(acr_tui::_db_widget_curs, w, acr_tui::_db) {
        if (!w.visible) continue;
        if (w.type == "label") RenderLabel(w);
        else if (w.type == "statusbar") RenderStatusBar(w);
        else if (w.type == "table") RenderTable(w);
        else if (w.type == "tree") RenderTree(w);
    }ind_end;
}

// ============================================================================
// Terminal raw mode
// ============================================================================

static struct termios orig_termios;
static void DisableRawMode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    ClearScreen();
}
static void EnableRawMode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(DisableRawMode);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

// ============================================================================
// Main: message-driven loop
//   1. Render current state
//   2. Read keypress
//   3. Translate to message
//   4. Dispatch message (pure state update)
//   5. Repeat
// ============================================================================

void acr_tui::Main() {
    // Build focus list
    ind_beg(acr_tui::_db_widget_curs, w, acr_tui::_db) {
        if (w.selection != "none" && n_focus < 16) {
            focus_widgets[n_focus++] = algo::strptr(w.widget);
        }
    }ind_end;

    expanded[0] = true;  // expand first namespace
    EnableRawMode();

    while (running) {
        Render();

        char ch;
        if (read(STDIN_FILENO, &ch, 1) != 1) break;

        UiMsg msg = TranslateKey(ch);
        if (msg.type != 0) {
            DispatchMsg(msg);
        }
    }
}
