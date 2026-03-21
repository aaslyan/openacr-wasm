// Copyright (C) 2026 AlgoRND
//
// License: GPL
// Target: acr_tui (exe) -- Terminal UI browser for OpenACR schemas
// Exceptions: yes
// Source: cpp/acr_tui/acr_tui.cpp

#include "include/algo.h"
#include "include/acr_tui.h"
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>

// ANSI escape helpers
static void ClearScreen() { (void)!write(1, "\x1b[2J\x1b[H", 7); }
static void MoveTo(int row, int col) {
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "\x1b[%d;%dH", row+1, col+1);
    (void)!write(1, buf, n);
}
static void SetColor(algo::strptr fg, algo::strptr bg, bool bold) {
    algo::cstring s;
    if (bold) s << "\x1b[1m";
    if (fg == "black")   s << "\x1b[30m"; else if (fg == "red")     s << "\x1b[31m";
    else if (fg == "green")   s << "\x1b[32m"; else if (fg == "yellow")  s << "\x1b[33m";
    else if (fg == "blue")    s << "\x1b[34m"; else if (fg == "magenta") s << "\x1b[35m";
    else if (fg == "cyan")    s << "\x1b[36m"; else if (fg == "white")   s << "\x1b[37m";
    if (bg == "blue")    s << "\x1b[44m"; else if (bg == "cyan")    s << "\x1b[46m";
    else if (bg == "black")   s << "\x1b[40m"; else if (bg == "red")     s << "\x1b[41m";
    else if (bg == "green")   s << "\x1b[42m"; else if (bg == "yellow")  s << "\x1b[43m";
    else if (bg == "magenta") s << "\x1b[45m"; else if (bg == "white")   s << "\x1b[47m";
    (void)!write(1, s.ch_elems, s.ch_n);
}
static void ResetColor() { (void)!write(1, "\x1b[0m", 4); }

// Box drawing
static const char* BOX_TL = "\xe2\x94\x8c";
static const char* BOX_TR = "\xe2\x94\x90";
static const char* BOX_BL = "\xe2\x94\x94";
static const char* BOX_BR = "\xe2\x94\x98";
static const char* BOX_H  = "\xe2\x94\x80";
static const char* BOX_V  = "\xe2\x94\x82";
static const char* TREE_BRANCH __attribute__((unused)) = "\xe2\x94\x9c";
static const char* TREE_LAST   __attribute__((unused)) = "\xe2\x94\x94";
static const char* TREE_DASH   __attribute__((unused)) = "\xe2\x94\x80";

// Terminal raw mode
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

// Runtime state
static int focus_idx = 0;
static algo::cstring focus_widgets[16];
static int n_focus = 0;
static int scroll_offset[16] __attribute__((unused)) = {};
static int selected[16] = {};
static bool expanded[256] = {};  // track expanded tree nodes by index

static void PutStr(int row, int col, algo::strptr s, int maxw) {
    MoveTo(row, col);
    for (int i = 0; i < maxw; i++) {
        char c = i < s.n_elems ? s.elems[i] : ' ';
        (void)!write(1, &c, 1);
    }
}

static void DrawBorder(int r, int c, int w, int h, algo::strptr title) {
    MoveTo(r, c); (void)!write(1, BOX_TL, 3);
    for (int i = 1; i < w-1; i++) (void)!write(1, BOX_H, 3);
    (void)!write(1, BOX_TR, 3);
    if (ch_N(title)) {
        MoveTo(r, c+2);
        (void)!write(1, " ", 1);
        (void)!write(1, title.elems, title.n_elems);
        (void)!write(1, " ", 1);
    }
    for (int i = 1; i < h-1; i++) {
        MoveTo(r+i, c); (void)!write(1, BOX_V, 3);
        MoveTo(r+i, c+w-1); (void)!write(1, BOX_V, 3);
    }
    MoveTo(r+h-1, c); (void)!write(1, BOX_BL, 3);
    for (int i = 1; i < w-1; i++) (void)!write(1, BOX_H, 3);
    (void)!write(1, BOX_BR, 3);
}

static acr_tui::FStyle* FindStyle(algo::strptr key) {
    return acr_tui::ind_style_Find(key);
}

static void ApplyStyle(acr_tui::FStyle* style) {
    if (style) {
        SetColor(style->fg, style->bg, style->bold);
    }
}

static void DrawWidget(acr_tui::FWidget& w) {
    if (!w.visible) return;
    acr_tui::FStyle* style = FindStyle(w.p_style);
    int r = w.row, c = w.col, wi = w.w, h = w.h;
    bool is_focused = n_focus > 0 && focus_widgets[focus_idx] == w.widget;

    if (w.type == "label") {
        if (style) ApplyStyle(style);
        PutStr(r, c, w.text, wi);
        ResetColor();
    }
    else if (w.type == "statusbar") {
        if (style) ApplyStyle(style);
        algo::cstring text;
        text << w.text;
        if (n_focus > 0) {
            text << "  [" << focus_widgets[focus_idx] << "]";
        }
        PutStr(r, c, text, wi);
        ResetColor();
    }
    else if (w.type == "table") {
        if (style) ApplyStyle(style);
        DrawBorder(r, c, wi, h, w.title);

        // Find binding
        int row = r + 1;
        ind_beg(acr_tui::_db_binding_curs, b, acr_tui::_db) {
            if (b.p_widget != w.widget) continue;

            // Find table config
            acr_tui::FTableCfg* cfg = NULL;
            ind_beg(acr_tui::_db_table_cfg_curs, tc, acr_tui::_db) {
                if (tc.p_widget == w.widget) { cfg = &tc; break; }
            }ind_end;

            // Collect columns for this widget
            acr_tui::FColumn* cols[32];
            int ncols = 0;
            ind_beg(acr_tui::_db_column_curs, col, acr_tui::_db) {
                if (col.p_widget == w.widget && ncols < 32) {
                    cols[ncols++] = &col;
                }
            }ind_end;

            // Header
            if (cfg && cfg->header && ncols > 0) {
                if (style) ApplyStyle(style);
                (void)!write(1, "\x1b[1m\x1b[4m", 7);  // bold + underline
                int coloff = c + 1;
                for (int ci = 0; ci < ncols; ci++) {
                    PutStr(row, coloff, cols[ci]->title, cols[ci]->w);
                    coloff += cols[ci]->w + 1;
                }
                ResetColor();
                row++;
            }

            // Data rows — iterate the bound data
            // For dmmeta browsing, source_ctype tells us which table
            int data_idx = 0;
            if (b.source_ctype == "dmmeta.Ns") {
                ind_beg(acr_tui::_db_ns_curs, ns, acr_tui::_db) {
                    if (row >= r + h - 1) break;
                    bool sel = is_focused && data_idx == selected[focus_idx];
                    if (sel) { (void)!write(1, "\x1b[30m\x1b[46m", 10); }
                    else if (style) { ApplyStyle(style); }
                    int coloff = c + 1;
                    for (int ci = 0; ci < ncols; ci++) {
                        algo::strptr val;
                        if (cols[ci]->field == "ns") val = ns.ns;
                        else if (cols[ci]->field == "nstype") val = ns.nstype;
                        else if (cols[ci]->field == "comment") val = ns.comment;
                        PutStr(row, coloff, val, cols[ci]->w);
                        coloff += cols[ci]->w + 1;
                    }
                    ResetColor();
                    row++;
                    data_idx++;
                }ind_end;
            } else if (b.source_ctype == "dmmeta.Field") {
                ind_beg(acr_tui::_db_field_curs, fld, acr_tui::_db) {
                    if (row >= r + h - 1) break;
                    bool sel = is_focused && data_idx == selected[focus_idx];
                    if (sel) { (void)!write(1, "\x1b[30m\x1b[46m", 10); }
                    else if (style) { ApplyStyle(style); }
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
            break;
        }ind_end;
    }
    else if (w.type == "tree") {
        if (style) ApplyStyle(style);
        DrawBorder(r, c, wi, h, w.title);

        int row = r + 1;
        int data_idx = 0;

        // Tree: ns -> ctype -> field
        ind_beg(acr_tui::_db_ns_curs, ns, acr_tui::_db) {
            if (row >= r + h - 1) break;
            bool is_exp = expanded[data_idx % 256];
            bool sel = is_focused && data_idx == selected[focus_idx];
            if (sel) { (void)!write(1, "\x1b[30m\x1b[46m", 10); }
            else if (style) { ApplyStyle(style); }

            algo::cstring line;
            line << (is_exp ? "\xe2\x96\xbc " : "\xe2\x96\xb6 ") << ns.ns;
            if (ch_N(ns.comment)) line << " - " << ns.comment;
            PutStr(row, c+1, line, wi-2);
            ResetColor();
            row++;
            (void)data_idx; // ns_idx reserved for future use
            data_idx++;

            if (is_exp) {
                // Show ctypes for this namespace
                ind_beg(acr_tui::_db_ctype_curs, ct, acr_tui::_db) {
                    if (row >= r + h - 1) break;
                    // Check if ctype belongs to this ns
                    algo::strptr ct_ns = algo::Pathcomp(ct.ctype, ".LL");
                    if (ct_ns != ns.ns) continue;
                    algo::strptr ct_name = algo::Pathcomp(ct.ctype, ".LR");

                    bool ct_sel = is_focused && data_idx == selected[focus_idx];
                    if (ct_sel) { (void)!write(1, "\x1b[30m\x1b[46m", 10); }
                    else if (style) { ApplyStyle(style); }

                    algo::cstring cline;
                    cline << "  " << TREE_BRANCH << TREE_DASH << " " << ct_name;
                    if (ch_N(ct.comment)) cline << " - " << ct.comment;
                    PutStr(row, c+1, cline, wi-2);
                    ResetColor();
                    row++;
                    data_idx++;
                }ind_end;
            }
        }ind_end;
    }
}

void acr_tui::Main() {
    // Build focus list from selectable widgets
    ind_beg(acr_tui::_db_widget_curs, w, acr_tui::_db) {
        if (w.selection != "none" && n_focus < 16) {
            focus_widgets[n_focus++] = algo::strptr(w.widget);
        }
    }ind_end;

    // Initialize expanded state (expand first few namespaces)
    expanded[0] = true;

    EnableRawMode();

    // Main loop
    bool running = true;
    while (running) {
        ClearScreen();

        // Draw all widgets
        ind_beg(acr_tui::_db_widget_curs, w, acr_tui::_db) {
            DrawWidget(w);
        }ind_end;

        // Read key
        char ch;
        if (read(STDIN_FILENO, &ch, 1) != 1) break;

        // Match against key maps
        algo::cstring focused_widget;
        if (n_focus > 0) focused_widget = focus_widgets[focus_idx];

        ind_beg(acr_tui::_db_key_map_curs, km, acr_tui::_db) {
            bool match = false;
            if (km.key == "q" && ch == 'q') match = true;
            else if (km.key == "j" && ch == 'j') match = true;
            else if (km.key == "k" && ch == 'k') match = true;
            else if (km.key == "Tab" && ch == '\t') match = true;
            else if (km.key == "Enter" && ch == '\r') match = true;

            if (!match) continue;
            // Check widget scope
            if (ch_N(km.p_widget) > 0 && km.p_widget != focused_widget) continue;

            if (km.action == "quit") {
                running = false;
            } else if (km.action == "focus_next") {
                if (n_focus > 0) focus_idx = (focus_idx + 1) % n_focus;
            } else if (km.action == "navigate_next") {
                selected[focus_idx]++;
            } else if (km.action == "navigate_prev") {
                if (selected[focus_idx] > 0) selected[focus_idx]--;
            } else if (km.action == "toggle_expand") {
                int idx = selected[focus_idx];
                expanded[idx % 256] = !expanded[idx % 256];
            }
        }ind_end;
    }
}
