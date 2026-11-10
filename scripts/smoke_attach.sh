#!/usr/bin/env bash
# B1 attach smoke: hookso-level inject into a live lua process (no readline/expect).
# Uses gdb for find_func_addr (same as dlua this PR) and hookso argp/triggerp/call.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

LUA_BIN="${LUA_BIN:-lua}"
HOOKSO="${DLUA_HOOKSO:-}"
AGENT="${DLUA_AGENT:-}"

die() { echo "smoke_attach: $*" >&2; exit 1; }

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

[[ -f "$ROOT/test.lua" ]] || die "missing test.lua"
command -v gdb >/dev/null 2>&1 || die "gdb required for find_func_addr"
command -v "$LUA_BIN" >/dev/null 2>&1 || die "lua not found: $LUA_BIN"

echo "smoke_attach: hookso=$HOOKSO"
echo "smoke_attach: agent=$AGENT"
echo "smoke_attach: lua=$LUA_BIN"

"$LUA_BIN" "$ROOT/test.lua" &
LUA_PID=$!
cleanup() {
  if kill -0 "$LUA_PID" 2>/dev/null; then
    kill "$LUA_PID" 2>/dev/null || true
    wait "$LUA_PID" 2>/dev/null || true
  fi
  if [[ -n "${LUA_PID:-}" ]]; then
    "$ROOT/scripts/ipc_cleanup.sh" "$LUA_PID" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT

# Give the target a moment to enter its loop.
sleep 0.5
kill -0 "$LUA_PID" 2>/dev/null || die "lua process exited early"

find_func_addr() {
  local func="$1"
  # Match dlua main.cpp: gdb -p PID -ex "p (long)FUNC" --batch
  local out
  out="$(gdb -p "$LUA_PID" -ex "p (long)${func}" --batch 2>/dev/null || true)"
  local addr
  addr="$(printf '%s\n' "$out" | sed -n 's/^\$1 = //p' | head -1 | tr -d '[:space:]')"
  printf '%s' "$addr"
}

FUNC_ADDR=""
LUA_STATE=""
DEADLINE=$((SECONDS + 20))
while (( SECONDS < DEADLINE )); do
  for func in luaV_execute luaD_call luaD_precall; do
    addr="$(find_func_addr "$func" || true)"
    if [[ -z "$addr" || "$addr" == "0" ]]; then
      continue
    fi
    # Wait briefly for the next call into that function.
    lval="$(timeout 1 "$HOOKSO" argp "$LUA_PID" "$addr" 1 2>/dev/null | tr -d '[:space:]' || true)"
    if [[ -n "$lval" && "$lval" != "0" && "$lval" != "-1" ]]; then
      FUNC_ADDR="$addr"
      LUA_STATE="$lval"
      echo "smoke_attach: found $func addr=$FUNC_ADDR L=$LUA_STATE"
      break 2
    fi
  done
  sleep 0.1
done

[[ -n "$FUNC_ADDR" && -n "$LUA_STATE" ]] || die "could not locate lua_State via gdb+hookso argp"

echo "smoke_attach: dlopen via triggerp"
HANDLE="$(timeout 5 "$HOOKSO" triggerp "$LUA_PID" "$FUNC_ADDR" dlopen "$AGENT" | tr -d '[:space:]')"
[[ -n "$HANDLE" && "$HANDLE" != "0" ]] || die "dlopen failed (handle='$HANDLE')"
echo "smoke_attach: handle=$HANDLE"

if grep -q 'dluaagent\.so' "/proc/${LUA_PID}/maps"; then
  echo "smoke_attach: maps contain dluaagent.so"
else
  die "dluaagent.so not present in /proc/${LUA_PID}/maps after dlopen"
fi

echo "smoke_attach: start_agent"
timeout 5 "$HOOKSO" call "$LUA_PID" dluaagent.so start_agent "i=${LUA_STATE}" "i=${LUA_PID}" >/dev/null

echo "smoke_attach: stop_agent"
timeout 5 "$HOOKSO" call "$LUA_PID" dluaagent.so stop_agent >/dev/null

# Wait until agent reports stopped (is_stop_agent == 1).
STOP_DEADLINE=$((SECONDS + 10))
while (( SECONDS < STOP_DEADLINE )); do
  stopped="$(timeout 5 "$HOOKSO" call "$LUA_PID" dluaagent.so is_stop_agent 2>/dev/null | tr -d '[:space:]' || true)"
  if [[ "$stopped" == "1" ]]; then
    break
  fi
  sleep 0.1
done
[[ "${stopped:-}" == "1" ]] || die "agent did not stop (is_stop_agent='${stopped:-}')"

echo "smoke_attach: dlclose via triggerp"
timeout 5 "$HOOKSO" triggerp "$LUA_PID" "$FUNC_ADDR" dlclose "$HANDLE" >/dev/null || true

echo "smoke_attach: OK"
