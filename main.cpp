#include "common.h"

int g_qid_recv;
int g_qid_send;

int usage() {
    printf("dlua: pid\n");
    return -1;
}

int init_env(int pid) {
    DLOG("init_env start %d", pid);

    // get lua state
    {
        int out = 0;
        char cmd[256] = {0};
        snprintf(cmd, sizeof(cmd), "pstack %d | grep \"in luaV_execute\" | grep \"=0x[0-9abcdef]*\" -o", pid);
        std::string ret = exec_command(cmd, out);
        if (out != 0) {
            DERR("exec_command fail pid %d command %s", pid, cmd);
            return -1;
        }
        ret.erase(std::remove(ret.begin(), ret.end(), '\n'), ret.end());
        ret.erase(std::remove(ret.begin(), ret.end(), '='), ret.end());
        DLOG("exec_command pstack ret %s", ret.c_str());
        if (ret.length() < 2 || ret.substr(0, 2) != "0x") {
            DERR("can not find pid %d lua state", pid);
            return -1;
        }
        std::string lstroct = ret.substr(2);
        long long lvalue = strtoll(lstroct.c_str(), NULL, 16);
        std::string lstr = std::to_string(lvalue);
        DLOG("lstr %s", lstr.c_str());
    }

    // inject so
    {
        int out = 0;
        char cmd[256] = {0};
        snprintf(cmd, sizeof(cmd), "./hookso dlopen %d ./dluaagent.so", pid);
        std::string ret = exec_command(cmd, out);
        if (out != 0) {
            DERR("exec_command fail pid %d command %s", pid, cmd);
            return -1;
        }
        ret.erase(std::remove(ret.begin(), ret.end(), '\n'), ret.end());
        DLOG("exec_command hookso dlopen ret %s", ret.c_str());
    }

    return 0;
}

int fini_env() {
    DLOG("fini_env start");
    msgctl(g_qid_send, IPC_RMID, 0);
    msgctl(g_qid_recv, IPC_RMID, 0);

    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage();
        exit(-1);
    }

    std::string pidstr = argv[1];
    int pid = atoi(pidstr.c_str());

    DLOG("start main %d", pid);

    const char *tmpfilename1 = "/tmp/dlua1.tmp";
    const char *tmpfilename2 = "/tmp/dlua2.tmp";
    int qid1 = open_msg_queue(tmpfilename1, pid);
    if (qid1 < 0) {
        DERR("open_msg_queue fail");
        exit(-1);
    }
    int qid2 = open_msg_queue(tmpfilename2, pid);
    if (qid2 < 0) {
        DERR("open_msg_queue fail");
        exit(-1);
    }
    g_qid_send = qid1;
    g_qid_recv = qid2;

    if (init_env(pid) != 0) {
        DERR("init_env fail");
        exit(-1);
    }

    if (fini_env() != 0) {
        DERR("fini_env fail");
        exit(-1);
    }

    return 0;
}
