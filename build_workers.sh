#!/bin/bash
set -e
cd "$(dirname "$0")"

for dir in workers/*/; do
    name=$(basename "$dir")
    srcs=$(find "$dir" -name '*.cpp')
    if [ -n "$srcs" ]; then
        echo "Kompiluję $name..."
        g++ -std=c++11 -Wall -O2 $srcs source/*.cpp -I. -Iheaders -Iutils -I$dir -o "$dir$name"
    fi
done

# Jeśli istnieje główny program `main.cpp`, zbuduj wykonywalny `main`
if [ -f main.cpp ]; then
    echo "Kompiluję main..."
    g++ -std=c++11 -Wall -O2 main.cpp source/*.cpp -I. -Iheaders -Iutils -o main
fi
