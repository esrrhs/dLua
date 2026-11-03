#! /bin/sh
set -e

# Optional overrides for non-default Lua installs, e.g.:
#   LUA_INC=/usr/local/include LUA_LIB="-L/usr/local/lib -llua" ./build.sh
LUA_INC="${LUA_INC:-}"
LUA_LIB="${LUA_LIB:--llua}"

INCFLAGS=""
if [ -n "$LUA_INC" ]; then
  INCFLAGS="-I${LUA_INC}"
fi

g++ -g3 -o dlua main.cpp -std=c++11 -lreadline

g++ -g3 -shared -o dluaagent.so agent.cpp -fPIC -std=c++11 ${INCFLAGS}

g++ -g3 -o test_unit test_unit.cpp agent.cpp -std=c++11 ${INCFLAGS} ${LUA_LIB} -ldl -lreadline
