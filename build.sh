#! /bin/sh

g++ -g3 -o dlua main.cpp -std=c++11

g++ -g3 -shared -o dluaagent.so agent.cpp -fPIC

