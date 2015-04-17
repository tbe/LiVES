#!/bin/bash
find . -name \*.c -or -name \*.h | xargs astyle --style=java -H -Y -s2 -U -k3 -W3 -xC140 -xL
