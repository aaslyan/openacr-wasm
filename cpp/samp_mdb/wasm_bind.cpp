// wasm_bind.cpp -- Emscripten embind bindings for samp_mdb
// Exposes samp_mdb in-memory database to JavaScript
#include "include/algo.h"
#include "include/samp_mdb.h"
#include <emscripten/bind.h>
#include <iostream>

using namespace emscripten;

// Global database instances (normally defined by each executable's main)
algo_lib::FDb algo_lib::_db;
samp_mdb::FDb samp_mdb::_db;

// Stubs for symbols not available on WASM
void algo::FatalErrorExit(const char *msg) {
    std::cerr << "FATAL: " << msg << std::endl;
    abort();
}

void algo_lib::fildes_Cleanup(algo_lib::FLockfile&) {
}

void algo_lib::giveup_time_Step() {
}

const tempstr algo::FileToString(const algo::strptr& fname, algo::FileFlags flags) {
    (void)fname; (void)flags;
    return tempstr();
}

bool algo::WriteFile(algo::Fildes fd, u8* buf, int n) {
    (void)fd; (void)buf; (void)n;
    return false;
}

// Helper: convert algo::strptr to std::string for JS
static std::string to_str(algo::strptr s) {
    return std::string(s.elems, s.n_elems);
}

// --- Init ---
static void WasmInit() {
    algo_lib::FDb_Init();
    samp_mdb::FDb_Init();
    samp_mdb::user_Reserve(64);
    samp_mdb::project_Reserve(64);
    samp_mdb::task_Reserve(64);
    samp_mdb::comment_Reserve(64);
    samp_mdb::quote_Reserve(4096);
}

// --- Insert from ssim string using AMC-generated parsers ---
static bool WasmInsertSsim(const std::string& ssim_line) {
    algo::strptr str((char*)ssim_line.c_str(), ssim_line.size());
    // Determine type from the first token
    algo::strptr type_tag = algo::GetTypeTag(str);

    if (type_tag == "samp_mdb.User") {
        samp_mdb::User* u = samp_mdb::user_AllocMaybe();
        if (!u) return false;
        if (!samp_mdb::User_ReadStrptrMaybe(*u, str)) {
            samp_mdb::user_Delete(*u);
            return false;
        }
        if (!samp_mdb::user_XrefMaybe(*u)) {
            samp_mdb::user_Delete(*u);
            return false;
        }
        return true;
    }
    if (type_tag == "samp_mdb.Project") {
        samp_mdb::Project* p = samp_mdb::project_AllocMaybe();
        if (!p) return false;
        if (!samp_mdb::Project_ReadStrptrMaybe(*p, str)) {
            samp_mdb::project_Delete(*p);
            return false;
        }
        if (!samp_mdb::project_XrefMaybe(*p)) {
            samp_mdb::project_Delete(*p);
            return false;
        }
        return true;
    }
    if (type_tag == "samp_mdb.Task") {
        samp_mdb::Task* t = samp_mdb::task_AllocMaybe();
        if (!t) return false;
        if (!samp_mdb::Task_ReadStrptrMaybe(*t, str)) {
            samp_mdb::task_Delete(*t);
            return false;
        }
        if (!samp_mdb::task_XrefMaybe(*t)) {
            samp_mdb::task_Delete(*t);
            return false;
        }
        return true;
    }
    if (type_tag == "samp_mdb.Quote") {
        samp_mdb::Quote* q = samp_mdb::quote_AllocMaybe();
        if (!q) return false;
        if (!samp_mdb::Quote_ReadStrptrMaybe(*q, str)) {
            samp_mdb::quote_Delete(*q);
            return false;
        }
        if (!samp_mdb::quote_XrefMaybe(*q)) {
            samp_mdb::quote_Delete(*q);
            return false;
        }
        return true;
    }
    if (type_tag == "samp_mdb.Comment") {
        samp_mdb::Comment* c = samp_mdb::comment_AllocMaybe();
        if (!c) return false;
        if (!samp_mdb::Comment_ReadStrptrMaybe(*c, str)) {
            samp_mdb::comment_Delete(*c);
            return false;
        }
        if (!samp_mdb::comment_XrefMaybe(*c)) {
            samp_mdb::comment_Delete(*c);
            return false;
        }
        return true;
    }
    return false;
}

// --- Serialize record to ssim using AMC-generated Print ---
static std::string WasmToSsim(const std::string& type, const std::string& key) {
    algo::strptr k((char*)key.c_str(), key.size());
    algo::cstring out;
    if (type == "user") {
        samp_mdb::User* u = samp_mdb::ind_user_Find(k);
        if (u) samp_mdb::User_Print(*u, out);
    } else if (type == "project") {
        samp_mdb::Project* p = samp_mdb::ind_project_Find(k);
        if (p) samp_mdb::Project_Print(*p, out);
    } else if (type == "task") {
        samp_mdb::Task* t = samp_mdb::ind_task_Find(k);
        if (t) samp_mdb::Task_Print(*t, out);
    } else if (type == "quote") {
        // Quote lookup by key not supported (no hash index), use list
    }
    return to_str(out);
}

// --- User accessors ---
static val UserFind(const std::string& key) {
    samp_mdb::User* u = samp_mdb::ind_user_Find(algo::strptr((char*)key.c_str(), key.size()));
    if (!u) return val::null();
    val obj = val::object();
    obj.set("user", to_str(u->user));
    obj.set("email", to_str(u->email));
    obj.set("role", to_str(u->role));
    return obj;
}

static val UserList() {
    val arr = val::array();
    int i = 0;
    for (samp_mdb::User* u = samp_mdb::zd_user_First(); u; u = samp_mdb::zd_user_Next(*u)) {
        val obj = val::object();
        obj.set("user", to_str(u->user));
        obj.set("email", to_str(u->email));
        obj.set("role", to_str(u->role));
        arr.set(i++, obj);
    }
    return arr;
}

static int UserCount() {
    return samp_mdb::zd_user_N();
}

// --- Project accessors ---
static val ProjectList() {
    val arr = val::array();
    int i = 0;
    for (samp_mdb::Project* p = samp_mdb::zd_project_First(); p; p = samp_mdb::zd_project_Next(*p)) {
        val obj = val::object();
        obj.set("project", to_str(p->project));
        obj.set("description", to_str(p->description));
        obj.set("status", to_str(p->status));
        arr.set(i++, obj);
    }
    return arr;
}

// --- Task accessors ---
static val TaskList() {
    val arr = val::array();
    int i = 0;
    for (samp_mdb::Task* t = samp_mdb::zd_task_First(); t; t = samp_mdb::zd_task_Next(*t)) {
        val obj = val::object();
        obj.set("task", to_str(t->task));
        obj.set("title", to_str(t->title));
        obj.set("priority", (int)t->priority);
        obj.set("status", to_str(t->status));
        arr.set(i++, obj);
    }
    return arr;
}

// --- Quote accessors ---
static val QuoteList() {
    val arr = val::array();
    int i = 0;
    for (samp_mdb::Quote* q = samp_mdb::zd_quote_First(); q; q = samp_mdb::zd_quote_Next(*q)) {
        val obj = val::object();
        obj.set("quote", to_str(q->quote));
        obj.set("symbol", to_str(q->symbol));
        obj.set("price", q->price);
        obj.set("ts", (double)q->ts);
        arr.set(i++, obj);
    }
    return arr;
}

static int QuoteCount() {
    return samp_mdb::zd_quote_N();
}

// Prune quotes older than cutoff_ms (unix timestamp in ms)
static int QuotePrune(double cutoff_ms) {
    int removed = 0;
    i64 cutoff = (i64)cutoff_ms;
    samp_mdb::Quote* q = samp_mdb::zd_quote_First();
    while (q) {
        if (q->ts >= cutoff) break;
        samp_mdb::Quote* next = samp_mdb::zd_quote_Next(*q);
        samp_mdb::quote_Delete(*q);
        q = next;
        removed++;
    }
    return removed;
}

EMSCRIPTEN_BINDINGS(samp_mdb_wasm) {
    function("init", &WasmInit);
    function("insertSsim", &WasmInsertSsim);
    function("toSsim", &WasmToSsim);
    function("userFind", &UserFind);
    function("userList", &UserList);
    function("userCount", &UserCount);
    function("projectList", &ProjectList);
    function("taskList", &TaskList);
    function("quoteList", &QuoteList);
    function("quoteCount", &QuoteCount);
    function("quotePrune", &QuotePrune);
}
