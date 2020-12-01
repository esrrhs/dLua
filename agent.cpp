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
                      "fin\tfinish current call\n";
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

int process_b_command(lua_State *L, const std::vector <std::string> &result) {
    std::string file;
    std::string linestr;

    if (result.size() < 2) {
        if (g_step != 0) {
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
        std::string token;
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
                lua_Debug entry;
                memset(&entry, 0, sizeof(entry));
                lua_getglobal(L, bpoint.c_str());  /* 取得全局变量 'f' */
                if (!lua_isfunction(L, -1)) {
                    lua_pop(L, 1);
                    send_msg(g_qid_send, SHOW_MSG, (std::string("can not find function ") + bpoint).c_str());
                    return 0;
                }
                int status = lua_getinfo(L, ">S", &entry);
                if (status <= 0) {
                    return 0;
                }
                file = entry.short_src;
                linestr = std::to_string(entry.linedefined + 1);
            }
        } else {
            file = tokens[0];
            linestr = tokens[1];
        }
    }

    DLOG("process_b_command start %s %s", file.c_str(), linestr.c_str());
    int line = atoi(linestr.c_str());

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

    std::string ret;
    char buff[128] = {0};
    // Breakpoint 2 at 0x41458c: file lapi.c, line 968.
    snprintf(buff, sizeof(buff) - 1, "Breakpoint %d at file %s, line %d\n", g_blist[bindex].no,
             g_blist[bindex].file.c_str(),
             g_blist[bindex].line);
    DLOG("process_b_command %d %s %d", g_blist[bindex].no, g_blist[bindex].file.c_str(), g_blist[bindex].line);
    ret = buff;
    send_msg(g_qid_send, SHOW_MSG, ret.c_str());

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
        std::string ret = "Num\tEnb\tWhat\t\tHit\n";
        char buff[128] = {0};
        for (int i = 0; i < g_blist.size(); ++i) {
            memset(buff, 0, sizeof(buff));
            snprintf(buff, sizeof(buff) - 1, "%d\t%s\t%s:%d\t\t%d\n", g_blist[i].no, g_blist[i].en ? "y" : "n",
                     g_blist[i].file.c_str(), g_blist[i].line, g_blist[i].hit);
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
    input.erase(0, input.find_first_not_of("p"));
    input.erase(0, input.find_first_not_of(" "));
    input.erase(input.find_last_not_of(" ") + 1);

    std::string param = input;

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

    const char *loadstr = "function dlua_tprint (tbl, indent, visit, path)\n"
                          "    if not indent then\n"
                          "        indent = 0\n"
                          "    end\n"
                          "    local ret = \"\"\n"
                          "    for k, v in pairs(tbl) do\n"
                          "        local formatting = string.rep(\"  \", indent) .. k .. \": \"\n"
                          "        if type(v) == \"table\" then\n"
                          "            if visit[v] then\n"
                          "                ret = ret .. formatting .. \"*Recursion at \" .. visit[v] .. \"*\\n\"\n"
                          "            else\n"
                          "                visit[v] = path .. \"/\" .. tostring(k)\n"
                          "                ret = ret .. formatting .. \"\\n\" .. dlua_tprint(v, indent + 1, visit, path .. \"/\" .. tostring(k))\n"
                          "            end\n"
                          "        elseif type(v) == 'boolean' then\n"
                          "            ret = ret .. formatting .. tostring(v) .. \"\\n\"\n"
                          "        elseif type(v) == 'string' then\n"
                          "            ret = ret .. formatting .. \"'\" .. tostring(v) .. \"'\" .. \"\\n\"\n"
                          "        else\n"
                          "            ret = ret .. formatting .. v .. \"\\n\"\n"
                          "        end\n"
                          "    end\n"
                          "    return ret\n"
                          "end\n"
                          "\n"
                          "function dlua_pprint (tbl)\n"
                          "    local path = \"\"\n"
                          "    local visit = {}\n"
                          "    if type(tbl) ~= \"table\" then\n"
                          "        return tostring(tbl)\n"
                          "    end\n"
                          "    return dlua_tprint(tbl, 0, visit, path)\n"
                          "end\n"
                          "return _G.dlua_pprint(\"ok\")";

    luaL_dostring(L, loadstr);
    std::string ret = lua_tostring(L, -1);
    lua_settop(L, oldn);
    DLOG("luaL_dostring ret %s", ret.c_str());
    lua_getglobal(L, "dlua_pprint");
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        send_msg(g_qid_send, SHOW_MSG, "get dlua_pprint fail\n");
        return 0;
    }

    bool find = false;
    int index = 1;
    while (1) {
        const char *name = lua_getlocal(L, &entry, index);
        if (!name) {
            break;
        }
        if (param == name) {
            find = true;
            DLOG("find local %s", name);
            break;
        } else {
            lua_pop(L, 1);
        }
        index++;
    }

    if (!find) {
        index = 1;
        while (1) {
            const char *name = lua_getupvalue(L, -1, index);
            if (!name) {
                break;
            }
            if (param == name) {
                find = true;
                DLOG("find upvalue %s", name);
                break;
            } else {
                lua_pop(L, 1);
            }
            index++;
        }
    }

    if (!find) {
        std::string tmp = "return dlua_pprint(" + param + ")";
        luaL_dostring(L, tmp.c_str());
        DLOG("call dlua_pprint %s", param.c_str());
    } else {
        lua_pcall(L, 1, 1, 0);
    }

    int newn = lua_gettop(L);
    if (newn > oldn) {
        std::string ret = lua_tostring(L, -1);
        lua_pop(L, 1);
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
    input.erase(0, input.find_first_not_of("p"));
    input.erase(0, input.find_first_not_of(" "));
    input.erase(input.find_last_not_of(" ") + 1);

    std::string param = input;

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
        ret = process_b_command(L, result);
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
            g_blist[i].hit++;
            bindex = i;
            g_step = 1;
            g_step_next = 0;
            g_step_next_in = 0;
            g_step_next_out = 0;
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
