#! /bin/sh

g++ -g3 -o dlua main.cpp -std=c++11 -lreadline 

g++ -g3 -shared -o dluaagent.so agent.cpp -fPIC -std=c++11

g++ -g3 -o test_unit test_unit.cpp agent.cpp -std=c++11 -llua -ldl -lreadline


