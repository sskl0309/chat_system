CXX = g++
CXXFLAGS = -std=c++14 -Wall -Wextra

GTEST_INCLUDE = -I/usr/include
GTEST_LIBS = -lgtest -lgtest_main -lpthread

TARGET = test_main
SRCS = main.cc

.PHONY: all clean run install_gtest

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) $(GTEST_INCLUDE) -o $@ $^ $(GTEST_LIBS)

clean:
	rm -f $(TARGET)

run: $(TARGET)
	./$(TARGET)

install_gtest:
	@echo "Installing Google Test..."
	sudo apt-get update && sudo apt-get install -y libgtest-dev libgmock-dev
	@echo "Building gtest libraries..."
	cd /usr/src/gtest && sudo cmake . && sudo make && sudo cp lib/*.a /usr/lib/x86_64-linux-gnu/
	cd /usr/src/gmock && sudo cmake . && sudo make && sudo cp lib/*.a /usr/lib/x86_64-linux-gnu/
