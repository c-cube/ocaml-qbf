#!/usr/bin/env sh

ROOT=`dirname $0`
cd "$ROOT"

for i in test_*.ml ; do
    echo -n "testing $i... "
    ocamlfind opt -package qbf.quantor -package qbf.depqbf -linkpkg "./$i" -o test.exe
    ./test.exe
    if [ -z $? ]; then
        echo "failed!";
    else
        echo "success!";
    fi
done
