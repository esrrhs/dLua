#include "common.h"

extern "C" {
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}

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

const int RUNNING_STATE_STOP = 0;
const int RUNNING_STATE_BEGIN = 1;
const int RUNNING_STATE_RUNNING = 2;
const int RUNNING_STATE_END = 3;

int g_qid_send;
int g_qid_recv;
lua_State *g_L;
int g_pid;
lua_Hook g_old_hook;
int g_old_hook_mask;
int g_old_hook_count;
int g_opening;
std::vector <BreakPoint> g_blist;
int g_step;
int g_step_next;
int g_step_next_in;
int g_step_next_out;
std::string g_step_last_file = "";
std::string g_step_last_func = "";
int g_step_last_line;
int g_step_last_level;
int g_step_debug_level;
int g_load_pprint_string;
int g_load_getvar_string;
int g_load_setvar_string;

extern "C" int is_stop_agent() {
    if (g_opening == RUNNING_STATE_STOP) {
        return 1;
    }
    return 0;
}

extern "C" int stop_agent() {

    if (g_opening == RUNNING_STATE_STOP) {
        return -1;
    }
    g_opening = RUNNING_STATE_END;

    return 0;
}

std::string get_file_code(std::string filename, int lineno) {
    FILE *fp = fopen(filename.c_str(), "r");
    if (fp == NULL) {
        return "";
    }

    char *line = NULL;
    size_t len = 0;
    int index = 1;
    std::string ret = "";
    while ((getline(&line, &len, fp)) != -1) {
        if (lineno == index) {
            ret = line;
            break;
        }
        index++;
    }
    fclose(fp);
    if (line) {
        free(line);
    }
    ret.erase(std::remove(ret.begin(), ret.end(), '\n'), ret.end());
    return ret;
}

int lastlevel(lua_State *L) {
    lua_Debug ar;
    int li = 1, le = 1;
    /* find an upper bound */
    while (lua_getstack(L, le, &ar)) {
        li = le;
        le *= 2;
    }
    /* do a binary search */
    while (li < le) {
        int m = (li + le) / 2;
        if (lua_getstack(L, m, &ar)) li = m + 1;
        else le = m;
    }
    return le - 1;
}

int ini_agent() {
    DLOG("ini_agent start %d", g_pid);

    const char *tmpfilename1 = "/tmp/dlua1.tmp";
    const char *tmpfilename2 = "/tmp/dlua2.tmp";
    int qid1 = open_msg_queue(tmpfilename1, g_pid);
    if (qid1 < 0) {
        DERR("open_msg_queue fail");
        return -1;
    }
    int qid2 = open_msg_queue(tmpfilename2, g_pid);
    if (qid2 < 0) {
        DERR("open_msg_queue fail");
        return -1;
    }

    g_qid_send = qid2;
    g_qid_recv = qid1;

    send_msg(g_qid_send, LOGIN_MSG, std::to_string(g_pid).c_str());

    g_blist.clear();
    g_step = 0;
    g_step_next = 0;
    g_step_next_in = 0;
    g_step_next_out = 0;
    g_step_last_file = "";
    g_step_last_func = "";
    g_step_last_line = 0;
    g_step_last_level = 0;
    g_step_debug_level = 0;
    g_load_pprint_string = 0;
    g_load_getvar_string = 0;
    g_load_setvar_string = 0;

    DLOG("ini_agent ok %d", g_pid);

    return 0;
}

int fini_agent() {
    DLOG("fini_agent start %d", g_pid);
    msgctl(g_qid_send, IPC_RMID, 0);
    msgctl(g_qid_recv, IPC_RMID, 0);
    lua_sethook(g_L, g_old_hook, g_old_hook_mask, g_old_hook_count);
    DLOG("fini_agent ok %d", g_pid);
    return 0;
}

int process_help_command() {
    std::string ret = "h\thelp commands\n"
                      "q\tquit\n"
                      "bt\tshow cur call stack\n"
                      "b\tadd breakpoint, eg: b test.lua:123\n"
                      "i\tshow info, eg: i b\n"
                      "n\tstep next line\n"
                      "s\tstep into next line\n"
                      "c\tcontinue run\n"
                      "dis\tdisable breakpoint, eg: dis 1\n"
                      "en\tenable breakpoint, eg: en 1\n"
                      "d\tdelete breakpoint, eg: d 1\n"
                      "p\tprint exp value, eg: p _G.xxx\n"
                      "l\tlist code\n"
                      "f\tselect stack frame\n"
                      "fin\tfinish current call\n"
                      "set\tset value, eg: set aa=1\n"
                      "r\trun code, eg: r print(\"test\")\n";
    send_msg(g_qid_send, SHOW_MSG, ret.c_str());
    return 0;
}

int process_bt_command(lua_State *L) {
    lua_Debug entry;
    memset(&entry, 0, sizeof(entry));
    int depth = 0;
    char buff[128] = {0};
    std::string ret = "";
    while (lua_getstack(L, depth, &entry)) {
        int status = lua_getinfo(L, "Sln", &entry);
        if (status <= 0) {
            break;
        }

        // #0  0x00007f772b7abe30 in __nanosleep_nocancel () from /lib64/libpthread.so.0
        memset(buff, 0, sizeof(buff));
        snprintf(buff, sizeof(buff) - 1, "%d in %s at %s:%d\n", depth, entry.name ? entry.name : "?", entry.short_src,
                 entry.currentline);
        ret = ret + buff;
        depth++;
    }
    DLOG("process_bt_command %s", ret.c_str());
    send_msg(g_qid_send, SHOW_MSG, ret.c_str());
    return 0;
}

bool find_and_push_val(lua_State *L, std::string param, int level) {

    int oldn = lua_gettop(L);

    DLOG("find_and_push_val oldn %d", oldn);

    if (g_load_getvar_string == 0) {
        const char *loadstr = "function dlua_getvarvalue (name, frame)\n"
                              "    local value, found\n"
                              "\n"
                              "    -- try local variables\n"
                              "    local i = 1\n"
                              "    while true do\n"
                              "        local n, v = debug.getlocal(frame + 2, i)\n"
                              "        if not n then\n"
                              "            break\n"
                              "        end\n"
                              "        if n == name then\n"
                              "            value = v\n"
                              "            found = true\n"
                              "        end\n"
                              "        i = i + 1\n"
                              "    end\n"
                              "    if found then\n"
                              "        return value, true\n"
                              "    end\n"
                              "\n"
                              "    -- try upvalues\n"
                              "    local func = debug.getinfo(frame + 2).func\n"
                              "    i = 1\n"
                              "    while true do\n"
                              "        local n, v = debug.getupvalue(func, i)\n"
                              "        if not n then\n"
                              "            break\n"
                              "        end\n"
                              "        if n == name then\n"
                              "            return v, true\n"
                              "        end\n"
                              "        i = i + 1\n"
                              "    end\n"
                              "\n"
                              "    return nil, false\n"
                              "end\n"
                              "";

        luaL_dostring(L, loadstr);
        lua_settop(L, oldn);
        g_load_getvar_string = 1;
    }

    lua_getglobal(L, "dlua_getvarvalue");
    if (!lua_isfunction(L, -1)) {
        lua_settop(L, oldn);
        return false;
    }

    lua_pushstring(L, param.c_str());
    lua_pushinteger(L, level);
    lua_pcall(L, 2, 2, 0);

    int newn = lua_gettop(L);
    if (newn > oldn) {
        bool ret = lua_toboolean(L, -1);
        if (ret) {
            lua_pop(L, 1);
            return true;
        } else {
            lua_settop(L, oldn);
            return false;
        }
    } else {
        lua_settop(L, oldn);
        return false;
    }
}

int get_input_val(std::string inputstr, std::map<std::string, int> &inputval, std::string &leftstr) {

    std::string str = inputstr;

    std::size_t left = str.find_first_of("[");
    std::size_t right = str.find_first_of("]");
    if (left == std::string::npos || right == std::string::npos || left >= right || left != 0) {
        return -1;
    }
    std::string inputvals = str.substr(left + 1, right - 1);

    inputstr.erase(0, right + 1);

    inputval.clear();
    std::string delimiter = ",";
    size_t pos = 0;
    while ((pos = inputvals.find(delimiter)) != std::string::npos) {
        std::string token = inputvals.substr(0, pos);
        token.erase(0, token.find_first_not_of(" "));
        token.erase(token.find_last_not_of(" ") + 1);
        if (token != "") {
            inputval[token] = 1;
        }
        inputvals.erase(0, pos + delimiter.length());
    }
    inputvals.erase(0, inputvals.find_first_not_of(" "));
    inputvals.erase(inputvals.find_last_not_of(" ") + 1);
    if (inputvals != "") {
        inputval[inputvals] = 1;
    }

    leftstr = inputstr;
    return 0;
}

int process_b_command(lua_State *L, const std::vector <std::string> &result, char data[QUEUED_MESSAGE_MSG_LEN]) {
    std::string file;
    std::string linestr;
    std::string ifparam = "";

    if (result.size() < 2) {
        if (g_step != 0) {
            // b
            lua_Debug entry;
            memset(&entry, 0, sizeof(entry));
            if (lua_getstack(L, g_step_debug_level, &entry) <= 0) {
                return 0;
            }
            int status = lua_getinfo(L, "Sln", &entry);
            if (status <= 0) {
                return 0;
            }
            file = entry.short_src;
            linestr = std::to_string(entry.currentline);
        } else {
            send_msg(g_qid_send, SHOW_MSG, "breakpoint need file:line\n");
            return 0;
        }
    } else {
        std::string bpos = result[1];
        DLOG("process_b_command start %s", bpos.c_str());

        std::string delimiter = ":";
        size_t pos = 0;
        std::vector <std::string> tokens;
        while ((pos = bpos.find(delimiter)) != std::string::npos) {
            std::string token = bpos.substr(0, pos);
            tokens.push_back(token);
            bpos.erase(0, pos + delimiter.length());
        }
        tokens.push_back(bpos);
        if (tokens.size() < 2) {
            std::string bpoint = tokens[0];
            if (std::all_of(bpoint.begin(), bpoint.end(), ::isdigit)) {
                // b 123
                lua_Debug entry;
                memset(&entry, 0, sizeof(entry));
                if (lua_getstack(L, g_step_debug_level, &entry) <= 0) {
                    return 0;
                }
                int status = lua_getinfo(L, "Sln", &entry);
                if (status <= 0) {
                    return 0;
                }
                file = entry.short_src;
                linestr = tokens[0];
            } else {
                // b xxx.xxx.xxx
                std::string delimiter = ".";
                size_t pos = 0;
                std::vector <std::string> tokens;
                while ((pos = bpoint.find(delimiter)) != std::string::npos) {
                    std::string token = bpoint.substr(0, pos);
                    tokens.push_back(token);
                    bpoint.erase(0, pos + delimiter.length());
                }
                tokens.push_back(bpoint);

                int oldn = lua_gettop(L);

                lua_getglobal(L, tokens[0].c_str());
                for (int i = 0; i < tokens.size() - 1; ++i) {
                    if (!lua_istable(L, -1)) {
                        lua_settop(L, oldn);
                        send_msg(g_qid_send, SHOW_MSG, (std::string("can not find table ") + tokens[i]).c_str());
                        return 0;
                    }
                    lua_getfield(L, -1, tokens[i + 1].c_str());
                    lua_remove(L, -2);
                }

                lua_Debug entry;
                memset(&entry, 0, sizeof(entry));
                if (!lua_isfunction(L, -1)) {
                    lua_settop(L, oldn);
                    send_msg(g_qid_send, SHOW_MSG,
                             (std::string("can not find function ") + tokens[tokens.size() - 1]).c_str());
                    return 0;
                }
                int status = lua_getinfo(L, ">S", &entry);
                if (status <= 0) {
                    lua_settop(L, oldn);
                    return 0;
                }

                lua_settop(L, oldn);

                file = entry.short_src;
                linestr = std::to_string(entry.linedefined + 1);
            }
        } else {
            // b test.lua:123
            file = tokens[0];
            linestr = tokens[1];
        }

        // b xxx if a==x
        if (result.size() >= 4) {
            std::string iftr = result[2];
            if (iftr != "if") {
                send_msg(g_qid_send, SHOW_MSG, "eg: b test.lua:33 if [a,b] a + b == 10 ");
                return 0;
            }

            std::string input = data;
            input.erase(0, input.find_first_not_of(" "));
            input.erase(0, input.find_first_not_of("b"));
            input.erase(0, input.find_first_not_of(" "));
            input.erase(0, input.find_first_not_of(result[1]));
            input.erase(0, input.find_first_not_of(" "));
            input.erase(0, input.find_first_not_of("if"));
            input.erase(0, input.find_first_not_of(" "));

            ifparam = input;
        }
    }

    DLOG("process_b_command start %s %s %s", file.c_str(), linestr.c_str(), ifparam.c_str());
    int line = atoi(linestr.c_str());

    std::string code = get_file_code(file, line);
    std::string tmpcode = code;
    tmpcode.erase(std::remove(tmpcode.begin(), tmpcode.end(), '\r'), tmpcode.end());
    tmpcode.erase(std::remove(tmpcode.begin(), tmpcode.end(), '\n'), tmpcode.end());
    tmpcode.erase(std::remove(tmpcode.begin(), tmpcode.end(), ' '), tmpcode.end());
    if (tmpcode == "") {
        std::string ret;
        char buff[128] = {0};
        snprintf(buff, sizeof(buff) - 1, "there is no code at %s:%d", file.c_str(), line);
        ret = buff;
        send_msg(g_qid_send, SHOW_MSG, ret.c_str());
        return 0;
    }

    int bindex = -1;
    int maxno = -1;
    for (int i = 0; i < g_blist.size(); ++i) {
        if (g_blist[i].file == file && g_blist[i].line == line) {
            bindex = i;
            break;
        }
        if (g_blist[i].no > maxno) {
            maxno = g_blist[i].no;
        }
    }

    std::string iffunc = "";
    std::vector <std::string> iffuncparam;
    if (ifparam != "") {
        std::string leftstr = "";
        std::map<std::string, int> inputval;
        if (get_input_val(ifparam, inputval, leftstr) != 0) {
            send_msg(g_qid_send, SHOW_MSG, "eg: b test.lua:33 if [a,b] a + b == 10 ");
            return 0;
        }
        ifparam = leftstr;

        int oldn = lua_gettop(L);
        std::string loadstr = "function dlua_debug_if" + std::to_string(maxno + 1) + "(";
        for (auto it = inputval.begin(); it != inputval.end();) {
            loadstr = loadstr + it->first;
            it++;
            if (it != inputval.end()) {
                loadstr = loadstr + ",";
            }
        }
        loadstr = loadstr + ") return " + ifparam + " end";
        DLOG("process_b_command if %s", loadstr.c_str());
        int status = luaL_dostring(L, loadstr.c_str());
        if (status != 0) {
            std::string ret = lua_tostring(L, -1);
            lua_settop(L, oldn);
            send_msg(g_qid_send, SHOW_MSG, ret.c_str());
            return 0;
        }
        lua_settop(L, oldn);

        iffunc = "dlua_debug_if" + std::to_string(maxno + 1);
        for (auto it = inputval.begin(); it != inputval.end(); it++) {
            iffuncparam.push_back(it->first);
        }
    }

    if (bindex == -1) {
        BreakPoint b;
        b.file = file;
        b.line = line;
        b.no = maxno + 1;
        b.hit = 0;
        g_blist.push_back(b);
        bindex = g_blist.size() - 1;
    }
    g_blist[bindex].en = true;
    g_blist[bindex].ifstr = ifparam;
    g_blist[bindex].iffunc = iffunc;
    g_blist[bindex].iffuncparam = iffuncparam;

    std::string ret;
    char buff[128] = {0};
    // Breakpoint 2 at 0x41458c: file lapi.c, line 968.
    snprintf(buff, sizeof(buff) - 1, "Breakpoint %d at file %s, line %d, if %s\n", g_blist[bindex].no,
             g_blist[bindex].file.c_str(), g_blist[bindex].line, g_blist[bindex].ifstr.c_str());
    DLOG("process_b_command %d %s %d", g_blist[bindex].no, g_blist[bindex].file.c_str(), g_blist[bindex].line);
    ret = buff;
    send_msg(g_qid_send, SHOW_MSG, ret.c_str());
    send_msg(g_qid_send, SHOW_MSG, (std::string("code: ") + code).c_str());

    return 0;
}

int process_i_command(lua_State *L, const std::vector <std::string> &result) {
    if (result.size() < 2) {
        send_msg(g_qid_send, SHOW_MSG, "info need param\n");
        return 0;
    }

    std::string param = result[1];
    if (param == "b") {
        // Num     Type           Disp Enb Address            What
        //1       breakpoint     keep y   0x000000000041458c in lua_pcallk at lapi.c:968
        std::string ret = "Num\tEnb\tWhat\tHit\tIf\n";
        char buff[128] = {0};
        for (int i = 0; i < g_blist.size(); ++i) {
            memset(buff, 0, sizeof(buff));
            snprintf(buff, sizeof(buff) - 1, "%d\t%s\t%s:%d\t%d\t%s\n", g_blist[i].no, g_blist[i].en ? "y" : "n",
                     g_blist[i].file.c_str(), g_blist[i].line, g_blist[i].hit, g_blist[i].ifstr.c_str());
            ret = ret + buff;
        }
        DLOG("process_i_command b %s", ret.c_str());
        send_msg(g_qid_send, SHOW_MSG, ret.c_str());
    } else {
        send_msg(g_qid_send, SHOW_MSG, "param not support\n");
        return 0;
    }

    return 0;
}

int process_n_command(lua_State *L) {
    if (g_step != 0) {
        g_step_next = 1;
    }
    return 0;
}

int process_fin_command(lua_State *L) {
    if (g_step != 0) {
        g_step_next_out = 1;
    }
    return 0;
}

int process_s_command(lua_State *L) {
    if (g_step != 0) {
        g_step_next_in = 1;
    }
    return 0;
}

int process_c_command(lua_State *L) {
    g_step = 0;
    g_step_next = 0;
    g_step_next_in = 0;
    g_step_next_out = 0;
    g_step_last_file = "";
    g_step_last_func = "";
    g_step_last_line = 0;
    g_step_last_level = 0;
    g_step_debug_level = 0;
    return 0;
}

int process_dis_command(lua_State *L, const std::vector <std::string> &result) {
    int bid = -1;
    if (result.size() >= 2) {
        bid = atoi(result[1].c_str());
    }

    for (int i = 0; i < g_blist.size(); ++i) {
        if (g_blist[i].no == bid || bid == -1) {
            g_blist[i].en = false;
        }
    }

    char buff[128] = {0};
    if (bid != -1) {
        snprintf(buff, sizeof(buff) - 1, "disable breakpoint %d\n", bid);
    } else {
        snprintf(buff, sizeof(buff) - 1, "disable all breakpoint\n");
    }
    std::string ret = buff;
    send_msg(g_qid_send, SHOW_MSG, ret.c_str());
    return 0;
}

int process_en_command(lua_State *L, const std::vector <std::string> &result) {
    int bid = -1;
    if (result.size() >= 2) {
        bid = atoi(result[1].c_str());
    }

    for (int i = 0; i < g_blist.size(); ++i) {
        if (g_blist[i].no == bid || bid == -1) {
            g_blist[i].en = true;
        }
    }

    char buff[128] = {0};
    if (bid != -1) {
        snprintf(buff, sizeof(buff) - 1, "enable breakpoint %d\n", bid);
    } else {
        snprintf(buff, sizeof(buff) - 1, "enable all breakpoint\n");
    }
    std::string ret = buff;
    send_msg(g_qid_send, SHOW_MSG, ret.c_str());
    return 0;
}

int process_d_command(lua_State *L, const std::vector <std::string> &result) {
    int bid = -1;
    if (result.size() >= 2) {
        bid = atoi(result[1].c_str());
    }

    if (bid == -1) {
        g_blist.clear();
    } else {
        for (int i = 0; i < g_blist.size(); ++i) {
            if (g_blist[i].no == bid) {
                g_blist.erase(g_blist.begin() + i);
            }
        }
    }

    char buff[128] = {0};
    if (bid != -1) {
        snprintf(buff, sizeof(buff) - 1, "delete breakpoint %d\n", bid);
    } else {
        snprintf(buff, sizeof(buff) - 1, "delete all breakpoint\n");
    }
    std::string ret = buff;
    send_msg(g_qid_send, SHOW_MSG, ret.c_str());
    return 0;
}

int process_p_command(lua_State *L, const std::vector <std::string> &result, char data[QUEUED_MESSAGE_MSG_LEN]) {
    if (result.size() < 2) {
        send_msg(g_qid_send, SHOW_MSG, "print need param\n");
        return 0;
    }

    if (g_step == 0) {
        send_msg(g_qid_send, SHOW_MSG, "need in step mode\n");
        return 0;
    }

    std::string input = data;
    input.erase(0, input.find_first_not_of(" "));
    input.erase(0, input.find_first_not_of("p"));
    input.erase(0, input.find_first_not_of(" "));
    input.erase(input.find_last_not_of(" ") + 1);

    std::string param = input;

    DLOG("process_p_command start %s", param.c_str());

    lua_Debug entry;
    memset(&entry, 0, sizeof(entry));
    if (lua_getstack(L, g_step_debug_level, &entry) <= 0) {
        return 0;
    }
    int status = lua_getinfo(L, "Sln", &entry);
    if (status <= 0) {
        return 0;
    }

    int oldn = lua_gettop(L);

    DLOG("process_p_command oldn %d", oldn);

    if (g_load_pprint_string == 0) {
        const char *loadstr = "local function dlua_unprintable_cb(s)\n"
                              "    return string.format(\"\\\\%03d\", string.byte(s, 1, 1))\n"
                              "end\n"
                              "\n"
                              "local function dlua_tostring (value)\n"
                              "    local typestr = type(value)\n"
                              "    local ptvalue = tostring(value)\n"
                              "\n"
                              "    if typestr == \"table\" or typestr == \"function\" or typestr == \"userdata\" then\n"
                              "        ptvalue = string.format(\"[%s]\", ptvalue)\n"
                              "    elseif typestr == \"string\" then\n"
                              "        ptvalue = string.gsub(ptvalue, \"\\r\", \"\\\\r\")\n"
                              "        ptvalue = string.gsub(ptvalue, \"\\n\", \"\\\\n\")\n"
                              "        ptvalue = string.gsub(ptvalue, \"%c\", dlua_unprintable_cb)\n"
                              "        ptvalue = string.format(\"\\\"%s\\\"\", ptvalue)\n"
                              "    end\n"
                              "\n"
                              "    return ptvalue\n"
                              "end\n"
                              "\n"
                              "local function dlua_tprint (tbl, indent, visit, path, max)\n"
                              "    if not indent then\n"
                              "        indent = 0\n"
                              "    end\n"
                              "    local ret = \"\"\n"
                              "    for k, v in pairs(tbl) do\n"
                              "        if type(v) ~= \"function\" and type(v) ~= \"userdata\" and type(v) ~= \"thread\" then\n"
                              "            local formatting = string.rep(\"  \", indent) .. dlua_tostring(k) .. \": \"\n"
                              "            if type(v) == \"table\" then\n"
                              "                if visit[v] then\n"
                              "                    ret = ret .. formatting .. \"*Recursion at \" .. visit[v] .. \"*\\n\"\n"
                              "                else\n"
                              "                    visit[v] = path .. \"/\" .. dlua_tostring(k)\n"
                              "                    ret = ret .. formatting .. \"\\n\" .. dlua_tprint(v, indent + 1, visit, path .. \"/\" .. dlua_tostring(k), max)\n"
                              "                end\n"
                              "            elseif type(v) == 'boolean' or type(v) == 'number' then\n"
                              "                ret = ret .. formatting .. dlua_tostring(v) .. \"\\n\"\n"
                              "            elseif type(v) == 'string' then\n"
                              "                ret = ret .. formatting .. \"'\" .. dlua_tostring(v) .. \"'\" .. \"\\n\"\n"
                              "            end\n"
                              "            if #ret > max then\n"
                              "                break\n"
                              "            end\n"
                              "        end\n"
                              "    end\n"
                              "    return ret\n"
                              "end\n"
                              "\n"
                              "function dlua_pprint (tbl)\n"
                              "    local path = \"\"\n"
                              "    local visit = {}\n"
                              "    if type(tbl) ~= \"table\" then\n"
                              "        return dlua_tostring(tbl)\n"
                              "    end\n"
                              "    local max = 1024 * 1024\n"
                              "    local ret = dlua_tprint(tbl, 0, visit, path, max)\n"
                              "    if #ret > max then\n"
                              "        ret = ret .. \"...\"\n"
                              "    end\n"
                              "    return ret\n"
                              "end\n"
                              "return _G.dlua_pprint(\"ok\")";

        luaL_dostring(L, loadstr);
        std::string ret = lua_tostring(L, -1);
        lua_settop(L, oldn);
        DLOG("luaL_dostring ret %s", ret.c_str());
        g_load_pprint_string = 1;
    }

    lua_getglobal(L, "dlua_pprint");
    if (!lua_isfunction(L, -1)) {
        lua_settop(L, oldn);
        send_msg(g_qid_send, SHOW_MSG, "get dlua_pprint fail\n");
        return 0;
    }

    if (result.size() >= 3 && param.length() > 0 && param[0] == '[') {
        // p [a] a.t
        std::string leftstr;
        std::map<std::string, int> inputval;
        if (get_input_val(param, inputval, leftstr) != 0) {
            send_msg(g_qid_send, SHOW_MSG, "eg: p [t] t.a");
            return 0;
        }

        std::string loadstr = "function dlua_p_val(";
        for (auto it = inputval.begin(); it != inputval.end();) {
            loadstr = loadstr + it->first;
            it++;
            if (it != inputval.end()) {
                loadstr = loadstr + ",";
            }
        }
        loadstr = loadstr + ")\n return " + leftstr + "\n end\n";
        DLOG("process_p_command p %s", loadstr.c_str());
        int status = luaL_dostring(L, loadstr.c_str());
        if (status != 0) {
            std::string ret = lua_tostring(L, -1);
            lua_settop(L, oldn);
            send_msg(g_qid_send, SHOW_MSG, ret.c_str());
            return 0;
        }

        lua_getglobal(L, "dlua_p_val");
        if (!lua_isfunction(L, -1)) {
            lua_settop(L, oldn);
            send_msg(g_qid_send, SHOW_MSG, "get dlua_p_val fail\n");
            return 0;
        }

        for (auto it = inputval.begin(); it != inputval.end(); it++) {
            if (!find_and_push_val(L, it->first, g_step_debug_level)) {
                lua_settop(L, oldn);
                send_msg(g_qid_send, SHOW_MSG, (std::string("can not find val ") + it->first).c_str());
                return 0;
            }
        }

        lua_pcall(L, inputval.size(), 1, 0);

        lua_pcall(L, 1, 1, 0);
    } else {
        // p aa
        bool find = find_and_push_val(L, param, g_step_debug_level);
        if (!find) {
            std::string tmp = "return dlua_pprint(" + param + ")";
            luaL_dostring(L, tmp.c_str());
            DLOG("call dlua_pprint %s", param.c_str());
        } else {
            lua_pcall(L, 1, 1, 0);
        }
    }

    int newn = lua_gettop(L);
    if (newn > oldn) {
        std::string ret = lua_tostring(L, -1);

        std::string delimiter = "\n";
        size_t pos = 0;
        while ((pos = ret.find(delimiter)) != std::string::npos) {
            std::string token = ret.substr(0, pos);
            send_msg(g_qid_send, SHOW_MSG, token.c_str());
            ret.erase(0, pos + delimiter.length());
        }
        send_msg(g_qid_send, SHOW_MSG, ret.c_str());
    }

    lua_settop(L, oldn);

    return 0;
}

int process_l_command(lua_State *L, const std::vector <std::string> &result) {
    int width = 5;
    if (result.size() >= 2) {
        width = atoi(result[1].c_str());
        if (width <= 0) {
            width = 5;
        }
    }

    if (g_step == 0) {
        send_msg(g_qid_send, SHOW_MSG, "need in step mode\n");
        return 0;
    }

    lua_Debug entry;
    memset(&entry, 0, sizeof(entry));
    if (lua_getstack(L, g_step_debug_level, &entry) <= 0) {
        return 0;
    }
    int status = lua_getinfo(L, "Sln", &entry);
    if (status <= 0) {
        return 0;
    }
    std::string file = entry.short_src;
    std::string linestr = std::to_string(entry.currentline);
    int curline = atoi(linestr.c_str());

    for (int i = -width; i <= width; ++i) {
        int line = i + curline;
        if (curline <= 0) {
            continue;
        }
        std::string code = get_file_code(file, line);
        if (code == "") {
            continue;
        }
        std::string ret;
        char buff[128] = {0};
        snprintf(buff, sizeof(buff) - 1, "%d %s\n", line, code.c_str());
        ret = buff;
        send_msg(g_qid_send, SHOW_MSG, ret.c_str());
    }

    return 0;
}

int process_f_command(lua_State *L, const std::vector <std::string> &result) {
    if (result.size() < 2) {
        send_msg(g_qid_send, SHOW_MSG, "frame need param\n");
        return 0;
    }

    if (g_step == 0) {
        send_msg(g_qid_send, SHOW_MSG, "need in step mode\n");
        return 0;
    }

    int frame = atoi(result[1].c_str());

    lua_Debug entry;
    memset(&entry, 0, sizeof(entry));
    if (lua_getstack(L, frame, &entry) <= 0) {
        char buff[128] = {0};
        snprintf(buff, sizeof(buff) - 1, "frame set fail %d\n", frame);
        std::string ret = buff;
        send_msg(g_qid_send, SHOW_MSG, ret.c_str());
    } else {
        int status = lua_getinfo(L, "Sln", &entry);
        if (status <= 0) {
            char buff[128] = {0};
            snprintf(buff, sizeof(buff) - 1, "frame set fail %d\n", frame);
            std::string ret = buff;
            send_msg(g_qid_send, SHOW_MSG, ret.c_str());
        } else {
            g_step_debug_level = frame;

            // #0  usage () at main.cpp:16
            char buff[128] = {0};
            std::string curfunc = entry.name ? entry.name : "?";
            std::string curfile = entry.short_src;
            int curline = entry.currentline;
            snprintf(buff, sizeof(buff) - 1, "#%d %s at %s:%d\n", frame, curfunc.c_str(), curfile.c_str(), curline);
            std::string ret = buff;
            send_msg(g_qid_send, SHOW_MSG, ret.c_str());

            // 16          printf("dlua: pid\n");
            std::string code = get_file_code(curfile, curline);
            memset(buff, 0, sizeof(buff));
            snprintf(buff, sizeof(buff) - 1, "%d %s\n", curline, code.c_str());
            ret = buff;
            send_msg(g_qid_send, SHOW_MSG, ret.c_str());
        }
    }

    return 0;
}

int process_set_command(lua_State *L, const std::vector <std::string> &result, char data[QUEUED_MESSAGE_MSG_LEN]) {
    if (result.size() < 2) {
        send_msg(g_qid_send, SHOW_MSG, "set need param\n");
        return 0;
    }

    if (g_step == 0) {
        send_msg(g_qid_send, SHOW_MSG, "need in step mode\n");
        return 0;
    }

    std::string input = data;
    input.erase(0, input.find_first_not_of(" "));
    input.erase(0, input.find_first_not_of("set"));
    input.erase(0, input.find_first_not_of(" "));
    input.erase(input.find_last_not_of(" ") + 1);

    std::map<std::string, int> inputval;
    if (result.size() >= 3 && input.length() > 0 && input[0] == '[') {
        // set [a] a.t = 123
        std::string leftstr;
        if (get_input_val(input, inputval, leftstr) != 0) {
            send_msg(g_qid_send, SHOW_MSG, "eg: set [t] t.a=1");
            return 0;
        }
        input = leftstr;
    }

    std::string delimiter = "=";
    size_t pos = input.find(delimiter);
    if (pos == std::string::npos) {
        send_msg(g_qid_send, SHOW_MSG, "param is xxx=xxx\n");
        return 0;
    }
    std::string val = input.substr(0, pos);
    input.erase(0, pos + delimiter.length());

    val.erase(0, val.find_first_not_of(" "));
    val.erase(val.find_last_not_of(" ") + 1);
    input.erase(0, input.find_first_not_of(" "));
    input.erase(input.find_last_not_of(" ") + 1);

    DLOG("process_set_command set %s to %s", val.c_str(), input.c_str());

    int oldn = lua_gettop(L);

    if (g_load_setvar_string == 0) {
        const char *loadstr = "function dlua_setvarvalue (name, frame, val, level)\n"
                              "    local found\n"
                              "\n"
                              "    -- try local variables\n"
                              "    local i = 1\n"
                              "    while true do\n"
                              "        local n, v = debug.getlocal(frame + level, i)\n"
                              "        if not n then\n"
                              "            break\n"
                              "        end\n"
                              "        if n == name then\n"
                              "            debug.setlocal(frame + level, i, val)\n"
                              "            found = true\n"
                              "        end\n"
                              "        i = i + 1\n"
                              "    end\n"
                              "    if found then\n"
                              "        return true\n"
                              "    end\n"
                              "\n"
                              "    -- try upvalues\n"
                              "    local func = debug.getinfo(frame + level).func\n"
                              "    i = 1\n"
                              "    while true do\n"
                              "        local n, v = debug.getupvalue(func, i)\n"
                              "        if not n then\n"
                              "            break\n"
                              "        end\n"
                              "        if n == name then\n"
                              "            debug.setupvalue(func, i, val)\n"
                              "            return true\n"
                              "        end\n"
                              "        i = i + 1\n"
                              "    end\n"
                              "\n"
                              "    return false\n"
                              "end"
                              "";

        luaL_dostring(L, loadstr);
        lua_settop(L, oldn);
        g_load_setvar_string = 1;
    }

    if (inputval.empty()) {
        std::string loadstr =
                "if not dlua_setvarvalue(\"" + val + "\"," + std::to_string(g_step_debug_level) + "," + input + ", 3" +
                ") then\n";
        loadstr += val + "=" + input + "\n";
        loadstr += "end\n";
        int status = luaL_dostring(L, loadstr.c_str());
        if (status != 0) {
            std::string ret = lua_tostring(L, -1);
            lua_settop(L, oldn);
            send_msg(g_qid_send, SHOW_MSG, ret.c_str());
            return 0;
        }
    } else {
        std::string loadstr = "function dlua_set_val(";
        for (auto it = inputval.begin(); it != inputval.end();) {
            loadstr = loadstr + it->first;
            it++;
            if (it != inputval.end()) {
                loadstr = loadstr + ",";
            }
        }
        loadstr = loadstr + ")\n" + val + "=" + input + "\n";
        loadstr = loadstr + "return ";
        for (auto it = inputval.begin(); it != inputval.end();) {
            loadstr = loadstr + it->first;
            it++;
            if (it != inputval.end()) {
                loadstr = loadstr + ",";
            }
        }
        loadstr = loadstr + "\n end\n";
        DLOG("process_set_command p %s", loadstr.c_str());
        int status = luaL_dostring(L, loadstr.c_str());
        if (status != 0) {
            std::string ret = lua_tostring(L, -1);
            lua_settop(L, oldn);
            send_msg(g_qid_send, SHOW_MSG, ret.c_str());
            return 0;
        }

        lua_settop(L, oldn);

        lua_getglobal(L, "dlua_set_val");
        if (!lua_isfunction(L, -1)) {
            lua_settop(L, oldn);
            send_msg(g_qid_send, SHOW_MSG, "get dlua_set_val fail\n");
            return 0;
        }

        for (auto it = inputval.begin(); it != inputval.end(); it++) {
            if (!find_and_push_val(L, it->first, g_step_debug_level)) {
                lua_settop(L, oldn);
                send_msg(g_qid_send, SHOW_MSG, (std::string("can not find val ") + it->first).c_str());
                return 0;
            }
        }

        DLOG("process_set_command oldn %d", oldn);
        int ret = lua_pcall(L, inputval.size(), inputval.size(), 0);
        if (ret != 0) {
            std::string ret = lua_tostring(L, -1);
            lua_settop(L, oldn);
            send_msg(g_qid_send, SHOW_MSG, ret.c_str());
            return 0;
        }
        DLOG("process_set_command newn %d", lua_gettop(L));
        DLOG("process_set_command top %d", lua_isinteger(L, -1) > 0 ? lua_tointeger(L, -1) : -2020);

        int index = -inputval.size();
        for (auto it = inputval.begin(); it != inputval.end(); it++) {
            std::string name = it->first;
            int curoldn = lua_gettop(L);

            lua_getglobal(L, "dlua_setvarvalue");
            if (!lua_isfunction(L, -1)) {
                lua_settop(L, oldn);
                send_msg(g_qid_send, SHOW_MSG, "get dlua_setvarvalue fail\n");
                return 0;
            }

            lua_pushstring(L, name.c_str());
            lua_pushinteger(L, g_step_debug_level);
            lua_pushnil(L);
            lua_pushinteger(L, 2);
            lua_copy(L, index - 5, -2);

            DLOG("process_set_command calln %d", lua_gettop(L));
            DLOG("process_set_command param1 %s", lua_isstring(L, -4) > 0 ? lua_tostring(L, -4) : "-2020");
            DLOG("process_set_command param2 %d", lua_isinteger(L, -3) > 0 ? lua_tointeger(L, -3) : -2020);
            DLOG("process_set_command param3 %d", lua_isinteger(L, -2) > 0 ? lua_tointeger(L, -2) : -2020);
            DLOG("process_set_command param4 %d", lua_isinteger(L, -1) > 0 ? lua_tointeger(L, -1) : -2020);

            ret = lua_pcall(L, 4, 1, 0);
            if (ret != 0) {
                std::string ret = lua_tostring(L, -1);
                lua_settop(L, oldn);
                send_msg(g_qid_send, SHOW_MSG, ret.c_str());
                return 0;
            }

            bool suc = lua_toboolean(L, -1);
            if (!suc) {
                lua_settop(L, oldn);
                send_msg(g_qid_send, SHOW_MSG, (std::string("dlua_setvarvalue set ") + name + " fail").c_str());
                return 0;
            }

            DLOG("process_set_command set back %s %d", name.c_str(), index);

            lua_settop(L, curoldn);
            index++;
        }
    }

    DLOG("luaL_dostring ret ok");

    send_msg(g_qid_send, SHOW_MSG, "set ok");
    lua_settop(L, oldn);

    return 0;
}

int process_r_command(lua_State *L, const std::vector <std::string> &result, char data[QUEUED_MESSAGE_MSG_LEN]) {
    if (result.size() < 2) {
        send_msg(g_qid_send, SHOW_MSG, "r need param\n");
        return 0;
    }

    if (g_step == 0) {
        send_msg(g_qid_send, SHOW_MSG, "need in step mode\n");
        return 0;
    }

    std::string input = data;
    input.erase(0, input.find_first_not_of("r"));
    input.erase(0, input.find_first_not_of(" "));
    input.erase(input.find_last_not_of(" ") + 1);

    lua_Debug entry;
    memset(&entry, 0, sizeof(entry));
    if (lua_getstack(L, g_step_debug_level, &entry) <= 0) {
        return 0;
    }
    int status = lua_getinfo(L, "Sln", &entry);
    if (status <= 0) {
        return 0;
    }

    int oldn = lua_gettop(L);

    if (input[0] == '[') {
        // r [a] print(a.t)
        std::string leftstr;
        std::map<std::string, int> inputval;
        if (get_input_val(input, inputval, leftstr) != 0) {
            send_msg(g_qid_send, SHOW_MSG, "eg: p [t] t.a");
            return 0;
        }

        std::string loadstr = "function dlua_r_val(";
        for (auto it = inputval.begin(); it != inputval.end();) {
            loadstr = loadstr + it->first;
            it++;
            if (it != inputval.end()) {
                loadstr = loadstr + ",";
            }
        }
        loadstr = loadstr + ")\n  " + leftstr + "\n end\n";
        DLOG("process_r_command p %s", loadstr.c_str());
        int status = luaL_dostring(L, loadstr.c_str());
        if (status != 0) {
            std::string ret = lua_tostring(L, -1);
            lua_settop(L, oldn);
            send_msg(g_qid_send, SHOW_MSG, ret.c_str());
            return 0;
        }

        lua_getglobal(L, "dlua_r_val");
        if (!lua_isfunction(L, -1)) {
            lua_settop(L, oldn);
            send_msg(g_qid_send, SHOW_MSG, "get dlua_r_val fail\n");
            return 0;
        }

        for (auto it = inputval.begin(); it != inputval.end(); it++) {
            if (!find_and_push_val(L, it->first, g_step_debug_level)) {
                lua_settop(L, oldn);
                send_msg(g_qid_send, SHOW_MSG, (std::string("can not find val ") + it->first).c_str());
                return 0;
            }
        }

        int ret = lua_pcall(L, inputval.size(), 0, 0);
        if (ret != 0) {
            std::string ret = lua_tostring(L, -1);
            lua_settop(L, oldn);
            send_msg(g_qid_send, SHOW_MSG, ret.c_str());
            return 0;
        }
        lua_settop(L, oldn);
    } else {
        std::string loadstr = "function dlua_r_val(";
        loadstr = loadstr + ")\n  " + input + "\n end\n";
        DLOG("process_r_command p %s", loadstr.c_str());

        int status = luaL_dostring(L, loadstr.c_str());
        if (status != 0) {
            std::string ret = lua_tostring(L, -1);
            lua_settop(L, oldn);
            send_msg(g_qid_send, SHOW_MSG, ret.c_str());
            return 0;
        }

        lua_getglobal(L, "dlua_r_val");
        if (!lua_isfunction(L, -1)) {
            lua_settop(L, oldn);
            send_msg(g_qid_send, SHOW_MSG, "get dlua_r_val fail\n");
            return 0;
        }

        int ret = lua_pcall(L, 0, 0, 0);
        if (ret != 0) {
            std::string ret = lua_tostring(L, -1);
            lua_settop(L, oldn);
            send_msg(g_qid_send, SHOW_MSG, ret.c_str());
            return 0;
        }
        lua_settop(L, oldn);
    }

    return 0;
}

int process_command(lua_State *L, long type, char data[QUEUED_MESSAGE_MSG_LEN]) {
    std::string command = data;
    if (command == "") {
        return 0;
    }
    std::vector <std::string> result;
    std::istringstream iss(command);
    for (std::string s; iss >> s;) {
        result.push_back(s);
    }
    if (result.empty()) {
        return 0;
    }
    std::string token = result[0];
    int ret = 0;

    if (token == "h") {
        ret = process_help_command();
    } else if (token == "bt") {
        ret = process_bt_command(L);
    } else if (token == "b") {
        ret = process_b_command(L, result, data);
    } else if (token == "i") {
        ret = process_i_command(L, result);
    } else if (token == "n") {
        ret = process_n_command(L);
    } else if (token == "s") {
        ret = process_s_command(L);
    } else if (token == "c") {
        ret = process_c_command(L);
    } else if (token == "dis") {
        ret = process_dis_command(L, result);
    } else if (token == "en") {
        ret = process_en_command(L, result);
    } else if (token == "d") {
        ret = process_d_command(L, result);
    } else if (token == "p") {
        ret = process_p_command(L, result, data);
    } else if (token == "l") {
        ret = process_l_command(L, result);
    } else if (token == "f") {
        ret = process_f_command(L, result);
    } else if (token == "set") {
        ret = process_set_command(L, result, data);
    } else if (token == "fin") {
        ret = process_fin_command(L);
    } else if (token == "r") {
        ret = process_r_command(L, result, data);
    } else {
        send_msg(g_qid_send, SHOW_MSG, "use h for help\n");
    }

    if (g_step != 0 && g_step_next_in == 0 && g_step_next == 0 && g_step_next_out == 0) {
        send_msg(g_qid_send, INPUT_MSG, "");
    }

    return ret;
}

int process_msg(lua_State *L, long type, char data[QUEUED_MESSAGE_MSG_LEN]) {
    if (type == COMMAND_MSG) {
        return process_command(L, type, data);
    }
    return 0;
}

int loop_recv(lua_State *L) {
    char msg[QUEUED_MESSAGE_MSG_LEN] = {0};
    long msgtype = 0;

    while (1) {
        if (recv_msg(g_qid_recv, msgtype, msg) != 0) {
            return -1;
        }

        if (msgtype == 0) {
            break;
        } else {
            if (process_msg(L, msgtype, msg) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

int check_bp(lua_State *L) {
    lua_Debug entry;
    memset(&entry, 0, sizeof(entry));
    if (lua_getstack(L, 0, &entry) <= 0) {
        return 0;
    }
    int status = lua_getinfo(L, "Sln", &entry);
    if (status <= 0) {
        return 0;
    }
    std::string curfile = entry.short_src;
    int curline = entry.currentline;

    int bindex = -1;

    for (int i = 0; i < g_blist.size(); ++i) {
        if (g_blist[i].en && g_blist[i].file == curfile && g_blist[i].line == curline) {
            bool hit = false;
            if (g_blist[i].iffunc != "") {
                int oldn = lua_gettop(L);

                bool ini_ok = true;
                lua_getglobal(L, g_blist[i].iffunc.c_str());
                if (lua_isfunction(L, -1)) {
                    for (int j = 0; j < g_blist[i].iffuncparam.size(); ++j) {
                        if (!find_and_push_val(L, g_blist[i].iffuncparam[j], 0)) {
                            ini_ok = false;
                            break;
                        }
                    }
                } else {
                    ini_ok = false;
                }

                if (ini_ok) {
                    lua_pcall(L, g_blist[i].iffuncparam.size(), 1, 0);

                    int newn = lua_gettop(L);
                    if (newn > oldn) {
                        if (lua_isstring(L, -1)) {
                            std::string ret = lua_tostring(L, -1);
                            send_msg(g_qid_send, SHOW_MSG,
                                     (std::string("breakpoint if val return str ") + ret).c_str());
                            hit = true;
                        } else if (lua_isboolean(L, -1)) {
                            int ret = lua_toboolean(L, -1);
                            DLOG("b if return %s %d", g_blist[i].ifstr.c_str(), ret);
                            if (ret) {
                                hit = true;
                            }
                        } else {
                            int tp = lua_type(L, -1);
                            const char *tyname = lua_typename(L, tp);
                            send_msg(g_qid_send, SHOW_MSG,
                                     (std::string("breakpoint if val type is ") + tyname).c_str());
                            hit = true;
                        }
                    } else {
                        send_msg(g_qid_send, SHOW_MSG, "breakpoint if val no return");
                        hit = true;
                    }
                }
                lua_settop(L, oldn);
            } else {
                hit = true;
            }

            if (hit) {
                g_blist[i].hit++;
                bindex = i;
                g_step = 1;
                g_step_next = 0;
                g_step_next_in = 0;
                g_step_next_out = 0;
            }
            break;
        }
    }

    std::string curfunc = entry.name ? entry.name : "?";
    int curlevel = lastlevel(L);

    std::string what = entry.what;
    if (what == "C") {
        return 0;
    }

    if (bindex != -1) {
        // Breakpoint 1, lua_pcallk (L=0x7f772b029008, nargs=2, nresults=1, errfunc=<optimized out>, ctx=0, k=0x0) at lapi.c:968
        std::string ret;
        char buff[128] = {0};
        snprintf(buff, sizeof(buff) - 1, "Breakpoint %d, %s at %s:%d\n", g_blist[bindex].no, curfunc.c_str(),
                 g_blist[bindex].file.c_str(), g_blist[bindex].line);
        ret = buff;
        send_msg(g_qid_send, SHOW_MSG, ret.c_str());
    }

    if (g_step == 1) {
        bool need_stop = false;

        if (g_step_next == 0 && g_step_next_in == 0 && g_step_next_out == 0) {
            need_stop = true;
        } else if (g_step_next != 0) {
            if (curlevel > g_step_last_level || (curfile == g_step_last_file && curline == g_step_last_line)) {
            } else {
                need_stop = true;
            }
        } else if (g_step_next_in != 0) {
            if (curfile == g_step_last_file && curline == g_step_last_line) {
            } else {
                need_stop = true;
            }
        } else if (g_step_next_out != 0) {
            if (curlevel >= g_step_last_level) {
            } else {
                need_stop = true;
            }
        }

        if (need_stop) {

            if (curfunc != g_step_last_func) {
                // usage () at main.cpp:15
                std::string ret;
                char buff[128] = {0};
                snprintf(buff, sizeof(buff) - 1, "%s at %s:%d\n", curfunc.c_str(), curfile.c_str(), curline);
                ret = buff;
                send_msg(g_qid_send, SHOW_MSG, ret.c_str());
            }

            // 968     in lapi.c
            std::string ret;
            char buff[128] = {0};
            std::string code = get_file_code(curfile, curline);
            snprintf(buff, sizeof(buff) - 1, "%d %s\n", curline, code.c_str());
            ret = buff;
            send_msg(g_qid_send, SHOW_MSG, ret.c_str());

            g_step_last_file = curfile;
            g_step_last_line = curline;
            g_step_last_func = curfunc;
            g_step_last_level = curlevel;

            g_step_next = 0;
            g_step_next_in = 0;
            g_step_next_out = 0;
            send_msg(g_qid_send, INPUT_MSG, "");

            while (1) {
                if (loop_recv(L) != 0) {
                    DERR("loop_recv fail");
                    stop_agent();
                    return -1;
                }
                if (g_step == 0 || g_step_next != 0 || g_step_next_in != 0 || g_step_next_out != 0 ||
                    g_opening != RUNNING_STATE_RUNNING) {
                    break;
                }
                usleep(100);
            }
        }
    }

    return 0;
}

void hook_handler(lua_State *L, lua_Debug *par) {
    if (par->event != LUA_HOOKLINE) {
        DERR("diff event %d", par->event);
        return;
    }

    if (g_opening == RUNNING_STATE_BEGIN) {
        if (ini_agent() != 0) {
            return;
        }
        g_opening = RUNNING_STATE_RUNNING;
        return;
    } else if (g_opening == RUNNING_STATE_END) {
        if (fini_agent() != 0) {
            return;
        }
        g_opening = RUNNING_STATE_STOP;
        return;
    } else if (g_opening == RUNNING_STATE_STOP) {
        DERR("hook_handler fail");
        return;
    }

    if (loop_recv(L) != 0) {
        DERR("loop_recv fail");
        stop_agent();
        return;
    }

    if (check_bp(L) != 0) {
        DERR("check_bp fail");
        stop_agent();
        return;
    }
}

extern "C" int start_agent(lua_State *L, int pid) {

    if (g_opening != RUNNING_STATE_STOP) {
        return -1;
    }
    g_opening = RUNNING_STATE_BEGIN;

    g_pid = pid;
    g_L = L;

    g_old_hook = lua_gethook(L);
    g_old_hook_mask = lua_gethookcount(L);
    g_old_hook_count = lua_gethookmask(L);

    lua_sethook(L, hook_handler, LUA_MASKLINE, 0);

    return 0;
}
