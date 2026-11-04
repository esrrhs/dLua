#ifndef DLUA_COMMON_H
#define DLUA_COMMON_H

#include <string>
#include <list>
#include <vector>
#include <map>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <typeinfo>
#include <time.h>
#include <stdarg.h>
#include <assert.h>
#include <math.h>
#include <sys/time.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <unordered_map>
#include <fcntl.h>
#include <sys/stat.h>
#include <sstream>
#include <algorithm>
#include <vector>
#include <unordered_set>
#include <set>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <array>
#include <limits.h>

const int open_debug = 0;

#define DLOG(...) if (open_debug) {dlog(stdout, "[DEBUG] ", __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__);}
#define DERR(...)  {dlog(stderr, "[ERROR] ", __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__);}

inline void dlog(FILE *fd, const char *header, const char *file, const char *func, int pos, const char *fmt, ...) {
    time_t clock1;
    struct tm *tptr;
    va_list ap;

    clock1 = time(0);
    tptr = localtime(&clock1);

    struct timeval tv;
    gettimeofday(&tv, NULL);

    fprintf(fd, "%s[%d.%d.%d,%d:%d:%d,%llu]%s:%d,%s: ", header,
            tptr->tm_year + 1900, tptr->tm_mon + 1,
            tptr->tm_mday, tptr->tm_hour, tptr->tm_min,
            tptr->tm_sec, (long long) ((tv.tv_usec) / 1000) % 1000, file, pos, func);

    va_start(ap, fmt);
    vfprintf(fd, fmt, ap);
    fprintf(fd, "\n");
    va_end(ap);
}

const int QUEUED_MESSAGE_MSG_LEN = 1023;
struct QueuedMessage {
    long type;
    char payload[QUEUED_MESSAGE_MSG_LEN + 1];
};

const int LOGIN_MSG = 1;
const int COMMAND_MSG = 2;
const int SHOW_MSG = 3;
const int INPUT_MSG = 4;

static int send_msg(int qid, long type, const char *data) {
    QueuedMessage msg;
    msg.type = type;
    strncpy(msg.payload, data, QUEUED_MESSAGE_MSG_LEN);
    msg.payload[QUEUED_MESSAGE_MSG_LEN] = 0;
    int msgsz = strlen(msg.payload);
    if (msgsnd(qid, &msg, msgsz, 0) != 0) {
        DERR("send_msg %d %d error %d %s", qid, msgsz, errno, strerror(errno));
        return -1;
    }
    return 0;
}

static int recv_msg(int qid, long &type, char data[QUEUED_MESSAGE_MSG_LEN]) {
    QueuedMessage msg;
    int size = msgrcv(qid, &msg, QUEUED_MESSAGE_MSG_LEN, 0, IPC_NOWAIT);
    if (size == -1) {
        if (errno == ENOMSG) {
            type = 0;
            data[0] = 0;
            return 0;
        }
        DERR("recv_msg %d error %d %s", qid, errno, strerror(errno));
        return -1;
    }
    type = msg.type;
    strncpy(data, msg.payload, QUEUED_MESSAGE_MSG_LEN);
    data[size] = 0;
    return 0;
}

// Build a per-session path. ftok() only uses the low 8 bits of proj_id, so
// embedding the full pid in the pathname is required for uniqueness.
inline std::string dlua_ipc_tmp_path(int pid, int side) {
    return std::string("/tmp/dlua-") + std::to_string(pid) + "-" + std::to_string(side);
}

// Idempotent remove of a message queue (single-owner cleanup helper).
static int remove_msg_queue(int qid) {
    if (qid < 0) {
        return 0;
    }
    if (msgctl(qid, IPC_RMID, 0) != 0) {
        if (errno == EINVAL || errno == EIDRM) {
            return 0;
        }
        DERR("msgctl IPC_RMID fail %d %d %s", qid, errno, strerror(errno));
        return -1;
    }
    return 0;
}

// Create or open a SysV message queue.
// proj_id: small constant (1..255); uniqueness comes from tmpfilename (includes pid).
// mode: typically 0600. Returns qid or -1.
static int open_msg_queue(const char *tmpfilename, int proj_id, int mode = 0600) {
    int tmpfd = open(tmpfilename, O_RDWR | O_CREAT, mode);
    if (tmpfd == -1) {
        DERR("open tmpfile fail %s %d %s", tmpfilename, errno, strerror(errno));
        return -1;
    }
    // Enforce permissions even if the file already existed with a wider mode.
    fchmod(tmpfd, mode);
    close(tmpfd);

    // Only low 8 bits of proj_id are used by ftok; keep it a tiny constant.
    int proj = proj_id & 0xff;
    if (proj == 0) {
        proj = 1;
    }
    key_t key = ftok(tmpfilename, proj);
    if (key == -1) {
        DERR("ftok fail %s %d %d %s", tmpfilename, proj, errno, strerror(errno));
        return -1;
    }

    DLOG("ftok key %d path %s", key, tmpfilename);

    int qid = msgget(key, mode | IPC_CREAT);
    if (qid < 0) {
        DERR("msgget fail %d %d %s", key, errno, strerror(errno));
        return -1;
    }

    DLOG("qid %d", qid);

    return qid;
}

inline std::string exec_command(const char *cmd, int &out) {
    out = 0;
    auto pPipe = ::popen(cmd, "r");
    if (pPipe == nullptr) {
        out = -1;
        return "";
    }

    std::array<char, 256> buffer;

    std::string result;

    while (not std::feof(pPipe)) {
        auto bytes = std::fread(buffer.data(), 1, buffer.size(), pPipe);
        result.append(buffer.data(), bytes);
    }

    auto rc = ::pclose(pPipe);

    if (WIFEXITED(rc)) {
        out = WEXITSTATUS(rc);
    }

    return result;
}

inline std::string exec_command(const std::string &cmd, int &out) {
    return exec_command(cmd.c_str(), out);
}

inline bool path_is_executable(const std::string &path) {
    if (path.empty()) {
        return false;
    }
    return access(path.c_str(), X_OK) == 0;
}

inline bool path_is_readable_file(const std::string &path) {
    if (path.empty()) {
        return false;
    }
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        return false;
    }
    if (!S_ISREG(st.st_mode)) {
        return false;
    }
    return access(path.c_str(), R_OK) == 0;
}

inline std::string dirname_of_path(const std::string &path) {
    size_t pos = path.find_last_of('/');
    if (pos == std::string::npos) {
        return ".";
    }
    if (pos == 0) {
        return "/";
    }
    return path.substr(0, pos);
}

inline std::string get_self_exe_path() {
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n < 0) {
        return "";
    }
    buf[n] = 0;
    return std::string(buf);
}

inline std::string make_absolute_path(const std::string &path) {
    if (path.empty()) {
        return path;
    }
    char *rp = realpath(path.c_str(), nullptr);
    if (rp) {
        std::string out(rp);
        free(rp);
        return out;
    }
    if (path[0] == '/') {
        return path;
    }
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) != nullptr) {
        return std::string(cwd) + "/" + path;
    }
    return path;
}

// Resolve hookso binary. Order:
// 1. $DLUA_HOOKSO if executable
// 2. dirname(/proc/self/exe)/hookso
// 3. ./hookso
// 4. hookso on PATH
// On failure, writes a clear stderr-oriented message into err listing where looked.
inline std::string resolve_hookso(std::string &err) {
    std::vector <std::string> looked;
    err.clear();

    const char *env = getenv("DLUA_HOOKSO");
    if (env != nullptr && env[0] != '\0') {
        looked.push_back(std::string("$DLUA_HOOKSO=") + env);
        if (path_is_executable(env)) {
            return make_absolute_path(env);
        }
    }

    std::string exe = get_self_exe_path();
    if (!exe.empty()) {
        std::string cand = dirname_of_path(exe) + "/hookso";
        looked.push_back(cand);
        if (path_is_executable(cand)) {
            return make_absolute_path(cand);
        }
    } else {
        looked.push_back("(dirname(/proc/self/exe)/hookso: /proc/self/exe unreadable)");
    }

    looked.push_back("./hookso");
    if (path_is_executable("./hookso")) {
        return make_absolute_path("./hookso");
    }

    const char *pathenv = getenv("PATH");
    if (pathenv != nullptr && pathenv[0] != '\0') {
        std::stringstream ss(pathenv);
        std::string dir;
        while (std::getline(ss, dir, ':')) {
            if (dir.empty()) {
                continue;
            }
            std::string cand = dir + "/hookso";
            looked.push_back(cand);
            if (path_is_executable(cand)) {
                return make_absolute_path(cand);
            }
        }
    } else {
        looked.push_back("hookso on PATH (PATH unset)");
    }

    err = "cannot find executable hookso; looked in:\n";
    for (size_t i = 0; i < looked.size(); ++i) {
        err += "  ";
        err += looked[i];
        err += "\n";
    }
    err += "Set DLUA_HOOKSO to an executable hookso, or place hookso next to dlua / on PATH.\n";
    return "";
}

// Resolve dluaagent.so for dlopen (absolute path). Order:
// 1. $DLUA_AGENT
// 2. dirname(exe)/dluaagent.so
// 3. ./dluaagent.so
// Post-inject hookso call still uses soname "dluaagent.so".
inline std::string resolve_dluaagent(std::string &err) {
    std::vector <std::string> looked;
    err.clear();

    const char *env = getenv("DLUA_AGENT");
    if (env != nullptr && env[0] != '\0') {
        looked.push_back(std::string("$DLUA_AGENT=") + env);
        if (path_is_readable_file(env)) {
            return make_absolute_path(env);
        }
    }

    std::string exe = get_self_exe_path();
    if (!exe.empty()) {
        std::string cand = dirname_of_path(exe) + "/dluaagent.so";
        looked.push_back(cand);
        if (path_is_readable_file(cand)) {
            return make_absolute_path(cand);
        }
    } else {
        looked.push_back("(dirname(/proc/self/exe)/dluaagent.so: /proc/self/exe unreadable)");
    }

    looked.push_back("./dluaagent.so");
    if (path_is_readable_file("./dluaagent.so")) {
        return make_absolute_path("./dluaagent.so");
    }

    err = "cannot find dluaagent.so; looked in:\n";
    for (size_t i = 0; i < looked.size(); ++i) {
        err += "  ";
        err += looked[i];
        err += "\n";
    }
    err += "Set DLUA_AGENT to the agent shared library path, or place dluaagent.so next to dlua.\n";
    return "";
}

#endif //DLUA_COMMON_H
