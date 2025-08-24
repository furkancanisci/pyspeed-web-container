#pragma once

#include "compatibility.hpp"
#include <string>
#include <unordered_map>
#include <memory>
#include <chrono>
#include <atomic>
#include <vector>
#include <thread>

#if PYSPEED_HAS_SHARED_MUTEX
    #include <shared_mutex>
#else
    #include <mutex>
#endif

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#endif

namespace pyspeed {

/**
 * High-performance static file serving with nginx-level performance.
 * 
 * Features:
 * - Memory-mapped file I/O for zero-copy serving
 * - Intelligent caching with LRU eviction
 * - Automatic MIME type detection
 * - HTTP range support for large files
 * - Gzip compression on-the-fly
 * - ETags and Last-Modified headers
 * 
 * Performance targets:
 * - 500-2000x faster than Python static serving
 * - 10,000+ files/second throughput
 * - Sub-millisecond response times
 * - Minimal memory overhead
 */
class StaticFileHandler {
public:
    // File cache entry with memory mapping
    struct CacheEntry {
        std::string file_path;
        std::string content_type;
        std::string etag;
        std::chrono::system_clock::time_point last_modified;
        size_t file_size;
        
        // Memory mapped data
        void* mapped_data = nullptr;
        size_t mapped_size = 0;
        
        // Platform-specific handles
#ifdef _WIN32
        HANDLE file_handle = INVALID_HANDLE_VALUE;
        HANDLE mapping_handle = INVALID_HANDLE_VALUE;
#else
        int file_descriptor = -1;
#endif
        
        // Cache metadata
        std::chrono::steady_clock::time_point last_accessed;
        std::atomic<uint64_t> access_count{0};
        bool is_compressed = false;
        std::string compressed_data; // For small files that benefit from compression
        
        ~CacheEntry();
        bool map_file();
        void unmap_file();
        bool is_valid() const;
    };
    
    // Configuration for static file serving
    struct Config {
        std::string root_directory = "./static";
        size_t max_cache_size_mb = 512;
        size_t max_file_size_mb = 100;
        std::chrono::minutes cache_ttl{60};
        bool enable_compression = true;
        bool enable_range_requests = true;
        bool enable_etags = true;
        size_t compression_threshold = 1024; // Compress files larger than this
        std::vector<std::string> compression_types = {
            "text/html", "text/css", "text/javascript", 
            "application/javascript", "application/json", "text/xml"
        };
        
        // Security settings
        bool allow_directory_traversal = false;
        std::vector<std::string> forbidden_extensions = {".tmp", ".bak", ".log"};
        std::vector<std::string> hidden_prefixes = {".", "_"};
    };
    
    explicit StaticFileHandler(const Config& config = Config{});
    ~StaticFileHandler();
    
    // Main serving functions
    struct ServeResult {
        enum Status {
            SUCCESS,
            NOT_FOUND,
            FORBIDDEN,
            NOT_MODIFIED,
            RANGE_NOT_SATISFIABLE,
            INTERNAL_ERROR
        } status = SUCCESS;
        
        std::string content_type;
        std::string etag;
        std::chrono::system_clock::time_point last_modified;
        size_t content_length = 0;
        
        // Response data
        const void* data = nullptr;
        bool owns_data = false;
        std::string string_data; // For compressed or generated content
        
        // Range support
        bool is_partial_content = false;
        size_t range_start = 0;
        size_t range_end = 0;
        size_t total_size = 0;
        
        ~ServeResult();
    };
    
    ServeResult serve_file(const std::string& request_path,
                          const std::unordered_map<std::string, std::string>& headers = {});
    
    // Cache management
    void clear_cache();
    void invalidate_file(const std::string& file_path);
    void set_cache_size_limit(size_t max_size_mb);
    
    // Directory operations
    bool add_route(const std::string& url_path, const std::string& local_path);
    void remove_route(const std::string& url_path);
    std::vector<std::string> list_routes() const;
    
    // Performance monitoring
    struct Stats {
        std::atomic<uint64_t> files_served{0};
        std::atomic<uint64_t> bytes_served{0};
        std::atomic<uint64_t> cache_hits{0};
        std::atomic<uint64_t> cache_misses{0};
        std::atomic<uint64_t> files_compressed{0};
        std::atomic<uint64_t> range_requests{0};
        std::atomic<uint64_t> not_modified_responses{0};
        std::atomic<uint64_t> total_serve_time_us{0};
        
        double cache_hit_ratio() const {
            auto total = cache_hits.load() + cache_misses.load();
            return total > 0 ? static_cast<double>(cache_hits.load()) / total : 0.0;
        }
        
        double average_serve_time_us() const {
            auto served = files_served.load();
            return served > 0 ? static_cast<double>(total_serve_time_us.load()) / served : 0.0;
        }
        
        double throughput_mb_per_sec() const {
            auto total_time_s = total_serve_time_us.load() / 1e6;
            auto total_mb = bytes_served.load() / (1024.0 * 1024.0);
            return total_time_s > 0 ? total_mb / total_time_s : 0.0;
        }
    };
    
    const Stats& get_stats() const { return stats_; }

private:
    Config config_;
    Stats stats_;
    
    // Route mapping: URL path -> local directory
    std::unordered_map<std::string, std::string> routes_;
    
    // File cache with LRU eviction
    std::unordered_map<std::string, std::shared_ptr<CacheEntry>> cache_;
#if PYSPEED_HAS_SHARED_MUTEX
    mutable std::shared_mutex cache_mutex_;
#else
    mutable std::mutex cache_mutex_;
#endif
    size_t current_cache_size_ = 0;
    
    // MIME type mapping
    std::unordered_map<std::string, std::string> mime_types_;
    
    // Internal functions
    std::string resolve_file_path(const std::string& request_path);
    std::string get_mime_type(const std::string& file_path);
    std::string generate_etag(const std::string& file_path, std::chrono::system_clock::time_point last_modified);
    bool is_file_forbidden(const std::string& file_path);
    bool should_compress(const std::string& content_type, size_t file_size);
    
    // Cache management
    std::shared_ptr<CacheEntry> get_cached_file(const std::string& file_path);
    void cache_file(const std::string& file_path, std::shared_ptr<CacheEntry> entry);
    void evict_lru_entries(size_t bytes_needed);
    void initialize_mime_types();
    
    // Range request handling
    struct Range {
        size_t start;
        size_t end;
        bool is_valid = false;
    };
    
    Range parse_range_header(const std::string& range_header, size_t file_size);
    
    // Compression
    std::string compress_content(const void* data, size_t size);
    bool accepts_gzip(const std::unordered_map<std::string, std::string>& headers);
};

/**
 * Memory-mapped file wrapper for efficient I/O operations.
 */
class MemoryMappedFile {
public:
    explicit MemoryMappedFile(const std::string& file_path);
    ~MemoryMappedFile();
    
    bool is_valid() const { return mapped_data_ != nullptr; }
    const void* data() const { return mapped_data_; }
    size_t size() const { return file_size_; }
    
    // Async prefetch for better performance
    void prefetch_async();
    
private:
    std::string file_path_;
    void* mapped_data_ = nullptr;
    size_t file_size_ = 0;
    
#ifdef _WIN32
    HANDLE file_handle_ = INVALID_HANDLE_VALUE;
    HANDLE mapping_handle_ = INVALID_HANDLE_VALUE;
#else
    int file_descriptor_ = -1;
#endif
    
    bool map_file();
    void unmap_file();
};

/**
 * High-performance directory watcher for cache invalidation.
 */
class DirectoryWatcher {
public:
    using ChangeCallback = std::function<void(const std::string& file_path, bool deleted)>;
    
    explicit DirectoryWatcher(const std::string& directory);
    ~DirectoryWatcher();
    
    void set_change_callback(ChangeCallback callback);
    void start_watching();
    void stop_watching();
    bool is_watching() const { return watching_; }

private:
    std::string directory_;
    ChangeCallback callback_;
    std::atomic<bool> watching_{false};
#if PYSPEED_HAS_CPP11
    std::thread watcher_thread_;
#endif
#ifdef _WIN32
    HANDLE directory_handle_ = INVALID_HANDLE_VALUE;
#else
    int inotify_fd_ = -1;
    int watch_descriptor_ = -1;
#endif
    
    void watch_thread();
    void process_changes();
};

/**
 * Utility functions for static file serving.
 */
namespace static_utils {
    // MIME type detection
    std::string detect_mime_type(const std::string& file_path);
    std::string detect_mime_type_by_content(const void* data, size_t size);
    
    // File system utilities
    bool is_safe_path(const std::string& path);
    std::string normalize_path(const std::string& path);
    std::string get_file_extension(const std::string& file_path);
    
    // HTTP utilities
    std::string format_http_date(std::chrono::system_clock::time_point time_point);
    std::chrono::system_clock::time_point parse_http_date(const std::string& date_str);
    
    // Performance utilities
    void warmup_file_cache(const std::string& directory, size_t max_files = 1000);
    
    // Compression utilities
    std::string compress_gzip(const void* data, size_t size);
    std::string compress_brotli(const void* data, size_t size);
    bool should_use_compression(const std::string& user_agent, const std::string& content_type);
}

} // namespace pyspeed