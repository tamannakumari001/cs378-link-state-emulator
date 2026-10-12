CXX      := g++
CXXFLAGS := -Wall -Wextra -O2
JAVAC    := javac 


CONFIG ?= configs/example.txt
ON_IP  ?= $(shell hostname -I | awk '{print $$1}')
PORT   ?= 5000

.PHONY: all oracle run-oracle run-vn clean

all: vn/vn oracle


vn/vn: vn/vn.cpp vn/protocol.h
	$(CXX) $(CXXFLAGS) -o $@ vn/vn.cpp


oracle:
	mkdir -p build
	$(JAVAC) -d build oracle/*.java

run-oracle: oracle
	java -cp build oracle.OracleNode $(CONFIG)

run-vn: vn/vn
	./vn/vn $(ON_IP) $(PORT)

clean:
	rm -rf build vn/vn
