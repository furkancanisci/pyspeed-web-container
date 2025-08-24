#pragma once

#include "compatibility.hpp"
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>

#if PYSPEED_HAS_STRING_VIEW
    #include <string_view>
#endif

#if PYSPEED_HAS_BOOST
    #include <boost/beast/http.hpp>
    namespace pyspeed {
    namespace http = boost::beast::http;
#else
    // Fallback when Boost is not available
    namespace pyspeed {
    namespace http {
        template<typename T> struct request {
            std::string method() const { return ""; }
            std::string target() const { return ""; }
            std::string body() const { return ""; }
        };
        template<typename T> struct response {
            std::string& body() { static std::string b; return b; }
            void set(const std::string&, const std::string&) {}
        };
        struct string_body {};
        struct field {};
    }
#endif

/**
 * High-performance HTTP request parser optimized for Python web applications.
 * Features:
 * - Zero-copy header parsing
 * - Fast URL parameter extraction
 * - Cookie parsing
 * - Multipart form data handling
 * - JSON body pre-validation
 * 
 * Performance targets:
 * - Parse 100,000+ requests/second
 * - Sub-microsecond parsing time
 * - Minimal memory allocations
 */
class RequestParser {
public:
    // Parsed request structure optimized for Python integration
    struct ParsedRequest {
        // Basic HTTP info
        std::string method;
        std::string path;
        std::string query_string;
        std::string protocol_version;
        
        // Headers (case-insensitive lookup)
        std::unordered_map<std::string, std::string> headers;
        
        // URL parameters
        std::unordered_map<std::string, std::string> params;
        
        // Query parameters
        std::unordered_map<std::string, std::vector<std::string>> query_params;
        
        // Cookies
        std::unordered_map<std::string, std::string> cookies;
        
        // Body content
        std::string body;
        std::string content_type;
        size_t content_length = 0;
        
        // Form data (for POST requests)
        std::unordered_map<std::string, std::string> form_data;
        
        // JSON pre-validation flag
        bool is_valid_json = false;
        
        // Performance tracking
        std::chrono::steady_clock::time_point parse_start;
        std::chrono::microseconds parse_duration{0};
    };
    
    RequestParser();
    ~RequestParser() = default;
    
    // Main parsing function
    ParsedRequest parse(const http::request<http::string_body>& request);
    
    // Individual parsing functions for specific components
    void parse_url_params(const std::string& path, ParsedRequest& result);
    void parse_query_string(const std::string& query, ParsedRequest& result);
    void parse_headers(const http::request<http::string_body>& request, ParsedRequest& result);
    void parse_cookies(const std::string& cookie_header, ParsedRequest& result);
    void parse_form_data(const std::string& body, const std::string& content_type, ParsedRequest& result);
    
    // Utility functions
    bool is_json_content(const std::string& body);
    std::string url_decode(const std::string& encoded);
    std::vector<std::string> split_string(const std::string& str, char delimiter);
    
    // Performance statistics
    struct ParserStats {
        std::atomic<uint64_t> requests_parsed{0};
        std::atomic<uint64_t> total_parse_time_us{0};
        std::atomic<uint64_t> json_requests{0};
        std::atomic<uint64_t> form_requests{0};
        std::atomic<uint64_t> multipart_requests{0};
        
        double average_parse_time_us() const {
            auto parsed = requests_parsed.load();
            return parsed > 0 ? static_cast<double>(total_parse_time_us.load()) / parsed : 0.0;
        }
        
        double requests_per_second() const {
            auto total_time_s = total_parse_time_us.load() / 1000000.0;
            return total_time_s > 0 ? static_cast<double>(requests_parsed.load()) / total_time_s : 0.0;
        }
    };
    
    const ParserStats& get_stats() const { return stats_; }

private:
    ParserStats stats_;
    
    // Optimized parsing helpers
    std::string_view fast_header_lookup(const http::request<http::string_body>& request, 
                                       const std::string& header_name);
    void fast_split_params(const std::string& params_str, 
                          std::unordered_map<std::string, std::string>& result);
    void fast_split_query(const std::string& query_str, 
                         std::unordered_map<std::string, std::vector<std::string>>& result);
};

/**
 * High-performance HTTP response builder optimized for Python web applications.
 * Features:
 * - Pre-allocated response buffers
 * - Fast header serialization
 * - Cookie setting optimization
 * - Compression support
 * - Template response caching
 */
class ResponseBuilder {
public:
    // Response structure for building HTTP responses
    struct ResponseData {
        int status_code = 200;
        std::string status_message = "OK";
        std::unordered_map<std::string, std::string> headers;
        std::vector<std::pair<std::string, std::string>> cookies;
        std::string body;
        
        // Response optimization flags
        bool enable_compression = false;
        bool enable_cache = false;
        std::chrono::seconds cache_max_age{0};
        
        // Performance tracking
        std::chrono::steady_clock::time_point build_start;
        std::chrono::microseconds build_duration{0};
    };
    
    ResponseBuilder();
    ~ResponseBuilder() = default;
    
    // Main response building function
    http::response<http::string_body> build_response(const ResponseData& data);
    
    // Convenience builders for common responses
    http::response<http::string_body> build_json_response(
        const std::string& json_body, 
        int status_code = 200,
        const std::unordered_map<std::string, std::string>& additional_headers = {});
    
    http::response<http::string_body> build_html_response(
        const std::string& html_body,
        int status_code = 200,
        const std::unordered_map<std::string, std::string>& additional_headers = {});
    
    http::response<http::string_body> build_error_response(
        int status_code,
        const std::string& error_message);
    
    http::response<http::string_body> build_redirect_response(
        const std::string& location,
        int status_code = 302);
    
    // Cookie helpers
    void add_cookie(ResponseData& response, 
                   const std::string& name, 
                   const std::string& value,
                   const std::string& path = "/",
                   const std::string& domain = "",
                   int max_age = -1,
                   bool secure = false,
                   bool http_only = true);
    
    // Compression helpers
    std::string compress_gzip(const std::string& data);
    std::string compress_deflate(const std::string& data);
    
    // Performance statistics
    struct BuilderStats {
        std::atomic<uint64_t> responses_built{0};
        std::atomic<uint64_t> total_build_time_us{0};
        std::atomic<uint64_t> compressed_responses{0};
        std::atomic<uint64_t> cached_responses{0};
        
        double average_build_time_us() const {
            auto built = responses_built.load();
            return built > 0 ? static_cast<double>(total_build_time_us.load()) / built : 0.0;
        }
    };
    
    const BuilderStats& get_stats() const { return stats_; }

private:
    BuilderStats stats_;
    
    // Pre-allocated buffers for performance
    std::string header_buffer_;
    std::string cookie_buffer_;
    
    // Fast header serialization
    void serialize_headers(const std::unordered_map<std::string, std::string>& headers,
                          std::string& buffer);
    void serialize_cookies(const std::vector<std::pair<std::string, std::string>>& cookies,
                          std::string& buffer);
    
    // Status code to message mapping
    std::string get_status_message(int status_code);
};

/**
 * URL routing and parameter extraction optimized for high performance.
 * Features:
 * - Trie-based route matching
 * - Fast parameter extraction
 * - Wildcard support
 * - Route caching
 */
class RouteParser {
public:
    struct Route {
        std::string pattern;
        std::vector<std::string> param_names;
        bool has_wildcards = false;
    };
    
    struct MatchResult {
        bool matched = false;
        std::string handler_id;
        std::unordered_map<std::string, std::string> params;
    };
    
    RouteParser();
    ~RouteParser() = default;
    
    // Route registration
    void add_route(const std::string& pattern, const std::string& handler_id);
    void remove_route(const std::string& pattern);
    
    // Route matching
    MatchResult match_route(const std::string& path);
    
    // Route utilities
    std::vector<std::string> extract_param_names(const std::string& pattern);
    bool is_route_pattern(const std::string& pattern);

private:
    std::vector<Route> routes_;
    
    // Fast pattern matching
    bool match_pattern(const std::string& pattern, 
                      const std::string& path,
                      std::unordered_map<std::string, std::string>& params);
};

} // namespace pyspeed