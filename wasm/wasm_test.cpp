// wasm_test.cpp -- Comprehensive WASM tests for AMC-generated data structures
// Tests: Tpool, Thash, Llist, Bheap, Ptrary, Pkey, cascade delete,
//        ssim round-trip, field types (i32, double, i64, Smallstr)
// Compile with bin/wasm-test, run in Node.js
#include "include/algo.h"
#include "include/samp_mdb.h"
#include <emscripten/bind.h>
#include <sstream>
#include <cmath>

using namespace emscripten;

// Global DB instances (Global reftype — must be defined by the executable)
algo_lib::FDb algo_lib::_db;
samp_mdb::FDb samp_mdb::_db;

// OS stubs — same as wasm_bind.cpp
void algo::FatalErrorExit(const char *msg) {
    fprintf(stderr, "FATAL: %s\n", msg);
    abort();
}
void algo_lib::fildes_Cleanup(algo_lib::FLockfile&) {}
void algo_lib::giveup_time_Step() {}
const tempstr algo::FileToString(const algo::strptr&, algo::FileFlags) { return tempstr(); }
bool algo::WriteFile(algo::Fildes, u8*, int) { return false; }

static std::string to_str(algo::strptr s) { return std::string(s.elems, s.n_elems); }

// Test framework
static int g_pass, g_fail, g_total;
static std::ostringstream g_log;

static void check(bool cond, const char* name, const char* expr, int line) {
    g_total++;
    if (cond) {
        g_pass++;
    } else {
        g_fail++;
        g_log << "  FAIL line " << line << ": " << expr << "\n";
    }
}
#define T(cond) check((cond), test_name, #cond, __LINE__)

static void test_begin(const char* name) {
    g_log << name << ": ";
}
static void test_end(const char* name) {
    g_log << "OK\n";
}

#define BEGIN(name) { const char* test_name = name; int _f0 = g_fail; test_begin(name);
#define END   if (g_fail == _f0) test_end(test_name); else g_log << "\n"; }

// Insert helper — same pattern as wasm_bind.cpp
static bool ins(const char* ssim) {
    algo::strptr str((char*)ssim, strlen(ssim));
    algo::strptr type_tag = algo::GetTypeTag(str);
    if (type_tag == "samp_mdb.User") {
        samp_mdb::User* r = samp_mdb::user_AllocMaybe();
        if (!r) return false;
        if (!samp_mdb::User_ReadStrptrMaybe(*r, str)) { samp_mdb::user_Delete(*r); return false; }
        if (!samp_mdb::user_XrefMaybe(*r)) { samp_mdb::user_Delete(*r); return false; }
        return true;
    }
    if (type_tag == "samp_mdb.Project") {
        samp_mdb::Project* r = samp_mdb::project_AllocMaybe();
        if (!r) return false;
        if (!samp_mdb::Project_ReadStrptrMaybe(*r, str)) { samp_mdb::project_Delete(*r); return false; }
        if (!samp_mdb::project_XrefMaybe(*r)) { samp_mdb::project_Delete(*r); return false; }
        return true;
    }
    if (type_tag == "samp_mdb.Task") {
        samp_mdb::Task* r = samp_mdb::task_AllocMaybe();
        if (!r) return false;
        if (!samp_mdb::Task_ReadStrptrMaybe(*r, str)) { samp_mdb::task_Delete(*r); return false; }
        if (!samp_mdb::task_XrefMaybe(*r)) { samp_mdb::task_Delete(*r); return false; }
        return true;
    }
    if (type_tag == "samp_mdb.Comment") {
        samp_mdb::Comment* r = samp_mdb::comment_AllocMaybe();
        if (!r) return false;
        if (!samp_mdb::Comment_ReadStrptrMaybe(*r, str)) { samp_mdb::comment_Delete(*r); return false; }
        if (!samp_mdb::comment_XrefMaybe(*r)) { samp_mdb::comment_Delete(*r); return false; }
        return true;
    }
    if (type_tag == "samp_mdb.Quote") {
        samp_mdb::Quote* r = samp_mdb::quote_AllocMaybe();
        if (!r) return false;
        if (!samp_mdb::Quote_ReadStrptrMaybe(*r, str)) { samp_mdb::quote_Delete(*r); return false; }
        if (!samp_mdb::quote_XrefMaybe(*r)) { samp_mdb::quote_Delete(*r); return false; }
        return true;
    }
    return false;
}

// =========================================================================
// TPOOL TESTS
// =========================================================================

static void test_tpool() {
    BEGIN("Tpool.AllocAndFields")
        samp_mdb::User* u = samp_mdb::user_AllocMaybe();
        T(u != nullptr);
        algo::strptr s("samp_mdb.User  user:tp1  email:a@b.com  role:dev");
        T(samp_mdb::User_ReadStrptrMaybe(*u, s));
        T(samp_mdb::user_XrefMaybe(*u));
        T(to_str(u->user) == "tp1");
        T(to_str(u->email) == "a@b.com");
        T(to_str(u->role) == "dev");
    END

    BEGIN("Tpool.Reserve")
        samp_mdb::user_Reserve(512);
        samp_mdb::User* u = samp_mdb::user_AllocMaybe();
        T(u != nullptr);
        algo::strptr s("samp_mdb.User  user:tp_reserve  email:r@r  role:r");
        samp_mdb::User_ReadStrptrMaybe(*u, s);
        samp_mdb::user_XrefMaybe(*u);
    END

    BEGIN("Tpool.DeleteAndRealloc")
        ins("samp_mdb.User  user:tp_del  email:d@d  role:d");
        samp_mdb::User* u = samp_mdb::ind_user_Find(algo::strptr("tp_del"));
        T(u != nullptr);
        int n_before = samp_mdb::zd_user_N();
        samp_mdb::user_Delete(*u);
        T(samp_mdb::ind_user_Find(algo::strptr("tp_del")) == nullptr);
        T(samp_mdb::zd_user_N() == n_before - 1);
        // Verify pool reuse — alloc should succeed using freed slot
        ins("samp_mdb.User  user:tp_del2  email:d2@d  role:d");
        T(samp_mdb::ind_user_Find(algo::strptr("tp_del2")) != nullptr);
    END

    BEGIN("Tpool.MultipleAlloc")
        int n0 = samp_mdb::zd_user_N();
        for (int i = 0; i < 100; i++) {
            char buf[128];
            snprintf(buf, sizeof(buf), "samp_mdb.User  user:multi_%d  email:m%d@x  role:test", i, i);
            ins(buf);
        }
        T(samp_mdb::zd_user_N() == n0 + 100);
    END
}

// =========================================================================
// THASH TESTS
// =========================================================================

static void test_thash() {
    BEGIN("Thash.Find")
        ins("samp_mdb.User  user:hash1  email:h@1  role:dev");
        samp_mdb::User* u = samp_mdb::ind_user_Find(algo::strptr("hash1"));
        T(u != nullptr);
        T(to_str(u->email) == "h@1");
    END

    BEGIN("Thash.FindMissing")
        T(samp_mdb::ind_user_Find(algo::strptr("nonexistent_xyz_999")) == nullptr);
    END

    BEGIN("Thash.DuplicateKey")
        ins("samp_mdb.User  user:dup1  email:first@dup  role:dev");
        bool ok = ins("samp_mdb.User  user:dup1  email:second@dup  role:mgr");
        T(!ok);
        samp_mdb::User* u = samp_mdb::ind_user_Find(algo::strptr("dup1"));
        T(u != nullptr);
        T(to_str(u->email) == "first@dup");
    END

    BEGIN("Thash.MultipleTypes")
        ins("samp_mdb.User  user:ht_u  email:u@u  role:dev");
        ins("samp_mdb.Project  project:ht_p  description:test  status:active");
        ins("samp_mdb.Task  task:ht_t  title:test  priority:1  status:open  p_project:ht_p  p_user:ht_u");
        T(samp_mdb::ind_user_Find(algo::strptr("ht_u")) != nullptr);
        T(samp_mdb::ind_project_Find(algo::strptr("ht_p")) != nullptr);
        T(samp_mdb::ind_task_Find(algo::strptr("ht_t")) != nullptr);
    END

    BEGIN("Thash.LargeKeySet")
        for (int i = 0; i < 200; i++) {
            char buf[128];
            snprintf(buf, sizeof(buf), "samp_mdb.Project  project:hk_%d  description:bulk  status:active", i);
            ins(buf);
        }
        // Verify random access
        T(samp_mdb::ind_project_Find(algo::strptr("hk_0")) != nullptr);
        T(samp_mdb::ind_project_Find(algo::strptr("hk_99")) != nullptr);
        T(samp_mdb::ind_project_Find(algo::strptr("hk_199")) != nullptr);
        T(samp_mdb::ind_project_Find(algo::strptr("hk_200")) == nullptr);
    END
}

// =========================================================================
// LLIST TESTS
// =========================================================================

static void test_llist() {
    BEGIN("Llist.InsertionOrder")
        ins("samp_mdb.Quote  quote:ll_a  symbol:A  price:1.0  ts:1");
        ins("samp_mdb.Quote  quote:ll_b  symbol:B  price:2.0  ts:2");
        ins("samp_mdb.Quote  quote:ll_c  symbol:C  price:3.0  ts:3");
        // Verify order
        int pos_a = -1, pos_b = -1, pos_c = -1, pos = 0;
        for (samp_mdb::Quote* q = samp_mdb::zd_quote_First(); q; q = samp_mdb::zd_quote_Next(*q)) {
            std::string k = to_str(q->quote);
            if (k == "ll_a") pos_a = pos;
            if (k == "ll_b") pos_b = pos;
            if (k == "ll_c") pos_c = pos;
            pos++;
        }
        T(pos_a >= 0 && pos_b >= 0 && pos_c >= 0);
        T(pos_a < pos_b);
        T(pos_b < pos_c);
    END

    BEGIN("Llist.Count")
        T(samp_mdb::zd_quote_N() >= 3);
    END

    BEGIN("Llist.FirstAndLast")
        samp_mdb::Quote* first = samp_mdb::zd_quote_First();
        samp_mdb::Quote* last = samp_mdb::zd_quote_Last();
        T(first != nullptr);
        T(last != nullptr);
        if (samp_mdb::zd_quote_N() > 1) {
            T(first != last);
        }
    END

    BEGIN("Llist.Prev")
        samp_mdb::Quote* last = samp_mdb::zd_quote_Last();
        T(last != nullptr);
        if (samp_mdb::zd_quote_N() > 1) {
            samp_mdb::Quote* prev = samp_mdb::zd_quote_Prev(*last);
            T(prev != nullptr);
            T(prev != last);
        }
    END

    BEGIN("Llist.RemoveFirst")
        int before = samp_mdb::zd_quote_N();
        samp_mdb::Quote* first = samp_mdb::zd_quote_First();
        T(first != nullptr);
        samp_mdb::quote_Delete(*first);
        T(samp_mdb::zd_quote_N() == before - 1);
    END

    BEGIN("Llist.InLlistQ")
        ins("samp_mdb.Quote  quote:ll_inq  symbol:X  price:1.0  ts:1");
        samp_mdb::Quote* q = nullptr;
        for (q = samp_mdb::zd_quote_First(); q; q = samp_mdb::zd_quote_Next(*q)) {
            if (to_str(q->quote) == "ll_inq") break;
        }
        T(q != nullptr);
        T(samp_mdb::zd_quote_InLlistQ(*q));
    END

    BEGIN("Llist.FullIteration")
        int count = 0;
        for (samp_mdb::Quote* q = samp_mdb::zd_quote_First(); q; q = samp_mdb::zd_quote_Next(*q)) {
            count++;
        }
        T(count == samp_mdb::zd_quote_N());
    END
}

// =========================================================================
// BHEAP TESTS
// =========================================================================

static void test_bheap() {
    BEGIN("Bheap.Setup")
        ins("samp_mdb.User  user:bh_u  email:bh@u  role:dev");
        ins("samp_mdb.Project  project:bh_p  description:bh  status:active");
        ins("samp_mdb.Task  task:bh5  title:low  priority:5  status:open  p_project:bh_p  p_user:bh_u");
        ins("samp_mdb.Task  task:bh1  title:high  priority:1  status:open  p_project:bh_p  p_user:bh_u");
        ins("samp_mdb.Task  task:bh3  title:mid  priority:3  status:open  p_project:bh_p  p_user:bh_u");
        ins("samp_mdb.Task  task:bh2  title:med  priority:2  status:open  p_project:bh_p  p_user:bh_u");
        ins("samp_mdb.Task  task:bh4  title:medlow  priority:4  status:open  p_project:bh_p  p_user:bh_u");
        T(samp_mdb::bh_task_N() >= 5);
    END

    BEGIN("Bheap.FirstIsMin")
        samp_mdb::Task* first = samp_mdb::bh_task_First();
        T(first != nullptr);
        T(first->priority == 1);
    END

    BEGIN("Bheap.InBheapQ")
        samp_mdb::Task* t = samp_mdb::ind_task_Find(algo::strptr("bh3"));
        T(t != nullptr);
        T(samp_mdb::bh_task_InBheapQ(*t));
    END

    BEGIN("Bheap.RemoveFirst")
        samp_mdb::Task* first = samp_mdb::bh_task_First();
        T(first != nullptr);
        i32 first_pri = first->priority;
        samp_mdb::bh_task_Remove(*first);
        samp_mdb::Task* new_first = samp_mdb::bh_task_First();
        T(new_first != nullptr);
        T(new_first->priority >= first_pri);
        // Re-insert
        samp_mdb::bh_task_Insert(*first);
    END

    BEGIN("Bheap.OrderAfterRemoves")
        // Remove and check ordering is maintained
        int prev_pri = -1;
        int count = samp_mdb::bh_task_N();
        for (int i = 0; i < count && samp_mdb::bh_task_N() > 0; i++) {
            samp_mdb::Task* top = samp_mdb::bh_task_First();
            T(top != nullptr);
            T(top->priority >= prev_pri);
            prev_pri = top->priority;
            samp_mdb::bh_task_Remove(*top);
        }
        T(samp_mdb::bh_task_N() == 0);
    END

    BEGIN("Bheap.EmptyQ")
        T(samp_mdb::bh_task_EmptyQ());
    END

    BEGIN("Bheap.ReinsertAll")
        // Reinsert tasks back into bheap
        for (samp_mdb::Task* t = samp_mdb::zd_task_First(); t; t = samp_mdb::zd_task_Next(*t)) {
            if (!samp_mdb::bh_task_InBheapQ(*t)) {
                samp_mdb::bh_task_Insert(*t);
            }
        }
        T(samp_mdb::bh_task_N() > 0);
        // First should be min priority again
        samp_mdb::Task* first = samp_mdb::bh_task_First();
        T(first != nullptr);
        T(first->priority == 1);
    END
}

// =========================================================================
// PTRARY TESTS
// =========================================================================

static void test_ptrary() {
    BEGIN("Ptrary.Insert")
        samp_mdb::Project* p = samp_mdb::ind_project_Find(algo::strptr("bh_p"));
        T(p != nullptr);
        samp_mdb::Task* t1 = samp_mdb::ind_task_Find(algo::strptr("bh1"));
        samp_mdb::Task* t3 = samp_mdb::ind_task_Find(algo::strptr("bh3"));
        T(t1 != nullptr);
        T(t3 != nullptr);
        samp_mdb::c_high_priority_task_Insert(*p, *t1);
        samp_mdb::c_high_priority_task_Insert(*p, *t3);
        T(samp_mdb::c_high_priority_task_N(*p) >= 2);
    END

    BEGIN("Ptrary.FindByIndex")
        samp_mdb::Project* p = samp_mdb::ind_project_Find(algo::strptr("bh_p"));
        T(p != nullptr);
        samp_mdb::Task* t = samp_mdb::c_high_priority_task_Find(*p, 0);
        T(t != nullptr);
    END

    BEGIN("Ptrary.EmptyQ")
        samp_mdb::Project* p = samp_mdb::ind_project_Find(algo::strptr("bh_p"));
        T(p != nullptr);
        T(!samp_mdb::c_high_priority_task_EmptyQ(*p));
    END

    BEGIN("Ptrary.Remove")
        samp_mdb::Project* p = samp_mdb::ind_project_Find(algo::strptr("bh_p"));
        T(p != nullptr);
        int before = samp_mdb::c_high_priority_task_N(*p);
        samp_mdb::Task* t = samp_mdb::c_high_priority_task_Find(*p, 0);
        T(t != nullptr);
        samp_mdb::c_high_priority_task_Remove(*p, *t);
        T(samp_mdb::c_high_priority_task_N(*p) == before - 1);
    END

    BEGIN("Ptrary.RemoveAll")
        samp_mdb::Project* p = samp_mdb::ind_project_Find(algo::strptr("bh_p"));
        T(p != nullptr);
        samp_mdb::c_high_priority_task_RemoveAll(*p);
        T(samp_mdb::c_high_priority_task_N(*p) == 0);
        T(samp_mdb::c_high_priority_task_EmptyQ(*p));
    END
}

// =========================================================================
// PKEY / FOREIGN KEY TESTS
// =========================================================================

static void test_pkey() {
    BEGIN("Pkey.TaskReferencesUserAndProject")
        ins("samp_mdb.User  user:pk_u  email:pk@u  role:dev");
        ins("samp_mdb.Project  project:pk_p  description:pkey  status:active");
        ins("samp_mdb.Task  task:pk_t  title:test  priority:1  status:open  p_project:pk_p  p_user:pk_u");
        samp_mdb::Task* t = samp_mdb::ind_task_Find(algo::strptr("pk_t"));
        T(t != nullptr);
        T(to_str(t->p_project) == "pk_p");
        T(to_str(t->p_user) == "pk_u");
    END

    BEGIN("Pkey.ChildListOnProject")
        // zd_task on Project should contain tasks for that project
        samp_mdb::Project* p = samp_mdb::ind_project_Find(algo::strptr("pk_p"));
        T(p != nullptr);
        bool found = false;
        for (samp_mdb::Task* t = samp_mdb::zd_task_First(*p); t; t = samp_mdb::zd_task_Next(*t)) {
            if (to_str(t->task) == "pk_t") found = true;
        }
        T(found);
    END

    BEGIN("Pkey.ChildListOnUser")
        samp_mdb::User* u = samp_mdb::ind_user_Find(algo::strptr("pk_u"));
        T(u != nullptr);
        bool found = false;
        for (samp_mdb::Task* t = samp_mdb::zd_task_First(*u); t; t = samp_mdb::zd_task_Next(*t)) {
            if (to_str(t->task) == "pk_t") found = true;
        }
        T(found);
    END
}

// =========================================================================
// CASCADE DELETE TESTS
// =========================================================================

static void test_cascade_delete() {
    BEGIN("CascadeDelete.ProjectDeletesTasks")
        ins("samp_mdb.User  user:cd_u  email:cd@u  role:dev");
        ins("samp_mdb.Project  project:cd_p  description:cascade  status:active");
        ins("samp_mdb.Task  task:cd_t1  title:t1  priority:1  status:open  p_project:cd_p  p_user:cd_u");
        ins("samp_mdb.Task  task:cd_t2  title:t2  priority:2  status:open  p_project:cd_p  p_user:cd_u");
        T(samp_mdb::ind_task_Find(algo::strptr("cd_t1")) != nullptr);
        T(samp_mdb::ind_task_Find(algo::strptr("cd_t2")) != nullptr);
        samp_mdb::Project* p = samp_mdb::ind_project_Find(algo::strptr("cd_p"));
        T(p != nullptr);
        samp_mdb::project_Delete(*p);
        T(samp_mdb::ind_project_Find(algo::strptr("cd_p")) == nullptr);
        // Tasks should be cascade deleted
        T(samp_mdb::ind_task_Find(algo::strptr("cd_t1")) == nullptr);
        T(samp_mdb::ind_task_Find(algo::strptr("cd_t2")) == nullptr);
    END

    BEGIN("CascadeDelete.UserDeletesTasks")
        ins("samp_mdb.User  user:cd_u2  email:cd@u2  role:dev");
        ins("samp_mdb.Project  project:cd_p2  description:cascade2  status:active");
        ins("samp_mdb.Task  task:cd_t3  title:t3  priority:1  status:open  p_project:cd_p2  p_user:cd_u2");
        T(samp_mdb::ind_task_Find(algo::strptr("cd_t3")) != nullptr);
        samp_mdb::User* u = samp_mdb::ind_user_Find(algo::strptr("cd_u2"));
        T(u != nullptr);
        samp_mdb::user_Delete(*u);
        T(samp_mdb::ind_user_Find(algo::strptr("cd_u2")) == nullptr);
        T(samp_mdb::ind_task_Find(algo::strptr("cd_t3")) == nullptr);
    END
}

// =========================================================================
// SSIM ROUND-TRIP TESTS
// =========================================================================

static void test_ssim() {
    BEGIN("Ssim.RoundTrip")
        ins("samp_mdb.User  user:rt_user  email:rt@test.com  role:tester");
        samp_mdb::User* u = samp_mdb::ind_user_Find(algo::strptr("rt_user"));
        T(u != nullptr);
        algo::cstring out;
        samp_mdb::User_Print(*u, out);
        std::string s = to_str(out);
        T(s.find("samp_mdb.User") != std::string::npos);
        T(s.find("user:rt_user") != std::string::npos);
        T(s.find("email:rt@test.com") != std::string::npos);
        T(s.find("role:tester") != std::string::npos);
    END

    BEGIN("Ssim.ReParseOutput")
        // Print a record, parse it back, verify identical
        ins("samp_mdb.Project  project:reparse  description:\"hello world\"  status:active");
        samp_mdb::Project* p = samp_mdb::ind_project_Find(algo::strptr("reparse"));
        T(p != nullptr);
        algo::cstring out;
        samp_mdb::Project_Print(*p, out);
        // Parse the printed string into a new record
        samp_mdb::Project* p2 = samp_mdb::project_AllocMaybe();
        T(p2 != nullptr);
        T(samp_mdb::Project_ReadStrptrMaybe(*p2, out));
        T(to_str(p2->project) == "reparse");
        T(to_str(p2->description) == "hello world");
        T(to_str(p2->status) == "active");
        samp_mdb::project_Delete(*p2);  // delete since key already exists
    END

    BEGIN("Ssim.TypeDispatch")
        int u0 = samp_mdb::zd_user_N();
        int p0 = samp_mdb::zd_project_N();
        int q0 = samp_mdb::zd_quote_N();
        ins("samp_mdb.User  user:disp_u  email:d@u  role:dev");
        ins("samp_mdb.Project  project:disp_p  description:d  status:active");
        ins("samp_mdb.Quote  quote:disp_q  symbol:X  price:1.0  ts:1");
        T(samp_mdb::zd_user_N() == u0 + 1);
        T(samp_mdb::zd_project_N() == p0 + 1);
        T(samp_mdb::zd_quote_N() == q0 + 1);
    END

    BEGIN("Ssim.BadInput")
        T(!ins("garbage not ssim"));
        T(!ins("unknown.Type  key:value"));
        T(!ins(""));
        // Note: ins("samp_mdb.User") succeeds — AMC allows default empty fields
    END

    BEGIN("Ssim.QuotedValues")
        ins("samp_mdb.Project  project:quoted  description:\"value with spaces\"  status:active");
        samp_mdb::Project* p = samp_mdb::ind_project_Find(algo::strptr("quoted"));
        T(p != nullptr);
        T(to_str(p->description) == "value with spaces");
    END

    BEGIN("Ssim.ComplexKey")
        // Key with special characters
        ins("samp_mdb.Quote  quote:SYM:12345  symbol:SYM  price:99.9  ts:12345");
        samp_mdb::Quote* q = nullptr;
        for (q = samp_mdb::zd_quote_First(); q; q = samp_mdb::zd_quote_Next(*q)) {
            if (to_str(q->quote) == "SYM:12345") break;
        }
        T(q != nullptr);
        T(to_str(q->symbol) == "SYM");
    END
}

// =========================================================================
// FIELD TYPE TESTS
// =========================================================================

static void test_field_types() {
    BEGIN("FieldType.Smallstr")
        ins("samp_mdb.User  user:ft_str  email:test@example.com  role:developer");
        samp_mdb::User* u = samp_mdb::ind_user_Find(algo::strptr("ft_str"));
        T(u != nullptr);
        T(to_str(u->user) == "ft_str");
        T(to_str(u->email) == "test@example.com");
        T(to_str(u->role) == "developer");
    END

    BEGIN("FieldType.I32")
        ins("samp_mdb.User  user:ft_i32u  email:i@32  role:dev");
        ins("samp_mdb.Project  project:ft_i32p  description:i32  status:active");
        ins("samp_mdb.Task  task:ft_i32  title:test  priority:42  status:open  p_project:ft_i32p  p_user:ft_i32u");
        samp_mdb::Task* t = samp_mdb::ind_task_Find(algo::strptr("ft_i32"));
        T(t != nullptr);
        T(t->priority == 42);
    END

    BEGIN("FieldType.I32.Negative")
        ins("samp_mdb.User  user:ft_neg_u  email:n@n  role:dev");
        ins("samp_mdb.Project  project:ft_neg_p  description:neg  status:active");
        ins("samp_mdb.Task  task:ft_neg  title:neg  priority:-5  status:open  p_project:ft_neg_p  p_user:ft_neg_u");
        samp_mdb::Task* t = samp_mdb::ind_task_Find(algo::strptr("ft_neg"));
        T(t != nullptr);
        T(t->priority == -5);
    END

    BEGIN("FieldType.I32.Zero")
        ins("samp_mdb.User  user:ft_z_u  email:z@z  role:dev");
        ins("samp_mdb.Project  project:ft_z_p  description:zero  status:active");
        ins("samp_mdb.Task  task:ft_zero  title:zero  priority:0  status:open  p_project:ft_z_p  p_user:ft_z_u");
        samp_mdb::Task* t = samp_mdb::ind_task_Find(algo::strptr("ft_zero"));
        T(t != nullptr);
        T(t->priority == 0);
    END

    BEGIN("FieldType.Double")
        ins("samp_mdb.Quote  quote:ft_dbl  symbol:TST  price:123.456  ts:1");
        samp_mdb::Quote* q = nullptr;
        for (q = samp_mdb::zd_quote_First(); q; q = samp_mdb::zd_quote_Next(*q)) {
            if (to_str(q->quote) == "ft_dbl") break;
        }
        T(q != nullptr);
        T(fabs(q->price - 123.456) < 0.001);
    END

    BEGIN("FieldType.Double.Negative")
        ins("samp_mdb.Quote  quote:ft_dbl_neg  symbol:TST  price:-42.5  ts:1");
        samp_mdb::Quote* q = nullptr;
        for (q = samp_mdb::zd_quote_First(); q; q = samp_mdb::zd_quote_Next(*q)) {
            if (to_str(q->quote) == "ft_dbl_neg") break;
        }
        T(q != nullptr);
        T(fabs(q->price - (-42.5)) < 0.001);
    END

    BEGIN("FieldType.I64")
        ins("samp_mdb.Quote  quote:ft_i64  symbol:TST  price:1.0  ts:1742558000000");
        samp_mdb::Quote* q = nullptr;
        for (q = samp_mdb::zd_quote_First(); q; q = samp_mdb::zd_quote_Next(*q)) {
            if (to_str(q->quote) == "ft_i64") break;
        }
        T(q != nullptr);
        T(q->ts == 1742558000000LL);
    END

    BEGIN("FieldType.I64.Large")
        ins("samp_mdb.Quote  quote:ft_i64_big  symbol:TST  price:1.0  ts:9223372036854775807");
        samp_mdb::Quote* q = nullptr;
        for (q = samp_mdb::zd_quote_First(); q; q = samp_mdb::zd_quote_Next(*q)) {
            if (to_str(q->quote) == "ft_i64_big") break;
        }
        T(q != nullptr);
        // i64 max
        T(q->ts == 9223372036854775807LL);
    END
}

// =========================================================================
// INTEGRATION TESTS
// =========================================================================

static void test_integration() {
    BEGIN("Integration.InsertViaHelper")
        int u0 = samp_mdb::zd_user_N();
        T(ins("samp_mdb.User  user:ispm  email:i@s  role:qa"));
        T(samp_mdb::zd_user_N() == u0 + 1);
        T(samp_mdb::ind_user_Find(algo::strptr("ispm")) != nullptr);
    END

    BEGIN("Integration.Counts")
        T(samp_mdb::zd_user_N() > 0);
        T(samp_mdb::zd_project_N() > 0);
        T(samp_mdb::zd_task_N() > 0);
        T(samp_mdb::zd_quote_N() > 0);
    END

    BEGIN("Integration.RemoveAll")
        // RemoveAll on quotes
        int n = samp_mdb::zd_quote_N();
        T(n > 0);
        while (samp_mdb::zd_quote_First()) {
            samp_mdb::quote_Delete(*samp_mdb::zd_quote_First());
        }
        T(samp_mdb::zd_quote_N() == 0);
        // Re-add one
        ins("samp_mdb.Quote  quote:after_clear  symbol:X  price:1.0  ts:1");
        T(samp_mdb::zd_quote_N() == 1);
    END
}

// =========================================================================
// ENTRY POINT
// =========================================================================

static void WasmInit() {
    algo_lib::FDb_Init();
    samp_mdb::FDb_Init();
    samp_mdb::user_Reserve(1024);
    samp_mdb::project_Reserve(1024);
    samp_mdb::task_Reserve(1024);
    samp_mdb::comment_Reserve(64);
    samp_mdb::quote_Reserve(1024);
}

static std::string RunAllTests() {
    g_pass = g_fail = g_total = 0;
    g_log.str("");

    g_log << "=== samp_mdb WASM Tests ===\n\n";

    test_tpool();
    test_thash();
    test_llist();
    test_bheap();
    test_ptrary();
    test_pkey();
    test_cascade_delete();
    test_ssim();
    test_field_types();
    test_integration();

    g_log << "\n=== " << g_pass << " passed, " << g_fail << " failed, "
          << g_total << " checks ===\n";

    return g_log.str();
}

EMSCRIPTEN_BINDINGS(samp_mdb_test) {
    function("init", &WasmInit);
    function("runTests", &RunAllTests);
}
