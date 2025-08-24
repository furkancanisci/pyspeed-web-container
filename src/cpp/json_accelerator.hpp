#pragma once

#include "compatibility.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <chrono>
#include <atomic>
#include <functional>
#include <stdexcept>
#include <cstdint>
#include <type_traits>

#if PYSPEED_HAS_VARIANT
    #include <variant>
    namespace pyspeed_detail {
        using std::variant;
        using std::holds_alternative;
        using std::get;
        using std::visit;
    }
#else
    namespace pyspeed_detail {
        using pyspeed_compat::variant;
        using pyspeed_compat::holds_alternative;
        template<typename T, typename Variant>
        T& get(Variant& v) { return v.template get<T>(); }
        template<typename T, typename Variant>
        const T& get(const Variant& v) { return v.template get<T>(); }
        using pyspeed_compat::visit;
    }
#endif

namespace pyspeed {

/**
 * High-performance JSON processing module for PySpeed Web Container.
 * 
 * Designed to provide massive speedups over Python's json module,
 * similar to the performance gains seen in cpythonwrapper.
 * 
 * Performance targets:
 * - 50-500x faster than Python json module
 * - Zero-copy operations where possible
 * - SIMD optimizations for large data
 * - Memory-efficient parsing and serialization
 */

// Forward declarations
class JsonValue;
class JsonParser;
class JsonSerializer;

// JSON value types (using variant for type safety)
using JsonNull = std::nullptr_t;
using JsonBool = bool;
using JsonNumber = double;
using JsonString = std::string;
using JsonArray = std::vector<JsonValue>;
using JsonObject = std::unordered_map<std::string, JsonValue>;

/**
 * High-performance JSON value container.
 * Uses std::variant for efficient storage without virtual function overhead.
 */
class JsonValue {
public:
    using ValueType = pyspeed_detail::variant<JsonNull, JsonBool, JsonNumber, JsonString, JsonArray, JsonObject>;
    
    // Constructors
    JsonValue() : value_(nullptr) {}
    JsonValue(std::nullptr_t) : value_(nullptr) {}
    JsonValue(bool b) : value_(b) {}
    JsonValue(int i) : value_(static_cast<double>(i)) {}
    JsonValue(double d) : value_(d) {}
    JsonValue(const std::string& s) : value_(s) {}
    JsonValue(std::string&& s) : value_(std::move(s)) {}
    JsonValue(const char* s) : value_(std::string(s)) {}
    JsonValue(const JsonArray& a) : value_(a) {}
    JsonValue(JsonArray&& a) : value_(std::move(a)) {}
    JsonValue(const JsonObject& o) : value_(o) {}
    JsonValue(JsonObject&& o) : value_(std::move(o)) {}
    
    // Type checking
    bool is_null() const { return pyspeed_detail::holds_alternative<JsonNull>(value_); }
    bool is_bool() const { return pyspeed_detail::holds_alternative<JsonBool>(value_); }
    bool is_number() const { return pyspeed_detail::holds_alternative<JsonNumber>(value_); }
    bool is_string() const { return pyspeed_detail::holds_alternative<JsonString>(value_); }
    bool is_array() const { return pyspeed_detail::holds_alternative<JsonArray>(value_); }
    bool is_object() const { return pyspeed_detail::holds_alternative<JsonObject>(value_); }
    
    // Value access (with bounds checking)
    bool as_bool() const { return pyspeed_detail::get<JsonBool>(value_); }
    double as_number() const { return pyspeed_detail::get<JsonNumber>(value_); }
    int as_int() const { return static_cast<int>(pyspeed_detail::get<JsonNumber>(value_)); }
    const std::string& as_string() const { return pyspeed_detail::get<JsonString>(value_); }
    const JsonArray& as_array() const { return pyspeed_detail::get<JsonArray>(value_); }
    const JsonObject& as_object() const { return pyspeed_detail::get<JsonObject>(value_); }
    
    // Mutable access
    JsonArray& as_array() { return pyspeed_detail::get<JsonArray>(value_); }
    JsonObject& as_object() { return pyspeed_detail::get<JsonObject>(value_); }
    
    // Array operations
    size_t size() const;
    JsonValue& operator[](size_t index);
    const JsonValue& operator[](size_t index) const;
    void push_back(const JsonValue& value);
    void push_back(JsonValue&& value);
    
    // Object operations
    JsonValue& operator[](const std::string& key);
    const JsonValue& operator[](const std::string& key) const;
    bool has_key(const std::string& key) const;
    void set(const std::string& key, const JsonValue& value);
    void set(const std::string& key, JsonValue&& value);
    
    // Utility
    std::string type_name() const;
    
private:
    ValueType value_;
};

/**
 * High-performance JSON parser with zero-copy optimizations.
 * Designed for maximum speed with large JSON payloads.
 */
class JsonParser {
public:
    JsonParser();
    ~JsonParser() = default;
    
    // Main parsing functions
    JsonValue parse(const std::string& json_str);
    JsonValue parse(const char* json_str, size_t length);
    
    // Streaming parser for large files
    class StreamParser {
    public:
        explicit StreamParser(JsonParser* parser);
        bool feed(const char* data, size_t length);
        JsonValue finish();
        void reset();
        
    private:
        JsonParser* parser_;
        std::string buffer_;
        bool complete_;
    };
    
    std::unique_ptr<StreamParser> create_stream_parser();
    
    // Parser configuration
    struct Config {
        bool allow_comments = false;
        bool allow_trailing_commas = false;
        bool strict_mode = true;
        size_t max_depth = 100;
        size_t max_string_length = 1024 * 1024; // 1MB
        bool use_simd = true;
    };
    
    void set_config(const Config& config) { config_ = config; }
    const Config& get_config() const { return config_; }
    
    // Performance statistics
    struct ParserStats {
        std::atomic<uint64_t> documents_parsed{0};
        std::atomic<uint64_t> total_parse_time_ns{0};
        std::atomic<uint64_t> bytes_parsed{0};
        std::atomic<uint64_t> parse_errors{0};
        
        double average_parse_time_ms() const {
            auto parsed = documents_parsed.load();
            return parsed > 0 ? static_cast<double>(total_parse_time_ns.load()) / (parsed * 1e6) : 0.0;
        }
        
        double parse_speed_mb_per_sec() const {
            auto total_time_s = total_parse_time_ns.load() / 1e9;
            auto total_mb = bytes_parsed.load() / (1024.0 * 1024.0);
            return total_time_s > 0 ? total_mb / total_time_s : 0.0;
        }
    };
    
    const ParserStats& get_stats() const { return stats_; }

private:
    Config config_;
    ParserStats stats_;
    
    // Internal parsing functions
    JsonValue parse_value(const char*& ptr, const char* end);
    JsonValue parse_object(const char*& ptr, const char* end);
    JsonValue parse_array(const char*& ptr, const char* end);
    JsonValue parse_string(const char*& ptr, const char* end);
    JsonValue parse_number(const char*& ptr, const char* end);
    JsonValue parse_literal(const char*& ptr, const char* end);
    
    // Utility functions
    void skip_whitespace(const char*& ptr, const char* end);
    void skip_comments(const char*& ptr, const char* end);
    bool is_whitespace(char c);
    bool is_digit(char c);
    std::string decode_string(const char* start, const char* end);
    
    // SIMD optimizations
    void skip_whitespace_simd(const char*& ptr, const char* end);
    const char* find_string_end_simd(const char* start, const char* end);
};

/**
 * High-performance JSON serializer with memory-efficient output.
 * Optimized for web API responses and large data structures.
 */
class JsonSerializer {
public:
    JsonSerializer();
    ~JsonSerializer() = default;
    
    // Main serialization functions
    std::string serialize(const JsonValue& value);
    void serialize(const JsonValue& value, std::string& output);
    
    // Streaming serializer for large data
    class StreamSerializer {
    public:
        explicit StreamSerializer(JsonSerializer* serializer);
        void begin_object();
        void end_object();
        void begin_array();
        void end_array();
        void write_key(const std::string& key);
        void write_value(const JsonValue& value);
        void write_separator();
        std::string finish();
        void reset();
        
    private:
        JsonSerializer* serializer_;
        std::string buffer_;
        std::vector<bool> in_object_stack_;
        bool needs_separator_;
    };
    
    std::unique_ptr<StreamSerializer> create_stream_serializer();
    
    // Serializer configuration
    struct Config {
        bool pretty_print = false;
        int indent_size = 2;
        bool escape_unicode = false;
        bool sort_keys = false;
        bool ensure_ascii = false;
    };
    
    void set_config(const Config& config) { config_ = config; }
    const Config& get_config() const { return config_; }
    
    // Performance statistics
    struct SerializerStats {
        std::atomic<uint64_t> documents_serialized{0};
        std::atomic<uint64_t> total_serialize_time_ns{0};
        std::atomic<uint64_t> bytes_serialized{0};
        std::atomic<uint64_t> serialize_errors{0};
        
        double average_serialize_time_ms() const {
            auto serialized = documents_serialized.load();
            return serialized > 0 ? static_cast<double>(total_serialize_time_ns.load()) / (serialized * 1e6) : 0.0;
        }
        
        double serialize_speed_mb_per_sec() const {
            auto total_time_s = total_serialize_time_ns.load() / 1e9;
            auto total_mb = bytes_serialized.load() / (1024.0 * 1024.0);
            return total_time_s > 0 ? total_mb / total_time_s : 0.0;
        }
    };
    
    const SerializerStats& get_stats() const { return stats_; }

private:
    Config config_;
    SerializerStats stats_;
    
    // Internal serialization functions
    void serialize_value(const JsonValue& value, std::string& output, int depth = 0);
    void serialize_object(const JsonObject& obj, std::string& output, int depth);
    void serialize_array(const JsonArray& arr, std::string& output, int depth);
    void serialize_string(const std::string& str, std::string& output);
    void serialize_number(double num, std::string& output);
    
    // Utility functions
    void add_indent(std::string& output, int depth);
    void escape_string(const std::string& str, std::string& output);
    bool needs_escaping(char c);
};

/**
 * Convenience functions for common JSON operations.
 * These provide the main API used by the Python bridge.
 */
namespace json {
    // Parse functions
    JsonValue parse(const std::string& json_str);
    JsonValue parse_file(const std::string& filename);
    
    // Serialize functions
    std::string dumps(const JsonValue& value, bool pretty = false);
    void dump_file(const JsonValue& value, const std::string& filename, bool pretty = false);
    
    // Validation
    bool is_valid_json(const std::string& json_str);
    std::string validate_json(const std::string& json_str); // Returns error message if invalid
    
    // Type conversion helpers
    JsonValue from_python_dict(const std::unordered_map<std::string, std::string>& dict);
    JsonValue from_python_list(const std::vector<std::string>& list);
    
    // Performance testing (similar to cpythonwrapper benchmarks)
    struct BenchmarkResult {
        double parse_time_ms;
        double serialize_time_ms;
        size_t input_size_bytes;
        size_t output_size_bytes;
        double parse_speed_mb_per_sec;
        double serialize_speed_mb_per_sec;
    };
    
    BenchmarkResult benchmark_json_processing(const std::string& json_data, int iterations = 1000);
    BenchmarkResult benchmark_large_json(size_t array_size, int iterations = 100);
}

} // namespace pyspeed