#!/bin/bash
set -e
cd "$(dirname "$0")"

UTILS_SRCS=""
for f in utils/*.cpp; do
    if [ -f "$f" ] && [ "$(basename "$f")" != "monitoring.cpp" ]; then
        UTILS_SRCS+="$f "
    fi
done

for dir in workers/*/; do
    name=$(basename "$dir")
    srcs=$(find "$dir" -name '*.cpp')
    if [ -n "$srcs" ]; then
        echo "Kompiluję $name..."
        g++ -std=c++11 -Wall -O2 $srcs $UTILS_SRCS config/config.cpp -I. -Iheaders -Iutils -I$dir -o "$dir$name"
    fi
done

if [ -f main.cpp ]; then
    echo "Kompiluję main..."
    g++ -std=c++11 -Wall -O2 main.cpp $UTILS_SRCS config/config.cpp -I. -Iheaders -Iutils -o main
fi

if [ -f loader.cpp ]; then
    echo "Kompiluję loader..."
    g++ -std=c++11 -Wall -O2 loader.cpp $UTILS_SRCS config/config.cpp -I. -Iheaders -Iutils -o loader
fi

if [ -d utils ]; then
    echo "Kompiluję monitoring..."
    g++ -std=c++11 -Wall -O2 utils/monitoring.cpp config/config.cpp -I. -Iheaders -Iutils -o monitoring
fi
