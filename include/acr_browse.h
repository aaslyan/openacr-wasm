// acr_browse.h -- Shared logic for acr_tui (terminal) and acr_web (browser)
// Generic tuple store, DataPath navigation, message dispatch
// Platform-specific code (rendering, input) is NOT here
#pragma once
#include "include/algo.h"
#include "include/acr_tui.h"
#include <dirent.h>

// ============================================================================
// Generic record store
// ============================================================================

struct GRec {
    algo::cstring ctype_tag;
    algo::cstring pkey;
    algo::Tuple   tuple;
    GRec* next;
};

static const int MAX_CTYPES = 256;
static const int MAX_RECS = 100000;
static GRec g_recs[MAX_RECS];
static int g_nrecs = 0;

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

static algo::strptr TupleGetAttr(const algo::Tuple& t, algo::strptr name) {
    for (u32 i = 0; i < t.attrs_n; i++) {
        if (algo::strptr(t.attrs_elems[i].name) == name) {
            return algo::strptr(t.attrs_elems[i].value);
        }
    }
    if (algo::strptr(t.head.name) == name) return algo::strptr(t.head.value);
    return algo::strptr();
}

// ============================================================================
// Navigation state
// ============================================================================

static int current_level = 0;
static int selected_row = 0;
static int scroll_offset = 0;
static algo::Smallstr100 breadcrumb[16];
static algo::Smallstr50 current_path;
static algo::Smallstr50 all_paths[8];
static int n_paths = 0;
static bool choosing_branch = false;
static algo::Smallstr50 active_step;

enum ViewMode { VIEW_LIST = 0, VIEW_GRAPH = 1, VIEW_RAW = 2 };
static ViewMode view_mode = VIEW_LIST;
static algo::cstring graph_output;
static int graph_scroll = 0;

struct BranchChoice {
    algo::Smallstr50 step_key;
    algo::Smallstr100 source_ctype;
    int item_count;
};
static BranchChoice g_branches[16];
static int g_nbranches = 0;

struct ViewItem {
    algo::cstring label;
    algo::cstring detail;
    algo::cstring key;
    bool has_children;
};
static ViewItem g_items[8192];
static int g_nitems = 0;

// ============================================================================
// DataPath navigation
// ============================================================================

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

    acr_tui::FDataStep* step = NULL;
    if (ch_N(active_step)) {
        ind_beg(acr_tui::_db_data_step_curs, ds, acr_tui::_db) {
            if (ds.data_step == active_step) { step = &ds; break; }
        }ind_end;
    }
    if (!step) {
        ind_beg(acr_tui::_db_data_step_curs, ds, acr_tui::_db) {
            if (ds.p_path == current_path && ds.level == current_level) { step = &ds; break; }
        }ind_end;
    }
    if (!step) return;

    int next_count = 0;
    ind_beg(acr_tui::_db_data_step_curs, ds, acr_tui::_db) {
        if (ds.p_path == current_path && ds.level == current_level + 1) next_count++;
    }ind_end;

    algo::strptr parent_key;
    if (current_level > 0) parent_key = algo::strptr(breadcrumb[current_level - 1]);

    CTypeIndex* ct = FindCType(step->source_ctype);
    if (!ct) return;

    algo::strptr link = step->link_field;
    algo::strptr label_name = step->label_field;
    algo::strptr detail_name = step->detail_field;

    for (GRec* rec = ct->head; rec && g_nitems < 8192; rec = rec->next) {
        if (!ch_N(rec->pkey)) continue;
        if (ch_N(link) && ch_N(parent_key)) {
            algo::strptr prefix = algo::Pathcomp(rec->pkey, link);
            if (prefix != parent_key) continue;
        }

        ViewItem& item = g_items[g_nitems];
        item.label = TupleGetAttr(rec->tuple, label_name);
        if (!ch_N(item.label)) item.label = rec->pkey;
        if (ch_N(link) && ch_N(parent_key)) {
            (void)item.label;
            if (link == ".LL") {
                algo::strptr local = algo::Pathcomp(rec->pkey, ".LR");
                if (ch_N(local)) item.label = local;
            } else if (link == "/LL") {
                algo::strptr local = algo::Pathcomp(rec->pkey, "/LR");
                if (ch_N(local)) item.label = local;
            } else if (link == ".RL") {
                algo::strptr local = algo::Pathcomp(rec->pkey, ".RR");
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
// Message dispatch — pure state updates, no I/O
// ============================================================================

enum { MSG_NAVIGATE = 1, MSG_ACTIVATE = 2, MSG_CANCEL = 3, MSG_QUIT = 4,
       MSG_SWITCH_PATH = 5, MSG_TOGGLE_VIEW = 6 };

struct UiMsg {
    u32 type;
    i32 direction;
    i32 path_idx;
    UiMsg() : type(0), direction(0), path_idx(0) {}
};

static bool running = true;

static void DispatchMsg(const UiMsg& msg, int term_rows_visible = 19) {
    switch (msg.type) {
        case MSG_QUIT: running = false; break;
        case MSG_NAVIGATE: {
            int new_sel = selected_row + msg.direction;
            if (new_sel >= 0 && new_sel < g_nitems) selected_row = new_sel;
            if (selected_row < scroll_offset) scroll_offset = selected_row;
            if (selected_row >= scroll_offset + term_rows_visible)
                scroll_offset = selected_row - term_rows_visible + 1;
            break;
        }
        case MSG_ACTIVATE:
            if (choosing_branch) {
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
                int branch_count = 0;
                ind_beg(acr_tui::_db_data_step_curs, ds, acr_tui::_db) {
                    if (ds.p_path == current_path && ds.level == current_level) branch_count++;
                }ind_end;
                if (branch_count > 1) {
                    choosing_branch = true;
                    active_step = algo::Smallstr50();
                } else {
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
                choosing_branch = false;
                current_level--;
                selected_row = 0;
                scroll_offset = 0;
                RefreshItems();
            } else if (current_level > 0) {
                current_level--;
                selected_row = 0;
                scroll_offset = 0;
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
                ind_beg(acr_tui::_db_data_step_curs, ds, acr_tui::_db) {
                    if (ds.p_path == current_path && ds.level == 0) { active_step = ds.data_step; break; }
                }ind_end;
                RefreshItems();
            }
            break;
        case MSG_TOGGLE_VIEW:
            view_mode = (ViewMode)((view_mode + 1) % 3);
            break;
    }
}

// Build breadcrumb string
static algo::cstring GetBreadcrumb() {
    algo::cstring crumb;
    crumb << current_path;
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
    return crumb;
}

// Initialize paths from loaded DataPath records
static void InitPaths() {
    ind_beg(acr_tui::_db_data_path_curs, dp, acr_tui::_db) {
        if (n_paths < 8) all_paths[n_paths++] = dp.data_path;
    }ind_end;
    if (n_paths > 0) current_path = all_paths[0];
    ind_beg(acr_tui::_db_data_step_curs, ds, acr_tui::_db) {
        if (ds.p_path == current_path && ds.level == 0) { active_step = ds.data_step; break; }
    }ind_end;
}
