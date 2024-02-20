#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/config.hpp>
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

class http_connection : public std::enable_shared_from_this<http_connection> {
public:
    http_connection(tcp::socket socket) : socket_(std::move(socket)) {}

    void start() {
        read_request();
    }

private:
    void read_request() {
        auto self = shared_from_this();

        http::async_read(socket_, buffer_, req_,
            [self](beast::error_code ec, std::size_t bytes_transferred) {
                boost::ignore_unused(bytes_transferred);
                if (!ec)
                    self->send_response();
            });
    }

    void send_response() {
        auto self = shared_from_this();
        res_.version(req_.version());
        res_.keep_alive(false);
        res_.set(http::field::server, "Simple C++ HTTP Server");
        res_.set(http::field::content_type, "text/html");
        res_.body() = "<html><body>Hello, world!</body></html>";
        res_.prepare_payload();

        http::async_write(socket_, res_,
            [self](beast::error_code ec, std::size_t bytes_transferred) {
                self->socket_.shutdown(tcp::socket::shutdown_send, ec);
                self->deadline_.cancel();
            });
    }

    tcp::socket socket_;
    beast::flat_buffer buffer_;
    http::request<http::dynamic_body> req_;
    http::response<http::dynamic_body> res_;
};

class http_server {
public:
    http_server(net::io_context& ioc, tcp::endpoint endpoint)
        : acceptor_(ioc), socket_(ioc) {
        beast::error_code ec;

        acceptor_.open(endpoint.protocol(), ec);
        acceptor_.set_option(net::socket_base::reuse_address(true), ec);
        acceptor_.bind(endpoint, ec);
        acceptor_.listen(net::socket_base::max_listen_connections, ec);

        if (ec) {
            std::cerr << "Failed to bind the acceptor: " << ec.message() << "\n";
            return;
        }

        accept_connection();
    }

private:
    void accept_connection() {
        acceptor_.async_accept(socket_, [this](beast::error_code ec) {
            if (!acceptor_.is_open())
                return;

            if (!ec) {
                std::make_shared<http_connection>(std::move(socket_))->start();
            }

            accept_connection();
        });
    }

    tcp::acceptor acceptor_;
    tcp::socket socket_;
};

int main() {
    auto const address = net::ip::make_address("0.0.0.0");
    auto const port = static_cast<unsigned short>(std::atoi("8080"));
    auto const threads = std::max<int>(1, std::atoi("1"));

    net::io_context ioc{threads};

    std::make_shared<http_server>(ioc, tcp::endpoint{address, port})->run();

    std::vector<std::thread> v;
    v.reserve(threads - 1);

    for(auto i = threads - 1; i > 0; --i)
        v.emplace_back([&ioc]{ ioc.run(); });

    ioc.run();

    return 0;
}