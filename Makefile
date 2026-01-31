CXX = g++
CXXFLAGS = -std=c++23 -O3 -Wall -Wextra -pedantic
LDFLAGS = -lsqlite3

all: build

build: spip

spip: spip.cpp
	$(CXX) $(CXXFLAGS) spip.cpp -o spip $(LDFLAGS)

clean:
	rm -f spip

.PHONY: all build clean
