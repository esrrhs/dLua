#include "common.h"

int g_qid_recv;
int g_qid_send;

int usage() {
    printf("dlua: pid\n");
    return -1;
}

int init_env(int pid) {
    DLOG("init_env start %d", pid);

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
