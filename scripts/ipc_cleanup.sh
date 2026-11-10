#!/usr/bin/env bash
# Remove SysV message queues and /tmp path files for a dLua session pid.
# Usage: ipc_cleanup.sh PID
# Safe to call when queues or files are already gone.
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "usage: $0 PID" >&2
  exit 2
fi
PID="$1"

python3 - "$PID" << 'PY'
import ctypes, os, sys

pid = sys.argv[1]
libc = ctypes.CDLL("libc.so.6", use_errno=True)
libc.ftok.argtypes = [ctypes.c_char_p, ctypes.c_int]
libc.ftok.restype = ctypes.c_int
libc.msgget.argtypes = [ctypes.c_int, ctypes.c_int]
libc.msgget.restype = ctypes.c_int
libc.msgctl.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_void_p]
libc.msgctl.restype = ctypes.c_int
IPC_RMID = 0

def rm_one(path):
    if not os.path.exists(path):
        return
    key = libc.ftok(path.encode(), 1)
    if key != -1:
        qid = libc.msgget(key, 0)
        if qid >= 0:
            libc.msgctl(qid, IPC_RMID, None)
    try:
        os.unlink(path)
    except OSError:
        pass

for side in (1, 2):
    rm_one("/tmp/dlua-%s-%s" % (pid, side))
PY
