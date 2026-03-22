// acr_web.cpp -- Browser frontend for acr_browse
// Same logic as acr_tui but rendered via embind → JavaScript DOM
// Compile with: bin/wasm-build-web

#include "include/acr_browse.h"
#include <emscripten/bind.h>

using namespace emscripten;

// OS stubs (DB instances defined in acr_tui_gen.cpp)
void algo::FatalErrorExit(const char *msg) { abort(); }
void algo_lib::fildes_Cleanup(algo_lib::FLockfile&) {}
void algo_lib::giveup_time_Step() {}
bool algo::FileQ(algo::strptr) { return false; }
bool algo::DirectoryQ(algo::strptr) { return false; }

// FileToString — in WASM, data is loaded via loadSsimData, not from disk
const tempstr algo::FileToString(const algo::strptr&, algo::FileFlags) { return tempstr(); }
bool algo::WriteFile(algo::Fildes, u8*, int) { return false; }

static std::string to_str(algo::strptr s) {
    return std::string(s.elems, s.n_elems);
}

// ============================================================================
// Init
// ============================================================================

static void WebInit() {
    algo_lib::FDb_Init();
    acr_tui::FDb_Init();
}

// Load ssim data line by line (called from JS after fetching ssim files)
static void WebLoadSsimLine(const std::string& line) {
    LoadRecord(algo::strptr((char*)line.c_str(), line.size()));
}

// Load ui.* ssim for the framework tables (finput equivalent)
static bool WebLoadUiRecord(const std::string& line) {
    algo::strptr str((char*)line.c_str(), line.size());
    return acr_tui::InsertStrptrMaybe(str);
}

// After all data loaded, initialize paths
static void WebInitPaths() {
    InitPaths();
    RefreshItems();
}

// ============================================================================
// Navigation API — JS calls these, they dispatch messages
// ============================================================================

static void WebNavigate(int direction) {
    UiMsg msg;
    msg.type = MSG_NAVIGATE;
    msg.direction = direction;
    DispatchMsg(msg, 30);
}

static void WebActivate() {
    UiMsg msg;
    msg.type = MSG_ACTIVATE;
    DispatchMsg(msg, 30);
    RefreshItems();
}

static void WebCancel() {
    UiMsg msg;
    msg.type = MSG_CANCEL;
    DispatchMsg(msg, 30);
}

static void WebSwitchPath(int idx) {
    UiMsg msg;
    msg.type = MSG_SWITCH_PATH;
    msg.path_idx = idx;
    DispatchMsg(msg, 30);
}

static void WebToggleView() {
    view_mode = (ViewMode)((view_mode + 1) % 3);
}

// ============================================================================
// State query API — JS reads these to render DOM
// ============================================================================

static int WebGetItemCount() { return g_nitems; }
static int WebGetSelectedRow() { return selected_row; }
static int WebGetScrollOffset() { return scroll_offset; }
static int WebGetCurrentLevel() { return current_level; }
static int WebGetViewMode() { return (int)view_mode; }
static int WebGetPathCount() { return n_paths; }
static int WebGetRecordCount() { return g_nrecs; }

static std::string WebGetBreadcrumb() {
    algo::cstring c = GetBreadcrumb();
    return to_str(c);
}

static std::string WebGetCurrentPath() {
    return to_str(current_path);
}

static std::string WebGetPathName(int idx) {
    if (idx >= 0 && idx < n_paths) return to_str(all_paths[idx]);
    return "";
}

static val WebGetItem(int idx) {
    if (idx < 0 || idx >= g_nitems) return val::null();
    ViewItem& item = g_items[idx];
    val obj = val::object();
    obj.set("label", to_str(item.label));
    obj.set("detail", to_str(item.detail));
    obj.set("key", to_str(item.key));
    obj.set("hasChildren", item.has_children);
    return obj;
}

static val WebGetItems() {
    val arr = val::array();
    for (int i = 0; i < g_nitems; i++) {
        arr.set(i, WebGetItem(i));
    }
    return arr;
}

// ============================================================================
// Embind registration
// ============================================================================

EMSCRIPTEN_BINDINGS(acr_web) {
    function("init", &WebInit);
    function("loadSsimLine", &WebLoadSsimLine);
    function("loadUiRecord", &WebLoadUiRecord);
    function("initPaths", &WebInitPaths);

    function("navigate", &WebNavigate);
    function("activate", &WebActivate);
    function("cancel", &WebCancel);
    function("switchPath", &WebSwitchPath);
    function("toggleView", &WebToggleView);

    function("getItemCount", &WebGetItemCount);
    function("getSelectedRow", &WebGetSelectedRow);
    function("getScrollOffset", &WebGetScrollOffset);
    function("getCurrentLevel", &WebGetCurrentLevel);
    function("getViewMode", &WebGetViewMode);
    function("getPathCount", &WebGetPathCount);
    function("getRecordCount", &WebGetRecordCount);
    function("getBreadcrumb", &WebGetBreadcrumb);
    function("getCurrentPath", &WebGetCurrentPath);
    function("getPathName", &WebGetPathName);
    function("getItem", &WebGetItem);
    function("getItems", &WebGetItems);
}
