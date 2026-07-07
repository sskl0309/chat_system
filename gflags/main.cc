#include <gflags/gflags.h>
#include <iostream>

DEFINE_int32(port, 8000, "TCP port of this server");
DEFINE_string(ip, "127.0.0.1", "Server listen address, default is 127.0.0.1");
DEFINE_bool(debug, true, "Enable debug mode");

int main(int argc, char* argv[]) 
{
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    std::cout << "port: " << FLAGS_port;
    std::cout << "ip: " << FLAGS_ip;
    std::cout << "debug: " << FLAGS_debug;
    
    return 0;
}