all: main

main: main.cpp DynamicThreadPool.h
	g++ -o main main.cpp -std=c++17 -pthread

clean:
	rm -f main