#!/usr/bin/env bash
# Hookso integration scenarios (true process attach, not in-process unit tests):
#   - repeated attach/detach on a live lua process
#   - target early exit (SIGKILL) with bounded timeouts and IPC cleanup
#   - coroutine-heavy target stays alive across attach/detach
# Uses gdb for find_func_addr (same as dlua / smoke_attach.sh).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

LUA_BIN="${LUA_BIN:-lua}"
HOOKSO="${DLUA_HOOKSO:-}"
AGENT="${DLUA_AGENT:-}"
# Inner hookso calls use timeout(1). CI wraps this script in `timeout 90`.
die() { echo "smoke_scenarios: $*" >&2; exit 1; }

if [[ -z "$HOOKSO" ]]; then
  if [[ -x "$ROOT/hookso" ]]; then
    HOOKSO="$ROOT/hookso"
  elif command -v hookso >/dev/null 2>&1; then
    HOOKSO="$(command -v hookso)"
  else
    die "hookso not found (set DLUA_HOOKSO or place ./hookso)"
  fi
fi
[[ -x "$HOOKSO" ]] || die "hookso not executable: $HOOKSO"
HOOKSO="$(realpath "$HOOKSO")"

if [[ -z "$AGENT" ]]; then
  if [[ -f "$ROOT/dluaagent.so" ]]; then
    AGENT="$ROOT/dluaagent.so"
  else
    die "dluaagent.so not found (set DLUA_AGENT or build first)"
  fi
fi
[[ -f "$AGENT" ]] || die "agent missing: $AGENT"
AGENT="$(realpath "$AGENT")"

command -v gdb >/dev/null 2>&1 || die "gdb required for find_func_addr"
command -v "$LUA_BIN" >/dev/null 2>&1 || die "lua not found: $LUA_BIN"
[[ -x "$ROOT/scripts/ipc_cleanup.sh" ]] || die "missing scripts/ipc_cleanup.sh"

echo "smoke_scenarios: hookso=$HOOKSO"
echo "smoke_scenarios: agent=$AGENT"
echo "smoke_scenarios: lua=$LUA_BIN"

# --- helpers ---------------------------------------------------------------

start_lua() {
  local script="$1"
  "$LUA_BIN" "$script" &
  LUA_PID=$!
  sleep 0.5
  kill -0 "$LUA_PID" 2>/dev/null || die "lua process exited early ($script)"
}

stop_lua() {
  if [[ -n "${LUA_PID:-}" ]] && kill -0 "$LUA_PID" 2>/dev/null; then
    kill "$LUA_PID" 2>/dev/null || true
    wait "$LUA_PID" 2>/dev/null || true
  fi
  if [[ -n "${LUA_PID:-}" ]]; then
    "$ROOT/scripts/ipc_cleanup.sh" "$LUA_PID" >/dev/null 2>&1 || true
  fi
  LUA_PID=""
}

find_func_addr() {
  local func="$1"
  local out addr
  out="$(gdb -p "$LUA_PID" -ex "p (long)${func}" --batch 2>/dev/null || true)"
  addr="$(printf '%s\n' "$out" | sed -n 's/^\$1 = //p' | head -1 | tr -d '[:space:]')"
  printf '%s' "$addr"
}

# Locate a lua_State pointer the same way dlua/smoke_attach do.
locate_state() {
  FUNC_ADDR=""
  LUA_STATE=""
  local deadline addr lval func
  deadline=$((SECONDS + 20))
  while (( SECONDS < deadline )); do
    for func in luaV_execute luaD_call luaD_precall; do
      addr="$(find_func_addr "$func" || true)"
      if [[ -z "$addr" || "$addr" == "0" ]]; then
        continue
      fi
      lval="$(timeout 1 "$HOOKSO" argp "$LUA_PID" "$addr" 1 2>/dev/null | tr -d '[:space:]' || true)"
      if [[ -n "$lval" && "$lval" != "0" && "$lval" != "-1" ]]; then
        FUNC_ADDR="$addr"
        LUA_STATE="$lval"
        echo "smoke_scenarios: found $func addr=$FUNC_ADDR L=$LUA_STATE"
        return 0
      fi
    done
    sleep 0.1
  done
  return 1
}

inject_agent() {
  echo "smoke_scenarios: dlopen via triggerp"
  HANDLE="$(timeout 5 "$HOOKSO" triggerp "$LUA_PID" "$FUNC_ADDR" dlopen "$AGENT" | tr -d '[:space:]')"
  [[ -n "$HANDLE" && "$HANDLE" != "0" ]] || die "dlopen failed (handle='$HANDLE')"
  echo "smoke_scenarios: handle=$HANDLE"
  if grep -q 'dluaagent\.so' "/proc/${LUA_PID}/maps"; then
    echo "smoke_scenarios: maps contain dluaagent.so"
  else
    die "dluaagent.so not present in /proc/${LUA_PID}/maps after dlopen"
  fi
}

call_start_agent() {
  timeout 5 "$HOOKSO" call "$LUA_PID" dluaagent.so start_agent "i=${LUA_STATE}" "i=${LUA_PID}" >/dev/null \
    || die "start_agent timed out or failed"
}

call_stop_agent() {
  timeout 5 "$HOOKSO" call "$LUA_PID" dluaagent.so stop_agent >/dev/null \
    || die "stop_agent timed out or failed"
}

wait_stopped() {
  local deadline stopped=""
  deadline=$((SECONDS + 10))
  while (( SECONDS < deadline )); do
    stopped="$(timeout 5 "$HOOKSO" call "$LUA_PID" dluaagent.so is_stop_agent 2>/dev/null | tr -d '[:space:]' || true)"
    if [[ "$stopped" == "1" ]]; then
      echo "smoke_scenarios: agent stopped"
      return 0
    fi
    sleep 0.1
  done
  die "agent did not stop (is_stop_agent='${stopped:-}')"
}

dlclose_agent() {
  timeout 5 "$HOOKSO" triggerp "$LUA_PID" "$FUNC_ADDR" dlclose "$HANDLE" >/dev/null || true
  HANDLE=""
}

write_coro_script() {
  local dest="$1"
  cat > "$dest" << 'LUA'
-- Deterministic coroutine target: main + a worker coroutine both loop.
local function worker()
    local n = 0
    while true do
        n = n + 1
        if n > 100000 then n = 0 end
        coroutine.yield(n)
    end
end

local co = coroutine.create(worker)
while true do
    if coroutine.status(co) ~= "dead" then
        coroutine.resume(co)
    else
        co = coroutine.create(worker)
    end
    local x = 0
    local f = function()
        x = x + 1
        return x
    end
    f()
    os.execute("usleep 100")
end
LUA
}

# --- scenarios -------------------------------------------------------------

scenario_repeat_attach() {
  echo "=== scenario: repeated attach/detach ==="
  start_lua "$ROOT/test.lua"
  locate_state || die "could not locate lua_State via gdb+hookso argp"
  inject_agent

  local i
  for i in 1 2 3; do
    echo "smoke_scenarios: attach cycle $i start"
    call_start_agent
    # Give the count hook a moment to run ini_agent.
    sleep 0.3
    kill -0 "$LUA_PID" 2>/dev/null || die "target died during attach cycle $i"
    call_stop_agent
    wait_stopped
    kill -0 "$LUA_PID" 2>/dev/null || die "target died during detach cycle $i"
    echo "smoke_scenarios: attach cycle $i ok"
  done

  dlclose_agent
  kill -0 "$LUA_PID" 2>/dev/null || die "target died after dlclose"
  stop_lua
  echo "scenario repeated attach/detach: OK"
}

scenario_early_exit() {
  echo "=== scenario: target early exit ==="
  start_lua "$ROOT/test.lua"
  local target_pid="$LUA_PID"
  locate_state || die "could not locate lua_State via gdb+hookso argp"
  inject_agent
  call_start_agent
  sleep 0.3

  echo "smoke_scenarios: SIGKILL target $target_pid"
  kill -9 "$target_pid" 2>/dev/null || true
  local deadline=$((SECONDS + 10))
  while (( SECONDS < deadline )); do
    if ! kill -0 "$target_pid" 2>/dev/null; then
      break
    fi
    sleep 0.05
  done
  wait "$target_pid" 2>/dev/null || true
  if kill -0 "$target_pid" 2>/dev/null; then
    die "target still alive after SIGKILL"
  fi
  LUA_PID=""

  # hookso against a dead pid must not hang the suite.
  echo "smoke_scenarios: hookso call on dead pid (expect failure, no hang)"
  timeout 5 "$HOOKSO" call "$target_pid" dluaagent.so stop_agent >/dev/null 2>&1 || true
  timeout 5 "$HOOKSO" call "$target_pid" dluaagent.so is_stop_agent >/dev/null 2>&1 || true

  "$ROOT/scripts/ipc_cleanup.sh" "$target_pid"
  if ls /tmp/dlua-"${target_pid}"-* >/dev/null 2>&1; then
    die "IPC path files left after cleanup: $(ls /tmp/dlua-${target_pid}-* 2>/dev/null)"
  fi
  echo "scenario target early exit: OK"
}

scenario_coro_target() {
  echo "=== scenario: coroutine target attach/detach ==="
  local script
  script="$(mktemp /tmp/dlua-coro-XXXXXX.lua)"
  write_coro_script "$script"
  start_lua "$script"
  locate_state || die "could not locate lua_State via gdb+hookso argp"
  inject_agent
  call_start_agent
  sleep 0.5
  kill -0 "$LUA_PID" 2>/dev/null || die "coroutine target died after attach"
  call_stop_agent
  wait_stopped
  kill -0 "$LUA_PID" 2>/dev/null || die "coroutine target died after detach"
  # One more attach/detach cycle on the same process.
  call_start_agent
  sleep 0.3
  call_stop_agent
  wait_stopped
  kill -0 "$LUA_PID" 2>/dev/null || die "coroutine target died after second detach"
  # Do not dlclose here: lua_newthread copies hook_handler onto worker
  # coroutines, and fini_agent only restores the hook on g_L. Leftover hooks
  # self-disarm the next time those threads run; unloading the agent first
  # would leave a dangling hook pointer.
  stop_lua
  rm -f "$script"
  echo "scenario coroutine target: OK"
}

cleanup_all() {
  stop_lua
}
trap cleanup_all EXIT

scenario_repeat_attach
scenario_early_exit
scenario_coro_target

echo "smoke_scenarios: ALL OK"
