CC = g++
CFLAGS = -std=c++14 -Wall -Wextra -I.. -I../bRpc -I../bRpc/build
LIBS = $(shell pkg-config --cflags --libs brpc) -lprotobuf -lgflags -lssl -lcrypto -lz -ldl -lpthread -lleveldb -lfmt -letcd-cpp-api 

PROTOS = ../bRpc/build/main.pb.h ../bRpc/build/main.pb.cc

# elasticlient 相关路径
ES_DIR = ../es/elasticlient/build/lib
ES_INC = -I../es/elasticlient/include -I../es/elasticlient/external/cpr/include -I../es/elasticlient/external/jsoncpp/include
ES_LIB = -L$(ES_DIR) -lelasticlient -lcpr -ljsoncpp -lcurl -lfmt -Wl,-rpath,$(ES_DIR)

all: server client es_client brpc_example

server: server.cpp $(PROTOS)
	$(CC) $(CFLAGS) -o server server.cpp ../bRpc/build/main.pb.cc $(LIBS)

client: client.cpp $(PROTOS)
	$(CC) $(CFLAGS) -o client client.cpp ../bRpc/build/main.pb.cc $(LIBS)

es_client: es_client.cpp
	$(CC) $(CFLAGS) $(ES_INC) -o es_client es_client.cpp $(ES_LIB)

brpc_example: brpc_example.cpp $(PROTOS)
	$(CC) $(CFLAGS) -o brpc_example brpc_example.cpp ../bRpc/build/main.pb.cc $(LIBS)

clean:
	rm -f server client es_client brpc_example