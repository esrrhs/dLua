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

const int open_debug = 0;

#define DLOG(...) if (open_debug) {dlog(stdout, "[DEBUG] ", __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__);}
#define DERR(...)  {dlog(stderr, "[ERROR] ", __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__);}

void dlog(FILE *fd, const char *header, const char *file, const char *func, int pos, const char *fmt, ...) {
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

static int open_msg_queue(const char *tmpfilename, int pid) {

    int tmpfd = open(tmpfilename, O_RDWR | O_CREAT, 0777);
    if (tmpfd == -1) {
        DERR("open tmpfile fail %s %d %s", tmpfilename, errno, strerror(errno));
        return -1;
    }
    close(tmpfd);

    key_t key = ftok(tmpfilename, pid);
    if (key == -1) {
        DERR("ftok fail %s %d %d %s", tmpfilename, pid, errno, strerror(errno));
        return -1;
    }

    DLOG("ftok key %d", key);

    int qid = msgget(key, 0666 | IPC_CREAT);
    if (qid < 0) {
        DERR("msgget fail %d %d %s", key, errno, strerror(errno));
        return -1;
    }

    DLOG("qid %d", qid);

    return qid;
}

std::string exec_command(const char *cmd, int &out) {
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

#endif //DLUA_COMMON_H
