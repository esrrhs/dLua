// In-process lifecycle / architecture tests for the dLua agent.
//
// These are NOT hookso attach tests and do not mock the hook path: they call
// start_agent/stop_agent against real lua_State(s) and real SysV IPC.
// True process-level attach lives in scripts/smoke_attach.sh and
// scripts/smoke_scenarios.sh.
//
// Assertions follow the supported architecture:
//   - one agent session at a time (g_L / g_opening)
//   - Lua 5.3/5.4 lua_newthread copies the current hook to a new coroutine
//   - coroutines created *before* attach are not auto-hooked
//   - fini_agent restores the hook only on g_L; leftover hooks on other
//     threads are disarmed when hook_handler next runs on them
//
// A process-wide alarm prevents a stuck step-wait loop from hanging CI.

#include "common.h"
#include <cassert>
#include <chrono>
#include <iostream>
#include <atomic>
#include <thread>
#include <vector>
#include <signal.h>
#include <sys/select.h>
#include <sys/msg.h>

extern "C" {
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}

struct BreakPoint {
    std::string file;
    std::string norm_file;
    int line;
    int no;
    bool en;
    int hit;
    bool temporary;
    std::string ifstr;
    std::string iffunc;
    std::vector <std::string> iffuncparam;
};

extern int g_qid_send;
extern int g_qid_recv;
extern lua_State *g_L;
extern int g_pid;
extern int g_opening;
extern int g_step;
extern int g_step_next;
extern int g_step_next_in;
extern int g_step_next_out;
extern int g_until_active;
extern std::vector<BreakPoint> g_blist;

extern "C" int start_agent(lua_State *L, int pid);
extern "C" int stop_agent();
extern "C" int is_stop_agent();
void hook_handler(lua_State *L, lua_Debug *par);
int lastlevel(lua_State *L);
int process_bt_command(lua_State *L);
int process_b_command(lua_State *L, const std::vector<std::string> &result,
                      char data[QUEUED_MESSAGE_MSG_LEN], bool temporary = false);

static const int OPENING_STOP = 0;

#if LUA_VERSION_NUM >= 504
static int resume_co(lua_State *co, lua_State *from, int nargs) {
    int nres = 0;
    return lua_resume(co, from, nargs, &nres);
}
#else
static int resume_co(lua_State *co, lua_State *from, int nargs) {
    return lua_resume(co, from, nargs);
}
#endif

static void alarm_handler(int) {
    const char msg[] = "test_scenarios: TIMEOUT (possible hang)\n";
    ssize_t _n = write(STDERR_FILENO, msg, sizeof(msg) - 1);
    (void)_n;
    _exit(124);
}

static long long now_ms() {
    using clock = std::chrono::steady_clock;
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               clock::now().time_since_epoch())
        .count();
}

struct Session {
    int pid;
    std::string path1;
    std::string path2;
    int q1;  // CLI send / agent recv  (COMMAND)
    int q2;  // agent send / CLI recv  (LOGIN/SHOW/INPUT)
};

static int g_session_seq = 0;

static Session open_session() {
    Session s;
    // Fake pid so ftok paths cannot collide with a live process or other tests.
    s.pid = 810000 + ((int)getpid() % 9000) * 10 + (g_session_seq++ % 10);
    s.path1 = dlua_ipc_tmp_path(s.pid, 1);
    s.path2 = dlua_ipc_tmp_path(s.pid, 2);
    s.q1 = open_msg_queue(s.path1.c_str(), 1, 0600);
    s.q2 = open_msg_queue(s.path2.c_str(), 1, 0600);
    assert(s.q1 >= 0 && s.q2 >= 0);
    return s;
}

static void close_session(Session &s) {
    remove_msg_queue(s.q1);
    remove_msg_queue(s.q2);
    s.q1 = s.q2 = -1;
    unlink(s.path1.c_str());
    unlink(s.path2.c_str());
}

static void reset_agent_globals() {
    g_opening = OPENING_STOP;
    g_L = NULL;
    g_qid_send = -1;
    g_qid_recv = -1;
    g_step = 0;
    g_step_next = 0;
    g_step_next_in = 0;
    g_step_next_out = 0;
    g_until_active = 0;
    g_blist.clear();
}

static void drain_queue(int qid) {
    long t = 0;
    char d[QUEUED_MESSAGE_MSG_LEN];
    while (recv_msg(qid, t, d) == 0 && t != 0) {
    }
}

static bool recv_type(int qid, long want, std::string *payload, int timeout_ms) {
    long long deadline = now_ms() + timeout_ms;
    while (now_ms() < deadline) {
        long t = 0;
        char d[QUEUED_MESSAGE_MSG_LEN];
        memset(d, 0, sizeof(d));
        if (recv_msg(qid, t, d) != 0) {
            return false;
        }
        if (t == 0) {
            usleep(1000);
            continue;
        }
        if (t == want) {
            if (payload) {
                *payload = d;
            }
            return true;
        }
        // skip unrelated messages
    }
    return false;
}

// Run enough VM instructions to trip both count=1 (attach) and count=1000 (idle).
static void pump_lua(lua_State *L, int n = 8000) {
    lua_pushinteger(L, n);
    lua_setglobal(L, "__dlua_pump_n");
    int rc = luaL_dostring(L,
                           "local n = __dlua_pump_n\n"
                           "for i = 1, n do local x = i * 2; x = x + 1 end\n");
    if (rc != LUA_OK) {
        const char *err = lua_tostring(L, -1);
        std::cerr << "pump_lua failed: " << (err ? err : "?") << std::endl;
        assert(rc == LUA_OK);
    }
}

static lua_State *new_lua() {
    lua_State *L = luaL_newstate();
    assert(L != NULL);
    luaL_openlibs(L);
    return L;
}

static void attach_and_login(lua_State *L, Session &s) {
    assert(is_stop_agent() == 1);
    int rc = start_agent(L, s.pid);
    assert(rc == 0);
    pump_lua(L);
    std::string login;
    assert(recv_type(s.q2, LOGIN_MSG, &login, 2000));
    assert(login == std::to_string(s.pid));
    assert(is_stop_agent() == 0);
    assert(lua_gethook(L) == hook_handler);
}

static void detach_and_stop(lua_State *L) {
    assert(stop_agent() == 0);
    pump_lua(L);
    assert(is_stop_agent() == 1);
}

// ---------------------------------------------------------------------------
// Test 8: repeated attach/detach + hook restore
// ---------------------------------------------------------------------------
static int g_dummy_hits = 0;
static void dummy_hook(lua_State *L, lua_Debug *ar) {
    (void)L;
    (void)ar;
    g_dummy_hits++;
}

static void test_repeated_attach_detach() {
    std::cout << "[Test 8] Repeated attach/detach and hook restore..." << std::endl;
    reset_agent_globals();
    Session s = open_session();
    lua_State *L = new_lua();

    lua_sethook(L, dummy_hook, LUA_MASKCOUNT, 50);
    g_dummy_hits = 0;
    pump_lua(L, 200);
    assert(g_dummy_hits > 0);
    int hits_before = g_dummy_hits;

    for (int cycle = 1; cycle <= 3; ++cycle) {
        assert(lua_gethook(L) == dummy_hook);
        assert(is_stop_agent() == 1);
        // Second start while already attached must be rejected.
        attach_and_login(L, s);
        assert(start_agent(L, s.pid) == -1);
        assert(lua_gethook(L) == hook_handler);
        detach_and_stop(L);
        assert(lua_gethook(L) == dummy_hook);
        assert(lua_gethookmask(L) == LUA_MASKCOUNT);
        assert(lua_gethookcount(L) == 50);
        drain_queue(s.q2);
    }

    int hits_mid = g_dummy_hits;
    pump_lua(L, 200);
    assert(g_dummy_hits > hits_mid);
    (void)hits_before;

    lua_close(L);
    close_session(s);
    reset_agent_globals();
    std::cout << "  -> PASS" << std::endl;
}

// ---------------------------------------------------------------------------
// Test 9: multiple independent lua_States (one session at a time)
// ---------------------------------------------------------------------------
static void test_multiple_states() {
    std::cout << "[Test 9] Multiple lua_States (single-session architecture)..."
              << std::endl;
    reset_agent_globals();
    Session s = open_session();
    lua_State *L1 = new_lua();
    lua_State *L2 = new_lua();

    attach_and_login(L1, s);
    // Cannot start a second session while the first is live.
    assert(start_agent(L2, s.pid) == -1);
    // Only the attached state is hooked.
    assert(lua_gethook(L1) == hook_handler);
    assert(lua_gethook(L2) == NULL);

    drain_queue(s.q2);
    pump_lua(L2, 4000);
    // Running the unattached state must not produce a second LOGIN or crash.
    long t = 0;
    char d[QUEUED_MESSAGE_MSG_LEN];
    int extra_login = 0;
    while (recv_msg(s.q2, t, d) == 0 && t != 0) {
        if (t == LOGIN_MSG) {
            extra_login++;
        }
    }
    assert(extra_login == 0);

    detach_and_stop(L1);
    assert(lua_gethook(L1) == NULL);
    assert(lua_gethook(L2) == NULL);

    // After a clean detach, a different state can be attached.
    drain_queue(s.q2);
    attach_and_login(L2, s);
    assert(lua_gethook(L2) == hook_handler);
    assert(lua_gethook(L1) == NULL);
    detach_and_stop(L2);
    assert(lua_gethook(L2) == NULL);

    lua_close(L1);
    lua_close(L2);
    close_session(s);
    reset_agent_globals();
    std::cout << "  -> PASS" << std::endl;
}

// ---------------------------------------------------------------------------
// Test 10: coroutines — inheritance, pre-existing threads, bt/lastlevel,
//          leftover-hook disarm after detach
// ---------------------------------------------------------------------------
static int g_bt_level = -1;
static void lastlevel_hook(lua_State *L, lua_Debug *ar) {
    if (ar->event != LUA_HOOKLINE) {
        return;
    }
    if (!lua_getinfo(L, "Sln", ar)) {
        return;
    }
    std::string src = ar->short_src ? ar->short_src : "";
    // Stop inside leaf() at "local x = 1" (coro_bt.lua:2).
    if (src.find("coro_bt.lua") != std::string::npos && ar->currentline == 2) {
        g_bt_level = lastlevel(L);
        process_bt_command(L);
        lua_sethook(L, NULL, 0, 0);
    }
}

static void test_coroutines() {
    std::cout << "[Test 10] Coroutines (hook inheritance and backtrace)..."
              << std::endl;
    reset_agent_globals();
    Session s = open_session();
    lua_State *L = new_lua();

    // --- 10a: coroutine created *before* attach is not auto-hooked ---
    lua_State *pre = lua_newthread(L);
    assert(lua_gethook(pre) == NULL);
    int lrc = luaL_loadstring(pre,
                              "local s = 0\n"
                              "for i = 1, 4000 do s = s + i end\n"
                              "return s\n");
    assert(lrc == LUA_OK);

    attach_and_login(L, s);
    assert(lua_gethook(pre) == NULL);
    int rrc = resume_co(pre, L, 0);
    assert(rrc == LUA_OK);
    assert(lua_gethook(pre) == NULL);

    // --- 10b: coroutine created *after* attach inherits hook_handler ---
    lua_State *co = lua_newthread(L);
    assert(lua_gethook(co) == hook_handler);
    lrc = luaL_loadstring(co,
                          "local s = 0\n"
                          "for i = 1, 4000 do s = s + i end\n"
                          "return s\n");
    assert(lrc == LUA_OK);
    rrc = resume_co(co, L, 0);
    assert(rrc == LUA_OK);
    // Still hooked after the work (session is still live).
    assert(lua_gethook(co) == hook_handler);

    // --- 10c: detach restores main hook; leftover inherited hook is disarmed
    //          the next time that coroutine runs (fini only restores g_L). ---
    drain_queue(s.q2);
    detach_and_stop(L);
    assert(lua_gethook(L) == NULL);

    // Un-resumed leftover: may still point at hook_handler until it runs.
    // Resume a fresh post-attach-style thread that still has the leftover hook:
    // 'co' was created while attached, so it may still have hook_handler.
    if (lua_gethook(co) == hook_handler) {
        lrc = luaL_loadstring(co,
                              "local s = 0\n"
                              "for i = 1, 4000 do s = s + i end\n"
                              "return s\n");
        assert(lrc == LUA_OK);
        rrc = resume_co(co, L, 0);
        assert(rrc == LUA_OK);
    }
    assert(lua_gethook(co) != hook_handler);
    assert(is_stop_agent() == 1);

    lua_close(L);
    close_session(s);
    reset_agent_globals();

    // --- 10d: lastlevel / process_bt_command on a coroutine stack ---
    // This is an in-process unit check of those helpers, not attach.
    s = open_session();
    g_qid_send = s.q2;
    g_qid_recv = s.q1;
    L = new_lua();
    {
        // Avoid tail calls so mid/top remain on the coroutine stack.
        FILE *fp = fopen("coro_bt.lua", "w");
        assert(fp != NULL);
        fputs("function leaf()\n", fp);          // 1
        fputs("  local x = 1\n", fp);            // 2  (stop here)
        fputs("  return x\n", fp);               // 3
        fputs("end\n", fp);                      // 4
        fputs("function mid()\n", fp);           // 5
        fputs("  local r = leaf()\n", fp);       // 6
        fputs("  return r\n", fp);               // 7
        fputs("end\n", fp);                      // 8
        fputs("function top()\n", fp);           // 9
        fputs("  local r = mid()\n", fp);        // 10
        fputs("  return r\n", fp);               // 11
        fputs("end\n", fp);                      // 12
        fclose(fp);
    }
    assert(luaL_dofile(L, "coro_bt.lua") == LUA_OK);
    lua_State *btc = lua_newthread(L);
    lua_getglobal(btc, "top");
    assert(lua_isfunction(btc, -1));
    g_bt_level = -1;
    drain_queue(s.q2);
    lua_sethook(btc, lastlevel_hook, LUA_MASKLINE, 0);
    rrc = resume_co(btc, L, 0);
    assert(rrc == LUA_OK);
    assert(g_bt_level >= 2);  // leaf + mid + top (not tail-called)
    long t = 0;
    char d[QUEUED_MESSAGE_MSG_LEN];
    std::string acc;
    while (recv_msg(s.q2, t, d) == 0 && t != 0) {
        if (t == SHOW_MSG) {
            acc += d;
        }
    }
    // lua_getinfo name is often NULL on this path; match frames by source:line.
    // Frame text looks like: "N in ? at coro_bt.lua:LINE"
    assert(acc.find("coro_bt.lua:2") != std::string::npos);
    assert(acc.find("coro_bt.lua:6") != std::string::npos);
    assert(acc.find("coro_bt.lua:10") != std::string::npos);

    unlink("coro_bt.lua");
    lua_close(L);
    close_session(s);
    reset_agent_globals();
    std::cout << "  -> PASS" << std::endl;
}

// ---------------------------------------------------------------------------
// Test 11: breakpoint inside a post-attach coroutine, with a commander
//          thread so the step-wait loop cannot hang the test.
// ---------------------------------------------------------------------------
static void test_coroutine_breakpoint() {
    std::cout << "[Test 11] Breakpoint hit inside an inherited-hook coroutine..."
              << std::endl;
    reset_agent_globals();
    Session s = open_session();
    lua_State *L = new_lua();

    {
        FILE *fp = fopen("coro_bp.lua", "w");
        assert(fp != NULL);
        fputs("function coro_work()\n", fp);   // 1
        fputs("  local a = 1\n", fp);         // 2
        fputs("  local b = 2\n", fp);         // 3  <- breakpoint
        fputs("  local c = a + b\n", fp);     // 4
        fputs("  return c\n", fp);            // 5
        fputs("end\n", fp);
        fclose(fp);
    }
    assert(luaL_dofile(L, "coro_bp.lua") == LUA_OK);

    attach_and_login(L, s);

    char data[QUEUED_MESSAGE_MSG_LEN];
    memset(data, 0, sizeof(data));
    strncpy(data, "b coro_bp.lua:3", QUEUED_MESSAGE_MSG_LEN - 1);
    std::vector<std::string> res = {"b", "coro_bp.lua:3"};
    process_b_command(L, res, data, false);
    assert(!g_blist.empty());
    drain_queue(s.q2);

    std::atomic<bool> saw_bp(false);
    std::atomic<bool> sent_c(false);
    std::atomic<bool> done(false);
    std::thread cmd([&]() {
        long long deadline = now_ms() + 8000;
        while (!done.load() && now_ms() < deadline) {
            long t = 0;
            char buf[QUEUED_MESSAGE_MSG_LEN];
            memset(buf, 0, sizeof(buf));
            if (recv_msg(s.q2, t, buf) != 0) {
                break;
            }
            if (t == 0) {
                usleep(1000);
                continue;
            }
            std::string msg = buf;
            if (msg.find("Breakpoint") != std::string::npos) {
                saw_bp.store(true);
            }
            if (t == INPUT_MSG && !sent_c.load()) {
                send_msg(s.q1, COMMAND_MSG, "c");
                sent_c.store(true);
            }
        }
    });

    lua_State *co = lua_newthread(L);
    assert(lua_gethook(co) == hook_handler);
    lua_getglobal(co, "coro_work");
    int rrc = resume_co(co, L, 0);
    done.store(true);
    cmd.join();
    assert(rrc == LUA_OK);
    assert(saw_bp.load());
    assert(sent_c.load());
    assert(lua_tointeger(co, -1) == 3);

    detach_and_stop(L);
    unlink("coro_bp.lua");
    lua_close(L);
    close_session(s);
    reset_agent_globals();
    std::cout << "  -> PASS" << std::endl;
}

// ---------------------------------------------------------------------------
// Test 12: target early exit (lua_close / SIGKILL) without hanging, IPC
//          remains owned by the "CLI" side and can be removed.
// ---------------------------------------------------------------------------
static void test_early_exit() {
    std::cout << "[Test 12] Target early exit and IPC cleanup..." << std::endl;

    // 12a: lua_close while the agent is still RUNNING (fini never runs).
    {
        reset_agent_globals();
        Session s = open_session();
        lua_State *L = new_lua();
        attach_and_login(L, s);
        lua_close(L);
        L = NULL;
        // Agent globals are stale; session queues must still be removable.
        assert(is_stop_agent() == 0);
        struct msqid_ds ds;
        assert(msgctl(s.q1, IPC_STAT, &ds) == 0);
        assert(msgctl(s.q2, IPC_STAT, &ds) == 0);
        close_session(s);
        // After RMID the ids must no longer STAT successfully (or EIDRM/EINVAL).
        errno = 0;
        int st = msgctl(s.q1, IPC_STAT, &ds);
        assert(st != 0);
        reset_agent_globals();
    }

    // 12b: stop_agent requested but lua_close before the fini hook runs.
    {
        reset_agent_globals();
        Session s = open_session();
        lua_State *L = new_lua();
        attach_and_login(L, s);
        assert(stop_agent() == 0);
        assert(is_stop_agent() == 0);  // still END, hook has not run fini
        lua_close(L);
        close_session(s);
        reset_agent_globals();
    }

    // 12c: child process is SIGKILL'd mid-session; parent cleans queues.
    {
        reset_agent_globals();
        int parent_pipe[2];
        assert(pipe(parent_pipe) == 0);
        pid_t child = fork();
        assert(child >= 0);
        if (child == 0) {
            close(parent_pipe[0]);
            int pid = (int)getpid();
            std::string p1 = dlua_ipc_tmp_path(pid, 1);
            std::string p2 = dlua_ipc_tmp_path(pid, 2);
            int q1 = open_msg_queue(p1.c_str(), 1, 0600);
            int q2 = open_msg_queue(p2.c_str(), 1, 0600);
            if (q1 < 0 || q2 < 0) {
                _exit(2);
            }
            lua_State *L = new_lua();
            if (start_agent(L, pid) != 0) {
                _exit(3);
            }
            pump_lua(L);
            // Tell the parent we have published LOGIN and are spinning.
            char ok = 'Y';
            ssize_t _w = write(parent_pipe[1], &ok, 1);
            (void)_w;
            close(parent_pipe[1]);
            while (1) {
                pump_lua(L, 1000);
            }
        }
        close(parent_pipe[1]);
        // Wait until the child reports LOGIN completed, with a deadline.
        char ok = 0;
        long long deadline = now_ms() + 8000;
        while (now_ms() < deadline && ok != 'Y') {
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(parent_pipe[0], &rfds);
            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 200000;
            int sr = select(parent_pipe[0] + 1, &rfds, NULL, NULL, &tv);
            if (sr > 0) {
                if (read(parent_pipe[0], &ok, 1) <= 0) {
                    break;
                }
            }
            int status = 0;
            pid_t wp = waitpid(child, &status, WNOHANG);
            if (wp == child) {
                std::cerr << "child exited early, status=" << status << std::endl;
                assert(false);
            }
        }
        close(parent_pipe[0]);
        assert(ok == 'Y');

        std::string p1 = dlua_ipc_tmp_path((int)child, 1);
        std::string p2 = dlua_ipc_tmp_path((int)child, 2);
        int q1 = open_msg_queue(p1.c_str(), 1, 0600);
        int q2 = open_msg_queue(p2.c_str(), 1, 0600);
        assert(q1 >= 0 && q2 >= 0);
        // LOGIN should already have been queued (may have been consumed or not
        // if we just opened the same queues; either way they must exist).
        struct msqid_ds ds;
        assert(msgctl(q1, IPC_STAT, &ds) == 0);
        assert(msgctl(q2, IPC_STAT, &ds) == 0);

        int kr = kill(child, SIGKILL);
        assert(kr == 0);
        int status = 0;
        deadline = now_ms() + 5000;
        pid_t wp = 0;
        while (now_ms() < deadline) {
            wp = waitpid(child, &status, WNOHANG);
            if (wp == child) {
                break;
            }
            usleep(10000);
        }
        assert(wp == child);
        assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);

        assert(remove_msg_queue(q1) == 0);
        assert(remove_msg_queue(q2) == 0);
        unlink(p1.c_str());
        unlink(p2.c_str());
        reset_agent_globals();
    }

    std::cout << "  -> PASS" << std::endl;
}

int main() {
    std::cout << "=== Running dLua Scenario Tests (in-process) ===" << std::endl;
    std::cout << "Lua version: " << LUA_VERSION
#if defined(LUA_RELEASE)
              << " (" << LUA_RELEASE << ")"
#endif
              << std::endl;

    signal(SIGALRM, alarm_handler);
    alarm(60);

    test_repeated_attach_detach();
    test_multiple_states();
    test_coroutines();
    test_coroutine_breakpoint();
    test_early_exit();

    alarm(0);
    std::cout << "\nALL SCENARIO TESTS PASSED SUCCESSFULLY!" << std::endl;
    return 0;
}
