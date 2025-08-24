#include "json_accelerator.hpp"
#include <sstream>
#include <fstream>
#include <algorithm>
#include <cstring>
#include <cctype>
#include <iomanip>
#include <stdexcept>

namespace pyspeed {

// JsonValue Implementation
size_t JsonValue::size() const {
    if (is_array()) {
        return as_array().size();
    } else if (is_object()) {
        return as_object().size();
    }
    return 0;
}

JsonValue& JsonValue::operator[](size_t index) {
    if (!is_array()) {
        throw std::runtime_error("JsonValue is not an array");
    }
    auto& arr = pyspeed_detail::get<JsonArray>(value_);
    if (index >= arr.size()) {
        throw std::out_of_range("Array index out of bounds");
    }
    return arr[index];
}

const JsonValue& JsonValue::operator[](size_t index) const {
    if (!is_array()) {
        throw std::runtime_error("JsonValue is not an array");
    }
    const auto& arr = pyspeed_detail::get<JsonArray>(value_);
    if (index >= arr.size()) {
        throw std::out_of_range("Array index out of bounds");
    }
    return arr[index];
}

void JsonValue::push_back(const JsonValue& value) {
    if (!is_array()) {
        throw std::runtime_error("JsonValue is not an array");
    }
    pyspeed_detail::get<JsonArray>(value_).push_back(value);
}

void JsonValue::push_back(JsonValue&& value) {
    if (!is_array()) {
        throw std::runtime_error("JsonValue is not an array");
    }
    pyspeed_detail::get<JsonArray>(value_).push_back(std::move(value));
}

JsonValue& JsonValue::operator[](const std::string& key) {
    if (!is_object()) {
        // Convert to object if null, otherwise throw
        if (is_null()) {
            value_ = JsonObject{};
        } else {
            throw std::runtime_error("JsonValue is not an object");
        }
    }
    return pyspeed_detail::get<JsonObject>(value_)[key];
}

const JsonValue& JsonValue::operator[](const std::string& key) const {
    if (!is_object()) {
        throw std::runtime_error("JsonValue is not an object");
    }
    const auto& obj = pyspeed_detail::get<JsonObject>(value_);
    auto it = obj.find(key);
    if (it == obj.end()) {
        throw std::out_of_range("Object key not found: " + key);
    }
    return it->second;
}

bool JsonValue::has_key(const std::string& key) const {
    if (!is_object()) {
        return false;
    }
    const auto& obj = pyspeed_detail::get<JsonObject>(value_);
    return obj.find(key) != obj.end();
}

void JsonValue::set(const std::string& key, const JsonValue& value) {
    if (!is_object()) {
        if (is_null()) {
            value_ = JsonObject{};
        } else {
            throw std::runtime_error("JsonValue is not an object");
        }
    }
    pyspeed_detail::get<JsonObject>(value_)[key] = value;
}

void JsonValue::set(const std::string& key, JsonValue&& value) {
    if (!is_object()) {
        if (is_null()) {
            value_ = JsonObject{};
        } else {
            throw std::runtime_error("JsonValue is not an object");
        }
    }
    pyspeed_detail::get<JsonObject>(value_)[key] = std::move(value);
}

std::string JsonValue::type_name() const {
    return pyspeed_detail::visit([](const auto& value) -> std::string {
        using T = std::decay_t<decltype(value)>;
        if (std::is_same<T, JsonNull>::value) return "null";
        else if (std::is_same<T, JsonBool>::value) return "bool";
        else if (std::is_same<T, JsonNumber>::value) return "number";
        else if (std::is_same<T, JsonString>::value) return "string";
        else if (std::is_same<T, JsonArray>::value) return "array";
        else if (std::is_same<T, JsonObject>::value) return "object";
        else return "unknown";
    }, value_);
}

// JsonParser Implementation
JsonParser::JsonParser() = default;

JsonValue JsonParser::parse(const std::string& json_str) {
    return parse(json_str.c_str(), json_str.length());
}

JsonValue JsonParser::parse(const char* json_str, size_t length) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    try {
        const char* ptr = json_str;
        const char* end = json_str + length;
        
        skip_whitespace(ptr, end);
        
        if (ptr >= end) {
            throw std::runtime_error("Empty JSON document");
        }
        
        JsonValue result = parse_value(ptr, end);
        
        skip_whitespace(ptr, end);
        if (ptr < end && config_.strict_mode) {
            throw std::runtime_error("Unexpected content after JSON document");
        }
        
        // Update statistics
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time);
        
        stats_.documents_parsed.fetch_add(1);
        stats_.total_parse_time_ns.fetch_add(duration.count());
        stats_.bytes_parsed.fetch_add(length);
        
        return result;
        
    } catch (...) {
        stats_.parse_errors.fetch_add(1);
        throw;
    }
}

JsonValue JsonParser::parse_value(const char*& ptr, const char* end) {
    skip_whitespace(ptr, end);
    
    if (ptr >= end) {
        throw std::runtime_error("Unexpected end of input");
    }
    
    switch (*ptr) {
        case '{':
            return parse_object(ptr, end);
        case '[':
            return parse_array(ptr, end);
        case '"':
            return parse_string(ptr, end);
        case 't':
        case 'f':
        case 'n':
            return parse_literal(ptr, end);
        default:
            if (*ptr == '-' || is_digit(*ptr)) {
                return parse_number(ptr, end);
            }
            throw std::runtime_error("Unexpected character: " + std::string(1, *ptr));
    }
}

JsonValue JsonParser::parse_object(const char*& ptr, const char* end) {
    if (ptr >= end || *ptr != '{') {
        throw std::runtime_error("Expected '{'");
    }
    
    ++ptr; // Skip '{'
    skip_whitespace(ptr, end);
    
    JsonObject obj;
    
    // Handle empty object
    if (ptr < end && *ptr == '}') {
        ++ptr;
        return JsonValue(std::move(obj));
    }
    
    while (ptr < end) {
        // Parse key
        skip_whitespace(ptr, end);
        if (ptr >= end || *ptr != '"') {
            throw std::runtime_error("Expected string key");
        }
        
        JsonValue key_value = parse_string(ptr, end);
        std::string key = key_value.as_string();
        
        // Parse colon
        skip_whitespace(ptr, end);
        if (ptr >= end || *ptr != ':') {
            throw std::runtime_error("Expected ':'");
        }
        ++ptr;
        
        // Parse value
        JsonValue value = parse_value(ptr, end);
        obj[key] = std::move(value);
        
        // Check for continuation
        skip_whitespace(ptr, end);
        if (ptr >= end) {
            throw std::runtime_error("Unexpected end of object");
        }
        
        if (*ptr == '}') {
            ++ptr;
            break;
        } else if (*ptr == ',') {
            ++ptr;
            if (config_.allow_trailing_commas) {
                skip_whitespace(ptr, end);
                if (ptr < end && *ptr == '}') {
                    ++ptr;
                    break;
                }
            }
        } else {
            throw std::runtime_error("Expected ',' or '}'");
        }
    }
    
    return JsonValue(std::move(obj));
}

JsonValue JsonParser::parse_array(const char*& ptr, const char* end) {
    if (ptr >= end || *ptr != '[') {
        throw std::runtime_error("Expected '['");
    }
    
    ++ptr; // Skip '['
    skip_whitespace(ptr, end);
    
    JsonArray arr;
    
    // Handle empty array
    if (ptr < end && *ptr == ']') {
        ++ptr;
        return JsonValue(std::move(arr));
    }
    
    while (ptr < end) {
        // Parse value
        JsonValue value = parse_value(ptr, end);
        arr.push_back(std::move(value));
        
        // Check for continuation
        skip_whitespace(ptr, end);
        if (ptr >= end) {
            throw std::runtime_error("Unexpected end of array");
        }
        
        if (*ptr == ']') {
            ++ptr;
            break;
        } else if (*ptr == ',') {
            ++ptr;
            if (config_.allow_trailing_commas) {
                skip_whitespace(ptr, end);
                if (ptr < end && *ptr == ']') {
                    ++ptr;
                    break;
                }
            }
        } else {
            throw std::runtime_error("Expected ',' or ']'");
        }
    }
    
    return JsonValue(std::move(arr));
}

JsonValue JsonParser::parse_string(const char*& ptr, const char* end) {
    if (ptr >= end || *ptr != '"') {
        throw std::runtime_error("Expected '\"'");
    }
    
    const char* start = ++ptr; // Skip opening quote
    
    // Find end of string (optimized version)
    while (ptr < end && *ptr != '"') {
        if (*ptr == '\\') {
            ++ptr; // Skip escape character
            if (ptr >= end) {
                throw std::runtime_error("Unterminated string escape");
            }
        }
        ++ptr;
    }
    
    if (ptr >= end) {
        throw std::runtime_error("Unterminated string");
    }
    
    std::string result = decode_string(start, ptr);
    ++ptr; // Skip closing quote
    
    return JsonValue(std::move(result));
}

JsonValue JsonParser::parse_number(const char*& ptr, const char* end) {
    const char* start = ptr;
    
    // Handle negative sign
    if (ptr < end && *ptr == '-') {
        ++ptr;
    }
    
    // Parse integer part
    if (ptr >= end || !is_digit(*ptr)) {
        throw std::runtime_error("Invalid number");
    }
    
    if (*ptr == '0') {
        ++ptr;
    } else {
        while (ptr < end && is_digit(*ptr)) {
            ++ptr;
        }
    }
    
    // Parse decimal part
    if (ptr < end && *ptr == '.') {
        ++ptr;
        if (ptr >= end || !is_digit(*ptr)) {
            throw std::runtime_error("Invalid decimal number");
        }
        while (ptr < end && is_digit(*ptr)) {
            ++ptr;
        }
    }
    
    // Parse exponent
    if (ptr < end && (*ptr == 'e' || *ptr == 'E')) {
        ++ptr;
        if (ptr < end && (*ptr == '+' || *ptr == '-')) {
            ++ptr;
        }
        if (ptr >= end || !is_digit(*ptr)) {
            throw std::runtime_error("Invalid number exponent");
        }
        while (ptr < end && is_digit(*ptr)) {
            ++ptr;
        }
    }
    
    // Convert to double
    std::string number_str(start, ptr);
    double value = std::stod(number_str);
    
    return JsonValue(value);
}

JsonValue JsonParser::parse_literal(const char*& ptr, const char* end) {
    if (ptr + 4 <= end && std::strncmp(ptr, "true", 4) == 0) {
        ptr += 4;
        return JsonValue(true);
    } else if (ptr + 5 <= end && std::strncmp(ptr, "false", 5) == 0) {
        ptr += 5;
        return JsonValue(false);
    } else if (ptr + 4 <= end && std::strncmp(ptr, "null", 4) == 0) {
        ptr += 4;
        return JsonValue(nullptr);
    } else {
        throw std::runtime_error("Invalid literal");
    }
}

void JsonParser::skip_whitespace(const char*& ptr, const char* end) {
    if (config_.use_simd) {
        skip_whitespace_simd(ptr, end);
        return;
    }
    
    while (ptr < end && is_whitespace(*ptr)) {
        ++ptr;
    }
    
    if (config_.allow_comments) {
        skip_comments(ptr, end);
    }
}

void JsonParser::skip_comments(const char*& ptr, const char* end) {
    while (ptr < end && *ptr == '/') {
        if (ptr + 1 < end && ptr[1] == '/') {
            // Line comment
            ptr += 2;
            while (ptr < end && *ptr != '\n') {
                ++ptr;
            }
        } else if (ptr + 1 < end && ptr[1] == '*') {
            // Block comment
            ptr += 2;
            while (ptr + 1 < end) {
                if (*ptr == '*' && ptr[1] == '/') {
                    ptr += 2;
                    break;
                }
                ++ptr;
            }
        } else {
            break;
        }
        skip_whitespace(ptr, end);
    }
}

bool JsonParser::is_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

bool JsonParser::is_digit(char c) {
    return c >= '0' && c <= '9';
}

std::string JsonParser::decode_string(const char* start, const char* end) {
    std::string result;
    result.reserve(end - start);
    
    for (const char* ptr = start; ptr < end; ++ptr) {
        if (*ptr == '\\') {
            ++ptr;
            if (ptr >= end) {
                throw std::runtime_error("Unterminated escape sequence");
            }
            
            switch (*ptr) {
                case '"': result += '"'; break;
                case '\\': result += '\\'; break;
                case '/': result += '/'; break;
                case 'b': result += '\b'; break;
                case 'f': result += '\f'; break;
                case 'n': result += '\n'; break;
                case 'r': result += '\r'; break;
                case 't': result += '\t'; break;
                case 'u':
                    // Unicode escape (simplified)
                    if (ptr + 4 >= end) {
                        throw std::runtime_error("Invalid unicode escape");
                    }
                    ptr += 4; // Skip the 4 hex digits for now
                    result += '?'; // Placeholder
                    break;
                default:
                    throw std::runtime_error("Invalid escape character");
            }
        } else {
            result += *ptr;
        }
    }
    
    return result;
}

void JsonParser::skip_whitespace_simd(const char*& ptr, const char* end) {
    // Simplified SIMD implementation - in practice would use actual SIMD instructions
    while (ptr < end && is_whitespace(*ptr)) {
        ++ptr;
    }
}

// JsonSerializer Implementation
JsonSerializer::JsonSerializer() = default;

std::string JsonSerializer::serialize(const JsonValue& value) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    try {
        std::string result;
        result.reserve(1024); // Start with reasonable capacity
        
        serialize_value(value, result);
        
        // Update statistics
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time);
        
        stats_.documents_serialized.fetch_add(1);
        stats_.total_serialize_time_ns.fetch_add(duration.count());
        stats_.bytes_serialized.fetch_add(result.size());
        
        return result;
        
    } catch (...) {
        stats_.serialize_errors.fetch_add(1);
        throw;
    }
}

void JsonSerializer::serialize(const JsonValue& value, std::string& output) {
    serialize_value(value, output);
}

void JsonSerializer::serialize_value(const JsonValue& value, std::string& output, int depth) {
    if (value.is_null()) {
        output += "null";
    } else if (value.is_bool()) {
        output += value.as_bool() ? "true" : "false";
    } else if (value.is_number()) {
        serialize_number(value.as_number(), output);
    } else if (value.is_string()) {
        serialize_string(value.as_string(), output);
    } else if (value.is_array()) {
        serialize_array(value.as_array(), output, depth);
    } else if (value.is_object()) {
        serialize_object(value.as_object(), output, depth);
    }
}

void JsonSerializer::serialize_object(const JsonObject& obj, std::string& output, int depth) {
    output += "{";
    
    if (config_.pretty_print) {
        output += "\n";
    }
    
    bool first = true;
    for (const auto& [key, value] : obj) {
        if (!first) {
            output += ",";
            if (config_.pretty_print) {
                output += "\n";
            }
        }
        first = false;
        
        if (config_.pretty_print) {
            add_indent(output, depth + 1);
        }
        
        serialize_string(key, output);
        output += ":";
        
        if (config_.pretty_print) {
            output += " ";
        }
        
        serialize_value(value, output, depth + 1);
    }
    
    if (config_.pretty_print && !obj.empty()) {
        output += "\n";
        add_indent(output, depth);
    }
    
    output += "}";
}

void JsonSerializer::serialize_array(const JsonArray& arr, std::string& output, int depth) {
    output += "[";
    
    if (config_.pretty_print && !arr.empty()) {
        output += "\n";
    }
    
    for (size_t i = 0; i < arr.size(); ++i) {
        if (i > 0) {
            output += ",";
            if (config_.pretty_print) {
                output += "\n";
            }
        }
        
        if (config_.pretty_print) {
            add_indent(output, depth + 1);
        }
        
        serialize_value(arr[i], output, depth + 1);
    }
    
    if (config_.pretty_print && !arr.empty()) {
        output += "\n";
        add_indent(output, depth);
    }
    
    output += "]";
}

void JsonSerializer::serialize_string(const std::string& str, std::string& output) {
    output += "\"";
    escape_string(str, output);
    output += "\"";
}

void JsonSerializer::serialize_number(double num, std::string& output) {
    // Check if it's an integer
    if (num == static_cast<long long>(num)) {
        output += std::to_string(static_cast<long long>(num));
    } else {
        std::ostringstream oss;
        oss << std::setprecision(15) << num;
        output += oss.str();
    }
}

void JsonSerializer::add_indent(std::string& output, int depth) {
    for (int i = 0; i < depth * config_.indent_size; ++i) {
        output += " ";
    }
}

void JsonSerializer::escape_string(const std::string& str, std::string& output) {
    for (char c : str) {
        if (needs_escaping(c)) {
            output += "\\";
            switch (c) {
                case '"': output += "\""; break;
                case '\\': output += "\\"; break;
                case '\b': output += "b"; break;
                case '\f': output += "f"; break;
                case '\n': output += "n"; break;
                case '\r': output += "r"; break;
                case '\t': output += "t"; break;
                default:
                    // Unicode escape for control characters
                    output += "u";
                    std::ostringstream oss;
                    oss << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
                    output += oss.str();
                    break;
            }
        } else {
            output += c;
        }
    }
}

bool JsonSerializer::needs_escaping(char c) {
    return c == '"' || c == '\\' || c < 0x20;
}

// Convenience functions
namespace json {

JsonValue parse(const std::string& json_str) {
    JsonParser parser;
    return parser.parse(json_str);
}

JsonValue parse_file(const std::string& filename) {
    std::ifstream file(filename);
    if (!file) {
        throw std::runtime_error("Cannot open file: " + filename);
    }
    
    std::string content((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
    
    return parse(content);
}

std::string dumps(const JsonValue& value, bool pretty) {
    JsonSerializer serializer;
    JsonSerializer::Config config;
    config.pretty_print = pretty;
    serializer.set_config(config);
    
    return serializer.serialize(value);
}

void dump_file(const JsonValue& value, const std::string& filename, bool pretty) {
    std::ofstream file(filename);
    if (!file) {
        throw std::runtime_error("Cannot create file: " + filename);
    }
    
    file << dumps(value, pretty);
}

bool is_valid_json(const std::string& json_str) {
    try {
        parse(json_str);
        return true;
    } catch (...) {
        return false;
    }
}

std::string validate_json(const std::string& json_str) {
    try {
        parse(json_str);
        return ""; // Valid
    } catch (const std::exception& e) {
        return e.what();
    }
}

JsonValue from_python_dict(const std::unordered_map<std::string, std::string>& dict) {
    JsonObject obj;
    for (const auto& [key, value] : dict) {
        obj[key] = JsonValue(value);
    }
    return JsonValue(std::move(obj));
}

JsonValue from_python_list(const std::vector<std::string>& list) {
    JsonArray arr;
    for (const auto& item : list) {
        arr.push_back(JsonValue(item));
    }
    return JsonValue(std::move(arr));
}

BenchmarkResult benchmark_json_processing(const std::string& json_data, int iterations) {
    JsonParser parser;
    JsonSerializer serializer;
    
    // Parse benchmark
    auto parse_start = std::chrono::high_resolution_clock::now();
    JsonValue parsed_value;
    
    for (int i = 0; i < iterations; ++i) {
        parsed_value = parser.parse(json_data);
    }
    
    auto parse_end = std::chrono::high_resolution_clock::now();
    auto parse_duration = std::chrono::duration_cast<std::chrono::nanoseconds>(parse_end - parse_start);
    
    // Serialize benchmark
    auto serialize_start = std::chrono::high_resolution_clock::now();
    std::string serialized;
    
    for (int i = 0; i < iterations; ++i) {
        serialized = serializer.serialize(parsed_value);
    }
    
    auto serialize_end = std::chrono::high_resolution_clock::now();
    auto serialize_duration = std::chrono::duration_cast<std::chrono::nanoseconds>(serialize_end - serialize_start);
    
    BenchmarkResult result;
    result.parse_time_ms = parse_duration.count() / (1e6 * iterations);
    result.serialize_time_ms = serialize_duration.count() / (1e6 * iterations);
    result.input_size_bytes = json_data.size();
    result.output_size_bytes = serialized.size();
    result.parse_speed_mb_per_sec = (json_data.size() / (1024.0 * 1024.0)) / (result.parse_time_ms / 1000.0);
    result.serialize_speed_mb_per_sec = (serialized.size() / (1024.0 * 1024.0)) / (result.serialize_time_ms / 1000.0);
    
    return result;
}

BenchmarkResult benchmark_large_json(size_t array_size, int iterations) {
    // Create large JSON array
    JsonArray large_array;
    for (size_t i = 0; i < array_size; ++i) {
        JsonObject obj;
        obj["id"] = JsonValue(static_cast<double>(i));
        obj["name"] = JsonValue("item_" + std::to_string(i));
        obj["value"] = JsonValue(i * 3.14159);
        obj["active"] = JsonValue(i % 2 == 0);
        large_array.push_back(JsonValue(std::move(obj)));
    }
    
    JsonValue large_json(std::move(large_array));
    
    // Serialize first to get JSON string
    JsonSerializer serializer;
    std::string json_string = serializer.serialize(large_json);
    
    return benchmark_json_processing(json_string, iterations);
}

} // namespace json

} // namespace pyspeed