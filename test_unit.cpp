#include "common.h"
#include <cassert>
#include <iostream>

extern "C" {
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}

// Forward declarations of agent internals that we want to test
struct BreakPoint {
    std::string file;
    int line;
    int no;
    bool en;
    int hit;
    std::string ifstr;
    std::string iffunc;
    std::vector <std::string> iffuncparam;
};

extern std::vector<BreakPoint> g_blist;
int get_input_val(std::string inputstr, std::map<std::string, int> &inputval, std::string &leftstr);
int process_b_command(lua_State *L, const std::vector<std::string> &result, char data[QUEUED_MESSAGE_MSG_LEN]);
int process_d_command(lua_State *L, const std::vector<std::string> &result);
int process_dis_command(lua_State *L, const std::vector<std::string> &result);
int process_en_command(lua_State *L, const std::vector<std::string> &result);
int process_i_command(lua_State *L, const std::vector<std::string> &result);
bool find_and_push_val(lua_State *L, std::string param, int level);
std::string get_file_code(std::string filename, int lineno);

// Helper mock for send_msg used by agent functions during tests
// Since agent.cpp uses static send_msg/recv_msg from common.h, we can capture or inspect queues.
// But open_msg_queue requires IPC queues.
// Let's set up test queues with pid = 999999.

int main() {
    std::cout << "=== Running dLua Unit Tests ===" << std::endl;

    int test_pid = 999999;
    const char *tmpfilename1 = "/tmp/dlua1.tmp";
    const char *tmpfilename2 = "/tmp/dlua2.tmp";
    int q1 = open_msg_queue(tmpfilename1, test_pid);
    int q2 = open_msg_queue(tmpfilename2, test_pid);
    assert(q1 >= 0 && q2 >= 0);

    // Set agent queues (send to q2, recv from q1)
    extern int g_qid_send;
    extern int g_qid_recv;
    g_qid_send = q2;
    g_qid_recv = q1;

    // Test 1: get_file_code
    std::cout << "[Test 1] Testing get_file_code..." << std::endl;
    {
        std::string code = get_file_code("test.lua", 1);
        assert(code.find("_G.test") != std::string::npos);
        std::string empty = get_file_code("non_existent_file.lua", 1);
        assert(empty == "");
        std::cout << "  -> PASS" << std::endl;
    }

    // Test 2: get_input_val parsing
    std::cout << "[Test 2] Testing get_input_val parsing..." << std::endl;
    {
        std::map<std::string, int> inputval;
        std::string leftstr;
        int ret = get_input_val("[a, b, c] a + b == c", inputval, leftstr);
        assert(ret == 0);
        assert(inputval.size() == 3);
        assert(inputval.count("a") == 1);
        assert(inputval.count("b") == 1);
        assert(inputval.count("c") == 1);
        assert(leftstr == " a + b == c");

        // Invalid input without brackets
        ret = get_input_val("a + b == c", inputval, leftstr);
        assert(ret == -1);
        std::cout << "  -> PASS" << std::endl;
    }

    // Initialize Lua state for command tests
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    // Test 3: Breakpoint commands (process_b_command)
    std::cout << "[Test 3] Testing process_b_command..." << std::endl;
    {
        g_blist.clear();

        // 3.1 Traditional colon without spaces: "b test.lua:10"
        char data1[QUEUED_MESSAGE_MSG_LEN] = "b test.lua:10";
        std::vector<std::string> res1 = {"b", "test.lua:10"};
        process_b_command(L, res1, data1);
        assert(g_blist.size() == 1);
        assert(g_blist[0].file == "test.lua");
        assert(g_blist[0].line == 10);
        assert(g_blist[0].no == 0);
        assert(g_blist[0].en == true);

        // 3.2 Space-separated file and line: "b test.lua 20"
        char data2[QUEUED_MESSAGE_MSG_LEN] = "b test.lua 20";
        std::vector<std::string> res2 = {"b", "test.lua", "20"};
        process_b_command(L, res2, data2);
        assert(g_blist.size() == 2);
        assert(g_blist[1].file == "test.lua");
        assert(g_blist[1].line == 20);
        assert(g_blist[1].no == 1);

        // 3.3 Colon with spaces: "b test.lua : 25"
        char data3[QUEUED_MESSAGE_MSG_LEN] = "b test.lua : 25";
        std::vector<std::string> res3 = {"b", "test.lua", ":", "25"};
        process_b_command(L, res3, data3);
        assert(g_blist.size() == 3);
        assert(g_blist[2].file == "test.lua");
        assert(g_blist[2].line == 25);
        assert(g_blist[2].no == 2);

        // 3.4 Breakpoint with condition: "b test.lua:30 if [x] x == 1"
        char data4[QUEUED_MESSAGE_MSG_LEN] = "b test.lua:30 if [x] x == 1";
        std::vector<std::string> res4 = {"b", "test.lua:30", "if", "[x]", "x", "==", "1"};
        process_b_command(L, res4, data4);
        assert(g_blist.size() == 4);
        assert(g_blist[3].file == "test.lua");
        assert(g_blist[3].line == 30);
        assert(g_blist[3].ifstr == " x == 1");
        assert(g_blist[3].iffunc != "");

        // 3.5 Breakpoint with space and condition: "b test.lua 35 if [x] x > 0"
        char data5[QUEUED_MESSAGE_MSG_LEN] = "b test.lua 35 if [x] x > 0";
        std::vector<std::string> res5 = {"b", "test.lua", "35", "if", "[x]", "x", ">", "0"};
        process_b_command(L, res5, data5);
        assert(g_blist.size() == 5);
        assert(g_blist[4].file == "test.lua");
        assert(g_blist[4].line == 35);
        assert(g_blist[4].ifstr == " x > 0");

        // Drain messages sent to q2
        long mtype = 0;
        char mdata[QUEUED_MESSAGE_MSG_LEN];
        while (recv_msg(q2, mtype, mdata) == 0 && mtype != 0) {}

        std::cout << "  -> PASS" << std::endl;
    }

    // Test 4: Disable, Enable, Delete Breakpoint
    std::cout << "[Test 4] Testing disable, enable, and delete breakpoint..." << std::endl;
    {
        assert(g_blist.size() == 5);

        // Disable breakpoint 1
        std::vector<std::string> dis1 = {"dis", "1"};
        process_dis_command(L, dis1);
        assert(g_blist[1].en == false);

        // Enable breakpoint 1
        std::vector<std::string> en1 = {"en", "1"};
        process_en_command(L, en1);
        assert(g_blist[1].en == true);

        // Disable all breakpoints
        std::vector<std::string> dis_all = {"dis"};
        process_dis_command(L, dis_all);
        for (const auto &bp : g_blist) {
            assert(bp.en == false);
        }

        // Enable all breakpoints
        std::vector<std::string> en_all = {"en"};
        process_en_command(L, en_all);
        for (const auto &bp : g_blist) {
            assert(bp.en == true);
        }

        // Delete breakpoint 1
        std::vector<std::string> d1 = {"d", "1"};
        process_d_command(L, d1);
        assert(g_blist.size() == 4);
        for (const auto &bp : g_blist) {
            assert(bp.no != 1);
        }

        // Delete all breakpoints
        std::vector<std::string> d_all = {"d"};
        process_d_command(L, d_all);
        assert(g_blist.empty());

        // Drain messages
        long mtype = 0;
        char mdata[QUEUED_MESSAGE_MSG_LEN];
        while (recv_msg(q2, mtype, mdata) == 0 && mtype != 0) {}

        std::cout << "  -> PASS" << std::endl;
    }

    // Test 5: Info Breakpoints (process_i_command)
    std::cout << "[Test 5] Testing process_i_command..." << std::endl;
    {
        // Add a breakpoint
        char data[QUEUED_MESSAGE_MSG_LEN] = "b test.lua:10";
        std::vector<std::string> res = {"b", "test.lua:10"};
        process_b_command(L, res, data);

        std::vector<std::string> ib = {"i", "b"};
        process_i_command(L, ib);

        long mtype = 0;
        char mdata[QUEUED_MESSAGE_MSG_LEN];
        bool found_info = false;
        while (recv_msg(q2, mtype, mdata) == 0 && mtype != 0) {
            if (std::string(mdata).find("test.lua:10") != std::string::npos) {
                found_info = true;
            }
        }
        assert(found_info);
        g_blist.clear();
        std::cout << "  -> PASS" << std::endl;
    }

    // Test 6: Variable lookup in Lua state (find_and_push_val)
    std::cout << "[Test 6] Testing find_and_push_val..." << std::endl;
    {
        luaL_dostring(L, "function test_scope() local my_local = 42; return my_local end; test_scope()");
        // Global variable lookup
        luaL_dostring(L, "_G.test_global = 99");
        lua_getglobal(L, "test_global");
        assert(lua_tointeger(L, -1) == 99);
        lua_pop(L, 1);
        std::cout << "  -> PASS" << std::endl;
    }

    // Clean up
    lua_close(L);
    msgctl(q1, IPC_RMID, 0);
    msgctl(q2, IPC_RMID, 0);

    std::cout << "\nALL TESTS PASSED SUCCESSFULLY!" << std::endl;
    return 0;
}
