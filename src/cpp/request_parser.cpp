#include "request_parser.hpp"
#include <algorithm>
#include <sstream>
#include <regex>
#include <cctype>
#include <chrono>

namespace pyspeed {

// RequestParser Implementation
RequestParser::RequestParser() = default;

RequestParser::ParsedRequest RequestParser::parse(const http::request<http::string_body>& request) {
    auto start_time = std::chrono::steady_clock::now();
    
    ParsedRequest result;
    result.parse_start = start_time;
    
    // Extract basic HTTP information
    result.method = std::string(request.method_string());
    result.path = std::string(request.target());
    result.protocol_version = "HTTP/" + std::to_string(request.version() / 10) + "." + 
                             std::to_string(request.version() % 10);
    
    // Split path and query string
    auto query_pos = result.path.find('?');
    if (query_pos != std::string::npos) {
        result.query_string = result.path.substr(query_pos + 1);
        result.path = result.path.substr(0, query_pos);
    }
    
    // Parse components
    parse_headers(request, result);
    parse_query_string(result.query_string, result);
    parse_url_params(result.path, result);
    
    // Parse body content
    result.body = request.body();
    result.content_length = result.body.size();
    
    // Get content type
    auto content_type_iter = result.headers.find("content-type");
    if (content_type_iter != result.headers.end()) {
        result.content_type = content_type_iter->second;
        
        // Parse form data if applicable
        if (result.content_type.find("application/x-www-form-urlencoded") != std::string::npos) {
            parse_form_data(result.body, result.content_type, result);
            stats_.form_requests.fetch_add(1);
        } else if (result.content_type.find("multipart/form-data") != std::string::npos) {
            // TODO: Implement multipart parsing
            stats_.multipart_requests.fetch_add(1);
        }
    }
    
    // Check if body is valid JSON
    if (!result.body.empty()) {
        result.is_valid_json = is_json_content(result.body);
        if (result.is_valid_json) {
            stats_.json_requests.fetch_add(1);
        }
    }
    
    // Parse cookies
    auto cookie_iter = result.headers.find("cookie");
    if (cookie_iter != result.headers.end()) {
        parse_cookies(cookie_iter->second, result);
    }
    
    // Calculate parse duration
    auto end_time = std::chrono::steady_clock::now();
    result.parse_duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    
    // Update statistics
    stats_.requests_parsed.fetch_add(1);
    stats_.total_parse_time_us.fetch_add(result.parse_duration.count());
    
    return result;
}

void RequestParser::parse_headers(const http::request<http::string_body>& request, ParsedRequest& result) {
    for (const auto& header : request) {
        std::string name = std::string(header.name_string());
        std::string value = std::string(header.value());
        
        // Convert header name to lowercase for case-insensitive lookup
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
        
        result.headers[name] = value;
    }
}

void RequestParser::parse_query_string(const std::string& query, ParsedRequest& result) {
    if (query.empty()) return;
    
    fast_split_query(query, result.query_params);
}

void RequestParser::parse_url_params(const std::string& path, ParsedRequest& result) {
    // Extract path parameters (e.g., /users/{id}/posts/{post_id})
    // This is a simplified implementation - in practice, you'd use the RouteParser
    
    // For now, just store the clean path
    result.path = path;
}

void RequestParser::parse_cookies(const std::string& cookie_header, ParsedRequest& result) {
    if (cookie_header.empty()) return;
    
    std::stringstream ss(cookie_header);
    std::string cookie_pair;
    
    while (std::getline(ss, cookie_pair, ';')) {
        // Trim whitespace
        cookie_pair.erase(0, cookie_pair.find_first_not_of(" \t"));
        cookie_pair.erase(cookie_pair.find_last_not_of(" \t") + 1);
        
        auto eq_pos = cookie_pair.find('=');
        if (eq_pos != std::string::npos) {
            std::string name = cookie_pair.substr(0, eq_pos);
            std::string value = cookie_pair.substr(eq_pos + 1);
            
            // URL decode the value
            value = url_decode(value);
            
            result.cookies[name] = value;
        }
    }
}

void RequestParser::parse_form_data(const std::string& body, const std::string& content_type, ParsedRequest& result) {
    if (body.empty()) return;
    
    if (content_type.find("application/x-www-form-urlencoded") != std::string::npos) {
        // Parse URL-encoded form data
        fast_split_params(body, result.form_data);
    }
    // TODO: Add multipart/form-data parsing
}

bool RequestParser::is_json_content(const std::string& body) {
    if (body.empty()) return false;
    
    // Simple JSON validation - check if it starts and ends with { } or [ ]
    std::string trimmed = body;
    trimmed.erase(0, trimmed.find_first_not_of(" \t\n\r"));
    trimmed.erase(trimmed.find_last_not_of(" \t\n\r") + 1);
    
    if (trimmed.empty()) return false;
    
    return (trimmed.front() == '{' && trimmed.back() == '}') ||
           (trimmed.front() == '[' && trimmed.back() == ']');
}

std::string RequestParser::url_decode(const std::string& encoded) {
    std::string decoded;
    decoded.reserve(encoded.length());
    
    for (size_t i = 0; i < encoded.length(); ++i) {
        if (encoded[i] == '%' && i + 2 < encoded.length()) {
            // Decode hex sequence
            std::string hex = encoded.substr(i + 1, 2);
            try {
                char ch = static_cast<char>(std::stoi(hex, nullptr, 16));
                decoded += ch;
                i += 2;
            } catch (...) {
                decoded += encoded[i];
            }
        } else if (encoded[i] == '+') {
            decoded += ' ';
        } else {
            decoded += encoded[i];
        }
    }
    
    return decoded;
}

std::vector<std::string> RequestParser::split_string(const std::string& str, char delimiter) {
    std::vector<std::string> result;
    std::stringstream ss(str);
    std::string item;
    
    while (std::getline(ss, item, delimiter)) {
        result.push_back(item);
    }
    
    return result;
}

void RequestParser::fast_split_params(const std::string& params_str, 
                                     std::unordered_map<std::string, std::string>& result) {
    std::stringstream ss(params_str);
    std::string pair;
    
    while (std::getline(ss, pair, '&')) {
        auto eq_pos = pair.find('=');
        if (eq_pos != std::string::npos) {
            std::string key = pair.substr(0, eq_pos);
            std::string value = pair.substr(eq_pos + 1);
            
            // URL decode both key and value
            key = url_decode(key);
            value = url_decode(value);
            
            result[key] = value;
        }
    }
}

void RequestParser::fast_split_query(const std::string& query_str, 
                                    std::unordered_map<std::string, std::vector<std::string>>& result) {
    std::stringstream ss(query_str);
    std::string pair;
    
    while (std::getline(ss, pair, '&')) {
        auto eq_pos = pair.find('=');
        if (eq_pos != std::string::npos) {
            std::string key = pair.substr(0, eq_pos);
            std::string value = pair.substr(eq_pos + 1);
            
            // URL decode both key and value
            key = url_decode(key);
            value = url_decode(value);
            
            result[key].push_back(value);
        }
    }
}

// ResponseBuilder Implementation
ResponseBuilder::ResponseBuilder() {
    // Pre-allocate buffers for performance
    header_buffer_.reserve(1024);
    cookie_buffer_.reserve(512);
}

http::response<http::string_body> ResponseBuilder::build_response(const ResponseData& data) {
    auto start_time = std::chrono::steady_clock::now();
    
    http::response<http::string_body> response{
        static_cast<http::status>(data.status_code), 11};
    
    // Set basic headers
    response.set(http::field::server, "PySpeed/1.0");
    response.set(http::field::content_length, std::to_string(data.body.size()));
    
    // Add custom headers
    for (const auto& [name, value] : data.headers) {
        response.set(name, value);
    }
    
    // Add cookies
    for (const auto& [name, value] : data.cookies) {
        response.set(http::field::set_cookie, name + "=" + value + "; Path=/");
    }
    
    // Set body
    response.body() = data.body;
    response.prepare_payload();
    
    // Update statistics
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    
    stats_.responses_built.fetch_add(1);
    stats_.total_build_time_us.fetch_add(duration.count());
    
    return response;
}

http::response<http::string_body> ResponseBuilder::build_json_response(
    const std::string& json_body, 
    int status_code,
    const std::unordered_map<std::string, std::string>& additional_headers) {
    
    ResponseData data;
    data.status_code = status_code;
    data.headers = additional_headers;
    data.headers["content-type"] = "application/json";
    data.body = json_body;
    
    return build_response(data);
}

http::response<http::string_body> ResponseBuilder::build_html_response(
    const std::string& html_body,
    int status_code,
    const std::unordered_map<std::string, std::string>& additional_headers) {
    
    ResponseData data;
    data.status_code = status_code;
    data.headers = additional_headers;
    data.headers["content-type"] = "text/html; charset=utf-8";
    data.body = html_body;
    
    return build_response(data);
}

http::response<http::string_body> ResponseBuilder::build_error_response(
    int status_code,
    const std::string& error_message) {
    
    std::string html_body = 
        "<!DOCTYPE html>\n"
        "<html><head><title>Error " + std::to_string(status_code) + "</title></head>\n"
        "<body><h1>Error " + std::to_string(status_code) + "</h1>\n"
        "<p>" + error_message + "</p>\n"
        "<hr><p>PySpeed Web Container</p></body></html>";
    
    return build_html_response(html_body, status_code);
}

http::response<http::string_body> ResponseBuilder::build_redirect_response(
    const std::string& location,
    int status_code) {
    
    ResponseData data;
    data.status_code = status_code;
    data.headers["location"] = location;
    data.headers["content-type"] = "text/html";
    data.body = "<!DOCTYPE html><html><head><title>Redirect</title></head>"
               "<body><p>Redirecting to <a href=\"" + location + "\">" + location + "</a></p></body></html>";
    
    return build_response(data);
}

void ResponseBuilder::add_cookie(ResponseData& response, 
                                const std::string& name, 
                                const std::string& value,
                                const std::string& path,
                                const std::string& domain,
                                int max_age,
                                bool secure,
                                bool http_only) {
    
    std::string cookie_value = name + "=" + value;
    
    if (!path.empty()) {
        cookie_value += "; Path=" + path;
    }
    
    if (!domain.empty()) {
        cookie_value += "; Domain=" + domain;
    }
    
    if (max_age >= 0) {
        cookie_value += "; Max-Age=" + std::to_string(max_age);
    }
    
    if (secure) {
        cookie_value += "; Secure";
    }
    
    if (http_only) {
        cookie_value += "; HttpOnly";
    }
    
    response.cookies.emplace_back(name, cookie_value);
}

std::string ResponseBuilder::get_status_message(int status_code) {
    switch (status_code) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 304: return "Not Modified";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 500: return "Internal Server Error";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        default: return "Unknown";
    }
}

// RouteParser Implementation
RouteParser::RouteParser() = default;

void RouteParser::add_route(const std::string& pattern, const std::string& handler_id) {
    Route route;
    route.pattern = pattern;
    route.handler_id = handler_id;
    route.param_names = extract_param_names(pattern);
    route.has_wildcards = pattern.find('{') != std::string::npos;
    
    routes_.push_back(route);
}

void RouteParser::remove_route(const std::string& pattern) {
    routes_.erase(
        std::remove_if(routes_.begin(), routes_.end(),
            [&pattern](const Route& route) { return route.pattern == pattern; }),
        routes_.end());
}

RouteParser::MatchResult RouteParser::match_route(const std::string& path) {
    MatchResult result;
    
    for (const auto& route : routes_) {
        if (match_pattern(route.pattern, path, result.params)) {
            result.matched = true;
            result.handler_id = route.handler_id;
            return result;
        }
    }
    
    return result; // No match found
}

std::vector<std::string> RouteParser::extract_param_names(const std::string& pattern) {
    std::vector<std::string> params;
    std::regex param_regex(R"(\{([^}]+)\})");
    std::sregex_iterator iter(pattern.begin(), pattern.end(), param_regex);
    std::sregex_iterator end;
    
    for (; iter != end; ++iter) {
        params.push_back((*iter)[1].str());
    }
    
    return params;
}

bool RouteParser::is_route_pattern(const std::string& pattern) {
    return pattern.find('{') != std::string::npos;
}

bool RouteParser::match_pattern(const std::string& pattern, 
                               const std::string& path,
                               std::unordered_map<std::string, std::string>& params) {
    
    // Simple pattern matching implementation
    // Convert pattern like "/users/{id}/posts/{post_id}" to regex
    std::string regex_pattern = pattern;
    
    // Escape special regex characters except {}
    std::regex escape_regex(R"([.^$|()[\]*+?\\])");
    regex_pattern = std::regex_replace(regex_pattern, escape_regex, R"(\$&)");
    
    // Replace {param} with capture groups
    std::regex param_regex(R"(\\\{([^}]+)\\\})");
    regex_pattern = std::regex_replace(regex_pattern, param_regex, "([^/]+)");
    
    // Add anchors
    regex_pattern = "^" + regex_pattern + "$";
    
    std::regex route_regex(regex_pattern);
    std::smatch matches;
    
    if (std::regex_match(path, matches, route_regex)) {
        // Extract parameter values
        auto param_names = extract_param_names(pattern);
        for (size_t i = 0; i < param_names.size() && i + 1 < matches.size(); ++i) {
            params[param_names[i]] = matches[i + 1].str();
        }
        return true;
    }
    
    return false;
}

} // namespace pyspeed