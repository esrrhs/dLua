#include "common.h"

extern "C" {
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}

int g_qid_send;
int g_qid_recv;

static void hook_handler(lua_State *L, lua_Debug *par) {
    if (par->event != LUA_HOOKLINE) {
        DERR("diff event %d", par->event);
        return;
    }

    lua_Debug ar;
    ar.source = 0;
    int ret = lua_getstack(L, 0, &ar);
    if (ret == 0) {
        DERR("lua_getstack fail %d", ret);
        return;
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

    send_msg(g_qid_send, HB_MSG, d.c_str());
}

extern "C" int start_agent(lua_State *L, int pid) {
    DLOG("start_agent start %d", pid);

    const char *tmpfilename1 = "/tmp/dlua1.tmp";
    const char *tmpfilename2 = "/tmp/dlua2.tmp";
    int qid1 = open_msg_queue(tmpfilename1, pid);
    if (qid1 < 0) {
        DERR("open_msg_queue fail");
        return -1;
    }
    int qid2 = open_msg_queue(tmpfilename2, pid);
    if (qid2 < 0) {
        DERR("open_msg_queue fail");
        return -1;
    }
    g_qid_send = qid2;
    g_qid_recv = qid1;

    lua_sethook(L, hook_handler, LUA_MASKLINE, 0);

    return 0;
}

extern "C" int stop_agent() {
    msgctl(g_qid_send, IPC_RMID, 0);
    msgctl(g_qid_recv, IPC_RMID, 0);
    return 0;
}
   