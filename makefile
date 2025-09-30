all: test

test: test.cpp DynamicThreadPool.h
	g++ -o test test.cpp -std=c++17 -pthread

clean:
	rm -f test