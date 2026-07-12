CC = g++
CFLAGS = -std=c++14 -Wall -Wextra -I.. -I../bRpc -I../bRpc/build
LIBS = $(shell pkg-config --cflags --libs brpc) -lprotobuf -lgflags -lssl -lcrypto -lz -ldl -lpthread -lleveldb -lfmt -letcd-cpp-api

PROTOS = ../bRpc/build/main.pb.h ../bRpc/build/main.pb.cc

all: server client

server: server.cpp $(PROTOS)
	$(CC) $(CFLAGS) -o server server.cpp ../bRpc/build/main.pb.cc $(LIBS)

client: client.cpp $(PROTOS)
	$(CC) $(CFLAGS) -o client client.cpp ../bRpc/build/main.pb.cc $(LIBS)

clean:
	rm -f server client