#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <iostream>
#include <string>
#include <set>

typedef websocketpp::server<websocketpp::config::asio> server;

using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::lib::bind;

class WebsocketServer {
public:
    WebsocketServer() {
        endpoint_.set_access_channels(websocketpp::log::alevel::all);
        endpoint_.clear_access_channels(websocketpp::log::alevel::frame_payload);
        endpoint_.init_asio();
        endpoint_.set_open_handler(bind(&WebsocketServer::on_open, this, ::_1));
        endpoint_.set_close_handler(bind(&WebsocketServer::on_close, this, ::_1));
        endpoint_.set_message_handler(bind(&WebsocketServer::on_message, this, ::_1, ::_2));
    }

    void on_open(websocketpp::connection_hdl hdl) {
        std::cout << "Client connected" << std::endl;
        connections_.insert(hdl);
        endpoint_.send(hdl, "Welcome to websocketpp server!", websocketpp::frame::opcode::text);
    }

    void on_close(websocketpp::connection_hdl hdl) {
        std::cout << "Client disconnected" << std::endl;
        connections_.erase(hdl);
    }

    void on_message(websocketpp::connection_hdl hdl, server::message_ptr msg) {
        std::string payload = msg->get_payload();
        std::cout << "Received: " << payload << std::endl;
        if (payload == "ping") {
            endpoint_.send(hdl, "pong", websocketpp::frame::opcode::text);
        } else if (payload == "broadcast") {
            broadcast("Server broadcast!");
        } else {
            endpoint_.send(hdl, "Echo: " + payload, websocketpp::frame::opcode::text);
        }
    }

    void broadcast(const std::string& message) {
        for (auto hdl : connections_) {
            endpoint_.send(hdl, message, websocketpp::frame::opcode::text);
        }
        std::cout << "Broadcast: " << message << std::endl;
    }

    void run(uint16_t port) {
        endpoint_.listen(port);
        endpoint_.start_accept();
        std::cout << "Server listening on ws://0.0.0.0:" << port << std::endl;
        endpoint_.run();
    }

private:
    server endpoint_;
    std::set<websocketpp::connection_hdl, std::owner_less<websocketpp::connection_hdl>> connections_;
};

int main() {
    try {
        WebsocketServer server;
        server.run(9002);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
