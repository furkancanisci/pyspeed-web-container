#pragma once

#include "compatibility.hpp"
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <functional>
#include <unordered_map>
#include <chrono>
#include <atomic>

#if PYSPEED_HAS_BOOST
    #include <boost/beast/core.hpp>
    #include <boost/beast/http.hpp>
    #include <boost/beast/version.hpp>
    #include <boost/asio/ip/tcp.hpp>
    #include <boost/asio/strand.hpp>
    #include <boost/config.hpp>
#endif

namespace pyspeed {

#if PYSPEED_HAS_BOOST
    namespace beast = boost::beast;
    namespace http = beast::http;
    namespace net = boost::asio;
    using tcp = boost::asio::ip::tcp;
#else
    // Fallback namespace definitions
    namespace beast {
        using error_code = int;
        struct tcp_stream {
            void close() {}
        };
        struct flat_buffer {};
    }
    namespace http {
        template<typename T> struct request {};
        template<typename T> struct response {};
        struct string_body {};
        using status = int;
        struct field {};
        template<typename T> struct basic_fields {};
    }
    namespace net {
        struct io_context {};
        struct socket_base {
            static constexpr int reuse_address = 1;
            static constexpr int max_listen_connections = 10;
        };
        template<typename T> struct strand {};
        template<typename... Args> auto make_strand(Args&&... args) { return strand<void>{}; }
    }
    struct tcp {
        struct socket {};
        struct acceptor {
            void set_option(int) {}
            void bind(const tcp::endpoint&) {}
            void listen(int) {}
        };
        struct endpoint {};
    };
#endif

/**
 * High-performance HTTP server designed for Python web application acceleration.
 * Uses Boost.Beast for async I/O and zero-copy operations.
 * 
 * Performance targets:
 * - 50,000+ requests/second
 * - Sub-millisecond latency
 * - Zero-copy request/response handling
 * - Memory-mapped static file serving
 */
class HttpServer {
public:
    // Request handler function type
    using RequestHandler = std::function<void(
        http::request<http::string_body>&&,
        std::function<void(http::response<http::string_body>)>
    )>;
    
    // Performance configuration
    struct Config {
        std::string address = "0.0.0.0";
        unsigned short port = 8080;
        int threads = std::thread::hardware_concurrency();
        std::size_t max_request_size = 10 * 1024 * 1024; // 10MB
        std::chrono::seconds keep_alive_timeout{30};
        bool enable_compression = true;
        bool enable_static_cache = true;
        std::size_t static_cache_size = 1024 * 1024 * 1024; // 1GB
        
        // Performance optimizations
        bool use_memory_pool = true;
        bool enable_zero_copy = true;
        std::size_t io_buffer_size = 64 * 1024; // 64KB
    };

    explicit HttpServer(const Config& config = Config{});
    ~HttpServer();

    // Server lifecycle
    void start();
    void stop();
    bool is_running() const { return running_; }

    // Request routing
    void set_request_handler(RequestHandler handler);
    void set_static_handler(const std::string& path, const std::string& root);
    
    // Performance monitoring
    struct Stats {
        std::atomic<uint64_t> requests_processed{0};
        std::atomic<uint64_t> bytes_sent{0};
        std::atomic<uint64_t> bytes_received{0};
        std::atomic<uint64_t> errors{0};
        std::chrono::steady_clock::time_point start_time;
        
        double requests_per_second() const;
        double average_response_time() const;
    };
    
    const Stats& get_stats() const { return stats_; }

private:
    class Session;
    
    void run_server();
    void do_accept();
    void on_accept(beast::error_code ec, tcp::socket socket);
    
    // Configuration
    Config config_;
    
    // Network components
    net::io_context ioc_;
    tcp::acceptor acceptor_;
    std::vector<std::thread> threads_;
    
    // Request handling
    RequestHandler request_handler_;
    std::unordered_map<std::string, std::string> static_paths_;
    
    // Server state
    std::atomic<bool> running_{false};
    Stats stats_;
};

/**
 * HTTP session handling individual connections.
 * Optimized for high throughput and low latency.
 */
class HttpServer::Session : public std::enable_shared_from_this<Session> {
public:
    explicit Session(tcp::socket&& socket, HttpServer* server);
    void run();

private:
    void do_read();
    void on_read(beast::error_code ec, std::size_t bytes_transferred);
    void on_write(bool close, beast::error_code ec, std::size_t bytes_transferred);
    void do_close();
    
    // Handle different request types
    void handle_request();
    void handle_static_request(const std::string& path);
    void handle_dynamic_request();
    
    // Response builders
    template<class Body, class Allocator>
    http::response<http::string_body> make_response(
        http::request<Body, http::basic_fields<Allocator>>&& req,
        http::status status,
        const std::string& body,
        const std::string& content_type = "text/html");
    
    http::response<http::string_body> make_error_response(
        http::status status, 
        const std::string& message);
    
    // Connection components
    beast::tcp_stream stream_;
    beast::flat_buffer buffer_;
    http::request<http::string_body> req_;
    std::shared_ptr<void> res_;
    
    // Server reference
    HttpServer* server_;
    
    // Performance tracking
    std::chrono::steady_clock::time_point start_time_;
};

/**
 * Static file cache for memory-mapped file serving.
 * Provides nginx-level performance for static content.
 */
class StaticFileCache {
public:
    struct CacheEntry {
        std::string content;
        std::string content_type;
        std::chrono::steady_clock::time_point last_modified;
        std::size_t size;
    };
    
    explicit StaticFileCache(std::size_t max_size);
    
    bool get_file(const std::string& path, CacheEntry& entry);
    void cache_file(const std::string& path, const CacheEntry& entry);
    
    // Cache statistics
    struct CacheStats {
        std::atomic<uint64_t> hits{0};
        std::atomic<uint64_t> misses{0};
        std::atomic<uint64_t> evictions{0};
        
        double hit_ratio() const {
            auto total = hits.load() + misses.load();
            return total > 0 ? static_cast<double>(hits.load()) / total : 0.0;
        }
    };
    
    const CacheStats& get_stats() const { return stats_; }

private:
    std::unordered_map<std::string, CacheEntry> cache_;
    std::size_t max_size_;
    std::size_t current_size_;
    mutable std::shared_mutex mutex_;
    CacheStats stats_;
    
    void evict_lru();
    std::string get_content_type(const std::string& path);
};

} // namespace pyspeed