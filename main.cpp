#include "common.h"

int g_qid_recv;
int g_qid_send;
int g_pid;
std::string g_sohandle = "";
std::string g_validfuncaddrvalue = "";
std::string g_lstr = "";
const int MAX_FIND_WAIT_SECONDS = 20;
const int MAX_CONN_WAIT_SECONDS = 10;
int g_int;
std::string g_command = "";
std::string g_last_command = "";
int g_quit;
const std::vector <std::string> g_lua_hook_func = {"luaV_execute", "luaD_call", "luaD_precall"};
const int MAX_TRIGGER_DLOPEN_SECONDS = 5;
const int MAX_TRIGGER_DLCLOSE_SECONDS = 5;
const int MAX_TRIGGER_ARG_SECONDS = 1;

int usage() {
    printf("dlua: pid\n");
    return -1;
}

std::string find_func_addr(std::string hook_func) {
    std::string funcaddrvalue = "";

    int out = 0;
    char cmd[256] = {0};
    snprintf(cmd, sizeof(cmd), "gdb -p %d -ex \"p (long)%s\" --batch", g_pid, hook_func.c_str());
    std::string ret = exec_command(cmd, out);
    if (out != 0) {
        DLOG("exec_command fail pid %d command %s", g_pid, cmd);
        return funcaddrvalue;
    }

    std::string key = "$1 = ";
    int left = ret.find(key);
    if (left == std::string::npos) {
        DLOG("exec_command ret %s no keyword", ret.c_str());
        return funcaddrvalue;
    }
    ret = ret.substr(left);
    int right = ret.find("\n");
    if (right == std::string::npos) {
        ret = ret.substr(key.length());
    } else {
        ret = ret.substr(key.length(), right - key.length());
    }

    ret.erase(std::remove(ret.begin(), ret.end(), '\n'), ret.end());
    ret.erase(std::remove(ret.begin(), ret.end(), ' '), ret.end());
    DLOG("func addr ret %s", ret.c_str());

    funcaddrvalue = ret;

    return funcaddrvalue;
}

long long find_luastate_breakpoint(std::string hook_func, std::string &validfuncaddrvalue) {
    DLOG("find_luastate_breakpoint start %d", g_pid);

    // get hook_func func addr
    std::string funcaddrvalue = find_func_addr(hook_func);

    long long lvalue = -1;
    // get hook_func func param
    {
        int out = 0;
        char cmd[256] = {0};
        snprintf(cmd, sizeof(cmd), "timeout %d ./hookso argp %d %s 1", MAX_TRIGGER_ARG_SECONDS, g_pid,
                 funcaddrvalue.c_str());
        std::string ret = exec_command(cmd, out);
        if (out != 0) {
            DLOG("exec_command fail pid %d command %s", g_pid, cmd);
            return -1;
        }
        ret.erase(std::remove(ret.begin(), ret.end(), '\n'), ret.end());
        DLOG("exec_command hookso call ret %s", ret.c_str());

        lvalue = strtoll(ret.c_str(), NULL, 10);
        validfuncaddrvalue = funcaddrvalue;
    }

    return lvalue;
}

long long find_luastate(std::string &validfuncaddrvalue) {
    long long ret = -1;
    validfuncaddrvalue = "";
    for (int i = 0; i < g_lua_hook_func.size(); ++i) {
        if (ret == -1) {
            ret = find_luastate_breakpoint(g_lua_hook_func[i], validfuncaddrvalue);
        }
    }
    return ret;
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
        snprintf(cmd, sizeof(cmd), "./hookso call %d dluaagent.so stop_agent", g_pid);
        std::string ret = exec_command(cmd, out);
        if (out != 0) {
            DERR("exec_command fail pid %d command %s", g_pid, cmd);
            return -1;
        }
        ret.erase(std::remove(ret.begin(), ret.end(), '\n'), ret.end());
        DLOG("exec_command hookso stop_agent call ret %s", ret.c_str());
    }

    // wait agent stop
    while (1) {
        int out = 0;
        char cmd[256] = {0};
        snprintf(cmd, sizeof(cmd), "./hookso call %d dluaagent.so is_stop_agent", g_pid);
        std::string ret = exec_command(cmd, out);
        if (out != 0) {
            DERR("exec_command fail pid %d command %s", g_pid, cmd);
            return -1;
        }
        ret.erase(std::remove(ret.begin(), ret.end(), '\n'), ret.end());
        DLOG("exec_command hookso is_stop_agent call ret %s", ret.c_str());
        if (ret == "1") {
            break;
        }
    }

    // close so
    {
        int out = 0;
        char cmd[256] = {0};
        snprintf(cmd, sizeof(cmd), "timeout %d ./hookso triggerp %d %s dlclose %s", MAX_TRIGGER_DLCLOSE_SECONDS, g_pid,
                 g_validfuncaddrvalue.c_str(), g_sohandle.c_str());
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
    signal(SIGINT, SIG_IGN);
    g_int = 1;
    signal(SIGINT, int_handler);
}

int init_env() {
    DLOG("init_env start %d", g_pid);

    // get lua state
    long long lvalue = -1;
    int last = time(0);
    std::string validfuncaddrvalue = "";
    while (time(0) - last < MAX_FIND_WAIT_SECONDS) {
        lvalue = find_luastate(validfuncaddrvalue);
        if (lvalue == -1 || validfuncaddrvalue == "") {
            usleep(100);
        } else {
            break;
        }
    }

    if (lvalue == -1 || validfuncaddrvalue == "") {
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
        snprintf(cmd, sizeof(cmd), "timeout %d ./hookso triggerp %d %s dlopen ./dluaagent.so",
                 MAX_TRIGGER_DLCLOSE_SECONDS, g_pid, validfuncaddrvalue.c_str());
        std::string ret = exec_command(cmd, out);
        if (out != 0) {
            DERR("exec_command fail pid %d command %s", g_pid, cmd);
            return -1;
        }
        ret.erase(std::remove(ret.begin(), ret.end(), '\n'), ret.end());
        g_sohandle = ret;
        g_validfuncaddrvalue = validfuncaddrvalue;
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
        std::string tmp = data;
        if (tmp != "") {
            if (tmp[tmp.length() - 1] != '\n') {
                tmp += "\n";
            }
            printf("%s", tmp.c_str());
        }
    } else if (type == INPUT_MSG) {
        g_int = 1;
    }
    return 0;
}

int get_command() {
    signal(SIGINT, SIG_IGN);
    char *command = readline("\n(dlua) ");
    signal(SIGINT, int_handler);
    if (!command) {
        return 0;
    }

    int oldpos = history_search_pos(command, 1, 0);
    if (oldpos < 0) {
        add_history(command);
    } else {
        remove_history(oldpos);
        add_history(command);
    }

    g_command = command;
    free(command);
    if (g_command.size() == 0) {
        g_command = g_last_command;
        return 0;
    }
    g_last_command = g_command;
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
    printf("attack to %d ok, use ctrl+c to input command, eg: h\n", g_pid);

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
            usleep(1);
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
