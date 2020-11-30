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

extern "C" int stop_agent() {

    if (g_opening == RUNNING_STATE_STOP) {
        return -1;
    }
    g_opening = RUNNING_STATE_END;

    return 0;
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
                      "i\tshow info, eg: i b\n";
    send_msg(g_qid_send, SHOW_MSG, ret.c_str());
    return 0;
}

int process_bt_command(lua_State *L, lua_Debug *ar) {
    lua_Debug entry;
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

int process_b_command(lua_State *L, lua_Debug *ar, const std::vector <std::string> &result) {
    if (result.size() < 2) {
        send_msg(g_qid_send, SHOW_MSG, "breakpoint need file:line\n");
        return 0;
    }
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
        send_msg(g_qid_send, SHOW_MSG, "breakpoint need file:line\n");
        return 0;
    }

    std::string file = tokens[0];
    std::string linestr = tokens[1];
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

int process_i_command(lua_State *L, lua_Debug *ar, const std::vector <std::string> &result) {
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
    }

    return 0;
}

int process_command(lua_State *L, lua_Debug *ar, long type, char data[QUEUED_MESSAGE_MSG_LEN]) {
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

    if (token == "h") {
        return process_help_command();
    } else if (token == "bt") {
        return process_bt_command(L, ar);
    } else if (token == "b") {
        return process_b_command(L, ar, result);
    } else if (token == "i") {
        return process_i_command(L, ar, result);
    }
    return 0;
}

int process_msg(lua_State *L, lua_Debug *ar, long type, char data[QUEUED_MESSAGE_MSG_LEN]) {
    if (type == COMMAND_MSG) {
        return process_command(L, ar, type, data);
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

    lua_Debug ar;
    ar.source = 0;
    int ret = lua_getstack(L, 0, &ar);
    if (ret == 0) {
        DERR("lua_getstack fail %d", ret);
        return;
    }

    char msg[QUEUED_MESSAGE_MSG_LEN] = {0};
    long msgtype = 0;

    while (1) {
        if (recv_msg(g_qid_recv, msgtype, msg) != 0) {
            DERR("recv_msg fail");
            stop_agent();
            return;
        }

        if (msgtype == 0) {
            break;
        } else {
            if (process_msg(L, &ar, msgtype, msg) != 0) {
                DERR("process_msg fail");
                stop_agent();
                return;
            }
        }
    }

    ret = lua_getinfo(L, "S", &ar);
    if (ret == 0) {
        DERR("lua_getinfo fail %d", ret);
        return;
    }
    if (ar.source == 0) {
        DERR("source nil ");
        return;
    }
    if (ar.source[0] != '@') {
        DERR("source error %s ", ar.source);
        return;
    }

    char buff[128] = {0};
    snprintf(buff, sizeof(buff) - 1, "%d", par->currentline);
    std::string d = ar.source;
    d = d + ":";
    d = d + buff;
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
