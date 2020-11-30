#include "common.h"

int g_qid_recv;
int g_qid_send;
int g_pid;
std::string g_sohandle = "";
std::string g_lstr = "";
const int MAX_FIND_WAIT_SECONDS = 10;
const int MAX_CONN_WAIT_SECONDS = 10;
int g_int;
std::string g_command = "";
int g_quit;

int usage() {
    printf("dlua: pid\n");
    return -1;
}

long long find_luastate() {
    DLOG("find_luastate start %d", g_pid);

    int out = 0;
    char cmd[256] = {0};
    snprintf(cmd, sizeof(cmd), "pstack %d | grep \"in luaV_execute\" | grep \"=0x[0-9abcdef]*\" -o", g_pid);
    std::string ret = exec_command(cmd, out);
    if (out != 0) {
        DLOG("exec_command fail pid %d command %s", g_pid, cmd);
        return -1;
    }
    ret.erase(std::remove(ret.begin(), ret.end(), '\n'), ret.end());
    ret.erase(std::remove(ret.begin(), ret.end(), '='), ret.end());
    DLOG("exec_command pstack ret %s", ret.c_str());
    if (ret.length() < 2 || ret.substr(0, 2) != "0x") {
        DLOG("can not find pid %d lua state", g_pid);
        return -1;
    }
    std::string lstroct = ret.substr(2);
    long long lvalue = strtoll(lstroct.c_str(), NULL, 16);

    return lvalue;
}

int fini_env() {
    DLOG("fini_env start");
    msgctl(g_qid_send, IPC_RMID, 0);
    g_qid_send = 0;
    msgctl(g_qid_recv, IPC_RMID, 0);
    g_qid_recv = 0;

    // close agent
    {
        int out = 0;
        char cmd[256] = {0};
        snprintf(cmd, sizeof(cmd), "./hookso call %d dluaagent.so stop_agent i=%s i=%d", g_pid, g_lstr.c_str(), g_pid);
        std::string ret = exec_command(cmd, out);
        if (out != 0) {
            DERR("exec_command fail pid %d command %s", g_pid, cmd);
            return -1;
        }
        ret.erase(std::remove(ret.begin(), ret.end(), '\n'), ret.end());
        DLOG("exec_command hookso call ret %s", ret.c_str());
    }

    // close so
    {
        int out = 0;
        char cmd[256] = {0};
        snprintf(cmd, sizeof(cmd), "./hookso dlclose %d %s", g_pid, g_sohandle.c_str());
        std::string ret = exec_command(cmd, out);
        if (out != 0) {
            DERR("exec_command fail pid %d command %s", g_pid, cmd);
            return -1;
        }
        ret.erase(std::remove(ret.begin(), ret.end(), '\n'), ret.end());
        DLOG("exec_command hookso dlclose ret %s", ret.c_str());
    }

    return 0;
}

void int_handler(int sig) {
    signal(sig, SIG_IGN);
    g_int = 1;
    signal(SIGINT, int_handler);
}

int init_env() {
    DLOG("init_env start %d", g_pid);

    // get lua state
    long long lvalue = -1;
    int last = time(0);
    while (time(0) - last < MAX_FIND_WAIT_SECONDS) {
        lvalue = find_luastate();
        if (lvalue == -1) {
            usleep(100);
        } else {
            break;
        }
    }

    if (lvalue == -1) {
        DERR("can not find pid %d lua state", g_pid);
        return -1;
    }

    std::string lstr = std::to_string(lvalue);
    DLOG("lstr %s", lstr.c_str());
    g_lstr = lstr;

    signal(SIGINT, int_handler);

    // inject so
    {
        int out = 0;
        char cmd[256] = {0};
        snprintf(cmd, sizeof(cmd), "./hookso dlopen %d ./dluaagent.so", g_pid);
        std::string ret = exec_command(cmd, out);
        if (out != 0) {
            DERR("exec_command fail pid %d command %s", g_pid, cmd);
            return -1;
        }
        ret.erase(std::remove(ret.begin(), ret.end(), '\n'), ret.end());
        g_sohandle = ret;
        DLOG("exec_command hookso dlopen ret %s", ret.c_str());
    }

    // open agent
    {
        int out = 0;
        char cmd[256] = {0};
        snprintf(cmd, sizeof(cmd), "./hookso call %d dluaagent.so start_agent i=%s i=%d", g_pid, lstr.c_str(), g_pid);
        std::string ret = exec_command(cmd, out);
        if (out != 0) {
            DERR("exec_command fail pid %d command %s", g_pid, cmd);
            return -1;
        }
        ret.erase(std::remove(ret.begin(), ret.end(), '\n'), ret.end());
        DLOG("exec_command hookso call ret %s", ret.c_str());
    }

    return 0;
}

int process_msg(long type, char data[QUEUED_MESSAGE_MSG_LEN]) {
    if (type == SHOW_MSG) {
        printf(data);
    }
    return 0;
}

int get_command() {
    printf("\n(dlua) ");
    std::getline(std::cin, g_command);
    return 0;
}

int process_command() {
    if (g_command == "") {
        return 0;
    }
    std::vector <std::string> result;
    std::istringstream iss(g_command);
    for (std::string s; iss >> s;) {
        result.push_back(s);
    }
    if (result.empty()) {
        return 0;
    }
    std::string token = result[0];
    DLOG("command begin %s", token.c_str());

    if (token == "q") {
        g_quit = 1;
    } else {
        send_msg(g_qid_send, COMMAND_MSG, g_command.c_str());
    }

    g_command = "";

    return 0;
}

int process() {
    DLOG("process start");

    char msg[QUEUED_MESSAGE_MSG_LEN] = {0};
    long msgtype = 0;

    bool ini_ok = false;
    int last_time = time(0);
    while (time(0) - last_time < MAX_CONN_WAIT_SECONDS) {
        if (recv_msg(g_qid_recv, msgtype, msg) != 0) {
            break;
        }

        if (msgtype == LOGIN_MSG) {
            if (std::to_string(g_pid) == std::string(msg)) {
                ini_ok = true;
                break;
            }
        }
    }

    if (!ini_ok) {
        DERR("can not connect agent %d", g_pid);
        return -1;
    }

    DLOG("connect ok %d", g_pid);

    while (g_quit != 1) {
        if (g_int != 0) {
            g_int = 0;
            get_command();
            process_command();
        }

        if (recv_msg(g_qid_recv, msgtype, msg) != 0) {
            DERR("recv_msg fail");
            break;
        }

        if (msgtype == 0) {
            continue;
        } else {
            if (process_msg(msgtype, msg) != 0) {
                DERR("process_msg fail");
                break;
            }
        }
    }

    DLOG("disconnect from %d", g_pid);

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
    g_pid = pid;

    const char *tmpfilename1 = "/tmp/dlua1.tmp";
    const char *tmpfilename2 = "/tmp/dlua2.tmp";
    int qid1 = open_msg_queue(tmpfilename1, g_pid);
    if (qid1 < 0) {
        DERR("open_msg_queue fail");
        exit(-1);
    }
    int qid2 = open_msg_queue(tmpfilename2, g_pid);
    if (qid2 < 0) {
        DERR("open_msg_queue fail");
        exit(-1);
    }
    g_qid_send = qid1;
    g_qid_recv = qid2;

    if (init_env() != 0) {
        DERR("init_env fail");
        exit(-1);
    }

    if (process() != 0) {
        DERR("init_env fail");
        exit(-1);
    }

    if (fini_env() != 0) {
        DERR("fini_env fail");
        exit(-1);
    }

    return 0;
}
