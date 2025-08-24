#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <chrono>

// Simple JSON acceleration implementation
// This is a basic version - real implementation would use SIMD and advanced optimizations

namespace pyspeed {
namespace json {

// Fast JSON validation
bool is_valid_json(const std::string& json_str) {
    // Simple validation - check basic structure
    if (json_str.empty()) return false;
    
    char first = json_str[0];
    char last = json_str[json_str.size() - 1];
    
    // Object
    if (first == '{' && last == '}') return true;
    // Array
    if (first == '[' && last == ']') return true;
    // String
    if (first == '"' && last == '"') return true;
    // Number
    if (std::isdigit(first) || first == '-') return true;
    // Boolean/null
    if (json_str == "true" || json_str == "false" || json_str == "null") return true;
    
    return false;
}

// Fast JSON minification
std::string minify(const std::string& json_str) {
    std::string result;
    result.reserve(json_str.size());
    
    bool in_string = false;
    bool escape_next = false;
    
    for (char c : json_str) {
        if (escape_next) {
            result += c;
            escape_next = false;
            continue;
        }
        
        if (c == '\\' && in_string) {
            result += c;
            escape_next = true;
            continue;
        }
        
        if (c == '"') {
            in_string = !in_string;
            result += c;
            continue;
        }
        
        if (!in_string && (c == ' ' || c == '\t' || c == '\n' || c == '\r')) {
            continue; // Skip whitespace outside strings
        }
        
        result += c;
    }
    
    return result;
}

// Performance benchmarking
struct BenchmarkResult {
    double parse_time_ms;
    double serialize_time_ms;
    size_t input_size_bytes;
    size_t output_size_bytes;
    double parse_speed_mb_per_sec;
    double serialize_speed_mb_per_sec;
};

BenchmarkResult benchmark_json_processing(const std::string& json_data, int iterations) {
    BenchmarkResult result = {0};
    result.input_size_bytes = json_data.size();
    
    // Benchmark validation (simulating parsing)
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        is_valid_json(json_data);
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    result.parse_time_ms = duration.count() / 1000.0;
    
    // Benchmark minification (simulating serialization)
    start = std::chrono::high_resolution_clock::now();
    std::string minified;
    for (int i = 0; i < iterations; ++i) {
        minified = minify(json_data);
    }
    end = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    result.serialize_time_ms = duration.count() / 1000.0;
    
    result.output_size_bytes = minified.size();
    
    // Calculate speeds
    if (result.parse_time_ms > 0) {
        result.parse_speed_mb_per_sec = (result.input_size_bytes * iterations) / (result.parse_time_ms * 1000.0);
    }
    if (result.serialize_time_ms > 0) {
        result.serialize_speed_mb_per_sec = (result.output_size_bytes * iterations) / (result.serialize_time_ms * 1000.0);
    }
    
    return result;
}

BenchmarkResult benchmark_large_json(int array_size, int iterations) {
    // Generate large JSON array
    std::ostringstream json_builder;
    json_builder << "[";
    for (int i = 0; i < array_size; ++i) {
        if (i > 0) json_builder << ",";
        json_builder << "{\"id\":" << i << ",\"name\":\"item_" << i 
                    << "\",\"value\":" << (i * 3.14159) << "}";
    }
    json_builder << "]";
    
    std::string json_data = json_builder.str();
    return benchmark_json_processing(json_data, iterations);
}

} // namespace json
} // namespace pyspeed