#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/client.hpp>
#include <iostream>
#include <string>

typedef websocketpp::client<websocketpp::config::asio> client;

using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::lib::bind;

class WebsocketClient {
public:
    WebsocketClient() {
        endpoint_.set_access_channels(websocketpp::log::alevel::all);
        endpoint_.clear_access_channels(websocketpp::log::alevel::frame_payload);
        endpoint_.init_asio();
        endpoint_.set_open_handler(bind(&WebsocketClient::on_open, this, ::_1));
        endpoint_.set_close_handler(bind(&WebsocketClient::on_close, this, ::_1));
        endpoint_.set_message_handler(bind(&WebsocketClient::on_message, this, ::_1, ::_2));
    }

    void on_open(websocketpp::connection_hdl hdl) {
        std::cout << "Connected to server" << std::endl;
        endpoint_.send(hdl, "Hello from client!", websocketpp::frame::opcode::text);
    }

    void on_close(websocketpp::connection_hdl hdl) {
        std::cout << "Disconnected from server" << std::endl;
        connected_ = false;
    }

    void on_message(websocketpp::connection_hdl hdl, client::message_ptr msg) {
        std::cout << "Server response: " << msg->get_payload() << std::endl;
    }

    void connect(const std::string& uri) {
        websocketpp::lib::error_code ec;
        client::connection_ptr con = endpoint_.get_connection(uri, ec);
        if (ec) {
            std::cerr << "Connection error: " << ec.message() << std::endl;
            return;
        }
        endpoint_.connect(con);
        connected_ = true;
    }

    void run() {
        endpoint_.run();
    }

    void send(const std::string& message) {
        if (connected_) {
            endpoint_.send(connection_hdl_, message, websocketpp::frame::opcode::text);
        }
    }

    void set_connection_hdl(websocketpp::connection_hdl hdl) {
        connection_hdl_ = hdl;
    }

private:
    client endpoint_;
    bool connected_ = false;
    websocketpp::connection_hdl connection_hdl_;
};

int main() {
    try {
        WebsocketClient client;
        client.connect("ws://localhost:9002");
        client.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
