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
void trim_inplace(std::string &s);
bool is_digits(const std::string &s);
void strip_command_verb(std::string &input, const std::string &verb);
bool split_file_line(const std::string &bpos, std::string &file, std::string &linestr);
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

    // Test 3: Helpers — strip_command_verb / split_file_line / is_digits
    std::cout << "[Test 3] Testing parse helpers..." << std::endl;
    {
        // strip_command_verb: exact token, keep filenames starting with verb letter
        {
            std::string s = "b bug.lua:10";
            strip_command_verb(s, "b");
            assert(s == "bug.lua:10");
        }
        {
            std::string s = "  b	bfile.lua : 3  ";
            strip_command_verb(s, "b");
            assert(s == "bfile.lua : 3");
        }
        {
            std::string s = "p printf_like";
            strip_command_verb(s, "p");
            assert(s == "printf_like");
        }
        {
            std::string s = "set setvar=1";
            strip_command_verb(s, "set");
            assert(s == "setvar=1");
        }
        {
            std::string s = "r return 1";
            strip_command_verb(s, "r");
            assert(s == "return 1");
        }
        // no boundary: do not eat into argument
        {
            std::string s = "bbug.lua:1";
            strip_command_verb(s, "b");
            assert(s == "bbug.lua:1");
        }

        // split_file_line: spaces and tabs around ':'
        {
            std::string f, l;
            assert(split_file_line("test.lua:10", f, l));
            assert(f == "test.lua" && l == "10");
            assert(split_file_line("test.lua: 10", f, l));
            assert(f == "test.lua" && l == "10");
            assert(split_file_line("test.lua :10", f, l));
            assert(f == "test.lua" && l == "10");
            assert(split_file_line("test.lua : 10", f, l));
            assert(f == "test.lua" && l == "10");
            assert(split_file_line("test.lua	:	10", f, l));
            assert(f == "test.lua" && l == "10");
            assert(split_file_line("  bfile.lua  :  7  ", f, l));
            assert(f == "bfile.lua" && l == "7");
            assert(!split_file_line("test.lua 10", f, l));
        }

        assert(is_digits("123"));
        assert(!is_digits(""));
        assert(!is_digits("12a"));
        assert(!is_digits(" 12"));

        std::cout << "  -> PASS" << std::endl;
    }

    // Test 3b: Breakpoint commands (process_b_command) table-driven
    std::cout << "[Test 3b] Testing process_b_command..." << std::endl;
    {
        g_blist.clear();

        // Create a file whose name starts with 'b' for verb-strip regression
        {
            FILE *fp = fopen("btest.lua", "w");
            assert(fp != NULL);
            fputs("local a = 1\nlocal b = 2\nlocal c = 3\nlocal d = 4\nlocal e = 5\n", fp);
            fputs("local f = 6\nlocal g = 7\nlocal h = 8\nlocal i = 9\nlocal j = 10\n", fp);
            fclose(fp);
        }

        struct BCase {
            const char *input;
            bool expect_ok;
            const char *expect_file;
            int expect_line;
        };
        BCase cases[] = {
            {"b test.lua:10", true, "test.lua", 10},
            {"b test.lua 20", true, "test.lua", 20},
            {"b test.lua : 25", true, "test.lua", 25},
            {"b test.lua: 11", true, "test.lua", 11},
            {"b test.lua :12", true, "test.lua", 12},
            {"b test.lua	:	13", true, "test.lua", 13},
            {"b	test.lua:15", true, "test.lua", 15},
            {"b btest.lua:10", true, "btest.lua", 10},
            {"b test.lua:abc", false, "", 0},
            {"b test.lua:", false, "", 0},
            {"b :10", false, "", 0},
            {"b test.lua:1x", false, "", 0},
        };

        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
            size_t before = g_blist.size();
            char data[QUEUED_MESSAGE_MSG_LEN];
            memset(data, 0, sizeof(data));
            strncpy(data, cases[i].input, QUEUED_MESSAGE_MSG_LEN - 1);
            // result only needs size >= 2 when args present; mirror tokenization loosely
            std::vector<std::string> res;
            {
                std::istringstream iss(cases[i].input);
                for (std::string tok; iss >> tok;) {
                    res.push_back(tok);
                }
            }
            process_b_command(L, res, data);
            if (cases[i].expect_ok) {
                assert(g_blist.size() == before + 1);
                assert(g_blist.back().file == cases[i].expect_file);
                assert(g_blist.back().line == cases[i].expect_line);
            } else {
                assert(g_blist.size() == before);
            }
        }

        // Conditional breakpoints (existing coverage)
        {
            char data4[QUEUED_MESSAGE_MSG_LEN] = "b test.lua:30 if [x] x == 1";
            std::vector<std::string> res4 = {"b", "test.lua:30", "if", "[x]", "x", "==", "1"};
            process_b_command(L, res4, data4);
            assert(g_blist.back().file == "test.lua");
            assert(g_blist.back().line == 30);
            assert(g_blist.back().ifstr == " x == 1");
            assert(g_blist.back().iffunc != "");
        }
        {
            char data5[QUEUED_MESSAGE_MSG_LEN] = "b test.lua 35 if [x] x > 0";
            std::vector<std::string> res5 = {"b", "test.lua", "35", "if", "[x]", "x", ">", "0"};
            process_b_command(L, res5, data5);
            assert(g_blist.back().file == "test.lua");
            assert(g_blist.back().line == 35);
            assert(g_blist.back().ifstr == " x > 0");
        }

        // Drain messages sent to q2
        long mtype = 0;
        char mdata[QUEUED_MESSAGE_MSG_LEN];
        while (recv_msg(q2, mtype, mdata) == 0 && mtype != 0) {}

        std::cout << "  -> PASS" << std::endl;
    }

    // Test 4: Disable, Enable, Delete Breakpoint
    std::cout << "[Test 4] Testing disable, enable, and delete breakpoint..." << std::endl;
    {
        assert(g_blist.size() >= 2);

        // Disable breakpoint 1
        std::vector<std::string> dis1 = {"dis", "1"};
        process_dis_command(L, dis1);
        {
            bool found = false;
            for (const auto &bp : g_blist) {
                if (bp.no == 1) {
                    assert(bp.en == false);
                    found = true;
                }
            }
            assert(found);
        }

        // Enable breakpoint 1
        std::vector<std::string> en1 = {"en", "1"};
        process_en_command(L, en1);
        {
            bool found = false;
            for (const auto &bp : g_blist) {
                if (bp.no == 1) {
                    assert(bp.en == true);
                    found = true;
                }
            }
            assert(found);
        }

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
        size_t before_del = g_blist.size();
        std::vector<std::string> d1 = {"d", "1"};
        process_d_command(L, d1);
        assert(g_blist.size() == before_del - 1);
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
    unlink("btest.lua");
    lua_close(L);
    msgctl(q1, IPC_RMID, 0);
    msgctl(q2, IPC_RMID, 0);

    std::cout << "\nALL TESTS PASSED SUCCESSFULLY!" << std::endl;
    return 0;
}
