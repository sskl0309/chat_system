CXX = g++
CXXFLAGS = -std=c++14 -Wall -Wextra

SPDLOG_INCLUDE = -I/usr/include
SPDLOG_LIBS = -lpthread -lfmt

.PHONY: all clean sync async log_test

all: sync async log_test

sync: sync.cc
	$(CXX) $(CXXFLAGS) $(SPDLOG_INCLUDE) -o $@ $^ $(SPDLOG_LIBS)

async: async.cc
	$(CXX) $(CXXFLAGS) $(SPDLOG_INCLUDE) -o $@ $^ $(SPDLOG_LIBS)

log_test: log_test.cc
	$(CXX) $(CXXFLAGS) $(SPDLOG_INCLUDE) -o $@ $^ $(SPDLOG_LIBS)

clean:
	rm -f sync async log_test sync.log async.log test.log app.log

run-sync: sync
	./sync

run-async: async
	./async

run-log: log_test
	./log_test
