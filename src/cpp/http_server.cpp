#include "http_server.hpp"
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/strand.hpp>
#include <fstream>
#include <iostream>
#include <thread>
#include <shared_mutex>
#include <filesystem>

namespace pyspeed {

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

// HttpServer Implementation
HttpServer::HttpServer(const Config& config)
    : config_(config)
    , ioc_{config.threads}
    , acceptor_{ioc_}
    , stats_{}
{
    stats_.start_time = std::chrono::steady_clock::now();
}

HttpServer::~HttpServer() {
    stop();
}

void HttpServer::start() {
    if (running_.load()) {
        return;
    }
    
    try {
        // Configure acceptor
        auto const address = net::ip::make_address(config_.address);
        tcp::endpoint endpoint{address, config_.port};
        
        acceptor_.open(endpoint.protocol());
        acceptor_.set_option(net::socket_base::reuse_address(true));
        acceptor_.bind(endpoint);
        acceptor_.listen(net::socket_base::max_listen_connections);
        
        running_.store(true);
        
        std::cout << "PySpeed HTTP Server starting on " 
                  << config_.address << ":" << config_.port 
                  << " with " << config_.threads << " threads\n";
        
        // Start accepting connections
        do_accept();
        
        // Start worker threads
        threads_.reserve(config_.threads);
        for (int i = 0; i < config_.threads; ++i) {
            threads_.emplace_back([this] { 
                ioc_.run(); 
            });
        }
        
        std::cout << "✅ PySpeed HTTP Server started successfully!\n";
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Failed to start server: " << e.what() << "\n";
        running_.store(false);
        throw;
    }
}

void HttpServer::stop() {
    if (!running_.load()) {
        return;
    }
    
    std::cout << "Stopping PySpeed HTTP Server...\n";
    
    running_.store(false);
    
    // Stop accepting new connections
    beast::error_code ec;
    acceptor_.close(ec);
    
    // Stop the I/O context
    ioc_.stop();
    
    // Wait for all threads to finish
    for (auto& thread : threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    threads_.clear();
    
    // Print final statistics
    auto duration = std::chrono::steady_clock::now() - stats_.start_time;
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration).count();
    
    std::cout << "✅ Server stopped. Final stats:\n";
    std::cout << "  Requests processed: " << stats_.requests_processed.load() << "\n";
    std::cout << "  Bytes sent: " << stats_.bytes_sent.load() << "\n";
    std::cout << "  Bytes received: " << stats_.bytes_received.load() << "\n";
    std::cout << "  Errors: " << stats_.errors.load() << "\n";
    if (seconds > 0) {
        std::cout << "  Average RPS: " << stats_.requests_processed.load() / seconds << "\n";
    }
}

void HttpServer::set_request_handler(RequestHandler handler) {
    request_handler_ = std::move(handler);
}

void HttpServer::set_static_handler(const std::string& path, const std::string& root) {
    static_paths_[path] = root;
}

void HttpServer::do_accept() {
    acceptor_.async_accept(
        net::make_strand(ioc_),
        beast::bind_front_handler(&HttpServer::on_accept, this));
}

void HttpServer::on_accept(beast::error_code ec, tcp::socket socket) {
    if (ec) {
        if (ec != net::error::operation_aborted) {
            std::cerr << "Accept error: " << ec.message() << "\n";
            stats_.errors.fetch_add(1);
        }
    } else {
        // Create new session for this connection
        std::make_shared<Session>(std::move(socket), this)->run();
    }
    
    // Continue accepting connections if still running
    if (running_.load()) {
        do_accept();
    }
}

double HttpServer::Stats::requests_per_second() const {
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
    return duration > 0 ? static_cast<double>(requests_processed.load()) / duration : 0.0;
}

double HttpServer::Stats::average_response_time() const {
    // This would need more sophisticated tracking in a real implementation
    return 0.0; // Placeholder
}

// Session Implementation
HttpServer::Session::Session(tcp::socket&& socket, HttpServer* server)
    : stream_(std::move(socket))
    , server_(server)
    , start_time_(std::chrono::steady_clock::now())
{
}

void HttpServer::Session::run() {
    // Set timeout
    stream_.expires_after(server_->config_.keep_alive_timeout);
    
    do_read();
}

void HttpServer::Session::do_read() {
    // Make the request empty before reading
    req_ = {};
    
    // Set the timeout
    stream_.expires_after(server_->config_.keep_alive_timeout);
    
    // Read a request
    http::async_read(stream_, buffer_, req_,
        beast::bind_front_handler(&Session::on_read, shared_from_this()));
}

void HttpServer::Session::on_read(beast::error_code ec, std::size_t bytes_transferred) {
    boost::ignore_unused(bytes_transferred);
    
    // Connection closed cleanly
    if (ec == http::error::end_of_stream) {
        return do_close();
    }
    
    if (ec) {
        std::cerr << "Read error: " << ec.message() << "\n";
        server_->stats_.errors.fetch_add(1);
        return;
    }
    
    // Update statistics
    server_->stats_.requests_processed.fetch_add(1);
    server_->stats_.bytes_received.fetch_add(bytes_transferred);
    
    // Handle the request
    handle_request();
}

void HttpServer::Session::handle_request() {
    auto const bad_request = [this](beast::string_view why) {
        auto response = make_error_response(http::status::bad_request, std::string(why));
        
        http::async_write(stream_, response,
            beast::bind_front_handler(&Session::on_write, shared_from_this(),
                response.need_eof()));
    };
    
    auto const not_found = [this](beast::string_view target) {
        auto response = make_error_response(http::status::not_found, 
            "The resource '" + std::string(target) + "' was not found.");
        
        http::async_write(stream_, response,
            beast::bind_front_handler(&Session::on_write, shared_from_this(),
                response.need_eof()));
    };
    
    auto const server_error = [this](beast::string_view what) {
        auto response = make_error_response(http::status::internal_server_error, 
            "An error occurred: '" + std::string(what) + "'");
        
        http::async_write(stream_, response,
            beast::bind_front_handler(&Session::on_write, shared_from_this(),
                response.need_eof()));
    };
    
    // Make sure we can handle the method
    if (req_.method() != http::verb::get &&
        req_.method() != http::verb::head &&
        req_.method() != http::verb::post &&
        req_.method() != http::verb::put &&
        req_.method() != http::verb::delete_ &&
        req_.method() != http::verb::options) {
        return bad_request("Unknown HTTP-method");
    }
    
    // Request path must be absolute and not contain ".."
    if (req_.target().empty() ||
        req_.target()[0] != '/' ||
        req_.target().find("..") != beast::string_view::npos) {
        return bad_request("Illegal request-target");
    }
    
    // Check if this is a static file request
    std::string target = std::string(req_.target());
    bool is_static = false;
    
    for (const auto& [path_prefix, root] : server_->static_paths_) {
        if (target.substr(0, path_prefix.length()) == path_prefix) {
            handle_static_request(target);
            is_static = true;
            break;
        }
    }
    
    if (!is_static) {
        // Handle dynamic request through Python
        handle_dynamic_request();
    }
}

void HttpServer::Session::handle_static_request(const std::string& path) {
    try {
        // Find the appropriate static root
        std::string file_path;
        for (const auto& [path_prefix, root] : server_->static_paths_) {
            if (path.substr(0, path_prefix.length()) == path_prefix) {
                file_path = root + path.substr(path_prefix.length());
                break;
            }
        }
        
        // Check if file exists
        if (!std::filesystem::exists(file_path) || 
            !std::filesystem::is_regular_file(file_path)) {
            auto response = make_error_response(http::status::not_found, 
                "File not found");
            
            http::async_write(stream_, response,
                beast::bind_front_handler(&Session::on_write, shared_from_this(),
                    response.need_eof()));
            return;
        }
        
        // Read file content
        std::ifstream file(file_path, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
        
        // Determine content type
        std::string content_type = "application/octet-stream";
        auto ext_pos = file_path.find_last_of('.');
        if (ext_pos != std::string::npos) {
            std::string ext = file_path.substr(ext_pos);
            if (ext == ".html") content_type = "text/html";
            else if (ext == ".css") content_type = "text/css";
            else if (ext == ".js") content_type = "application/javascript";
            else if (ext == ".json") content_type = "application/json";
            else if (ext == ".png") content_type = "image/png";
            else if (ext == ".jpg" || ext == ".jpeg") content_type = "image/jpeg";
            else if (ext == ".gif") content_type = "image/gif";
            else if (ext == ".svg") content_type = "image/svg+xml";
        }
        
        // Create response
        auto response = make_response(std::move(req_), http::status::ok, 
                                    content, content_type);
        
        // Send response
        http::async_write(stream_, response,
            beast::bind_front_handler(&Session::on_write, shared_from_this(),
                response.need_eof()));
        
    } catch (const std::exception& e) {
        auto response = make_error_response(http::status::internal_server_error,
            "Error serving static file: " + std::string(e.what()));
        
        http::async_write(stream_, response,
            beast::bind_front_handler(&Session::on_write, shared_from_this(),
                response.need_eof()));
    }
}

void HttpServer::Session::handle_dynamic_request() {
    if (server_->request_handler_) {
        // Call the Python request handler
        server_->request_handler_(
            std::move(req_),
            [self = shared_from_this()](http::response<http::string_body> response) {
                // Send the response back to client
                http::async_write(self->stream_, response,
                    beast::bind_front_handler(&Session::on_write, self,
                        response.need_eof()));
            }
        );
    } else {
        // No handler configured
        auto response = make_error_response(http::status::not_implemented,
            "No request handler configured");
        
        http::async_write(stream_, response,
            beast::bind_front_handler(&Session::on_write, shared_from_this(),
                response.need_eof()));
    }
}

template<class Body, class Allocator>
http::response<http::string_body> HttpServer::Session::make_response(
    http::request<Body, http::basic_fields<Allocator>>&& req,
    http::status status,
    const std::string& body,
    const std::string& content_type) {
    
    http::response<http::string_body> res{status, req.version()};
    res.set(http::field::server, "PySpeed/1.0");
    res.set(http::field::content_type, content_type);
    res.content_length(body.size());
    res.keep_alive(req.keep_alive());
    res.body() = body;
    res.prepare_payload();
    
    return res;
}

http::response<http::string_body> HttpServer::Session::make_error_response(
    http::status status, 
    const std::string& message) {
    
    http::response<http::string_body> res{status, 11};
    res.set(http::field::server, "PySpeed/1.0");
    res.set(http::field::content_type, "text/html");
    res.content_length(message.size());
    res.keep_alive(false);
    res.body() = message;
    res.prepare_payload();
    
    return res;
}

void HttpServer::Session::on_write(bool close, beast::error_code ec, std::size_t bytes_transferred) {
    boost::ignore_unused(bytes_transferred);
    
    if (ec) {
        std::cerr << "Write error: " << ec.message() << "\n";
        server_->stats_.errors.fetch_add(1);
        return;
    }
    
    // Update statistics
    server_->stats_.bytes_sent.fetch_add(bytes_transferred);
    
    if (close) {
        // Connection should be closed
        return do_close();
    }
    
    // Read another request
    do_read();
}

void HttpServer::Session::do_close() {
    beast::error_code ec;
    stream_.socket().shutdown(tcp::socket::shutdown_send, ec);
    
    // At this point the connection is closed gracefully
}

} // namespace pyspeed