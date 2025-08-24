#include "static_handler.hpp"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <thread>
#include <zlib.h>

namespace pyspeed {

// CacheEntry Implementation
StaticFileHandler::CacheEntry::~CacheEntry() {
    unmap_file();
}

bool StaticFileHandler::CacheEntry::map_file() {
    if (mapped_data != nullptr) {
        return true; // Already mapped
    }
    
#ifdef _WIN32
    // Windows memory mapping
    file_handle = CreateFileA(
        file_path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    
    if (file_handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    
    LARGE_INTEGER file_size_li;
    if (!GetFileSizeEx(file_handle, &file_size_li)) {
        CloseHandle(file_handle);
        file_handle = INVALID_HANDLE_VALUE;
        return false;
    }
    
    mapped_size = static_cast<size_t>(file_size_li.QuadPart);
    
    mapping_handle = CreateFileMapping(
        file_handle,
        nullptr,
        PAGE_READONLY,
        0, 0,
        nullptr
    );
    
    if (mapping_handle == nullptr) {
        CloseHandle(file_handle);
        file_handle = INVALID_HANDLE_VALUE;
        return false;
    }
    
    mapped_data = MapViewOfFile(
        mapping_handle,
        FILE_MAP_READ,
        0, 0, 0
    );
    
    if (mapped_data == nullptr) {
        CloseHandle(mapping_handle);
        CloseHandle(file_handle);
        mapping_handle = nullptr;
        file_handle = INVALID_HANDLE_VALUE;
        return false;
    }
    
#else
    // Unix/Linux memory mapping
    file_descriptor = open(file_path.c_str(), O_RDONLY);
    if (file_descriptor == -1) {
        return false;
    }
    
    struct stat file_stat;
    if (fstat(file_descriptor, &file_stat) == -1) {
        close(file_descriptor);
        file_descriptor = -1;
        return false;
    }
    
    mapped_size = static_cast<size_t>(file_stat.st_size);
    
    mapped_data = mmap(nullptr, mapped_size, PROT_READ, MAP_PRIVATE, file_descriptor, 0);
    if (mapped_data == MAP_FAILED) {
        close(file_descriptor);
        file_descriptor = -1;
        mapped_data = nullptr;
        return false;
    }
    
    // Advise the kernel about our access pattern
    madvise(mapped_data, mapped_size, MADV_SEQUENTIAL);
    
#endif
    
    return true;
}

void StaticFileHandler::CacheEntry::unmap_file() {
    if (mapped_data == nullptr) {
        return;
    }
    
#ifdef _WIN32
    UnmapViewOfFile(mapped_data);
    CloseHandle(mapping_handle);
    CloseHandle(file_handle);
    mapping_handle = nullptr;
    file_handle = INVALID_HANDLE_VALUE;
#else
    munmap(mapped_data, mapped_size);
    close(file_descriptor);
    file_descriptor = -1;
#endif
    
    mapped_data = nullptr;
    mapped_size = 0;
}

bool StaticFileHandler::CacheEntry::is_valid() const {
    return mapped_data != nullptr && pyspeed_compat::file_exists(file_path);
}

// StaticFileHandler Implementation
StaticFileHandler::StaticFileHandler(const Config& config) : config_(config) {
    initialize_mime_types();
    
    // Ensure root directory exists
    if (!pyspeed_compat::directory_exists(config_.root_directory)) {
        pyspeed_compat::create_directory(config_.root_directory);
    }
}

StaticFileHandler::~StaticFileHandler() {
    clear_cache();
}

StaticFileHandler::ServeResult StaticFileHandler::serve_file(
    const std::string& request_path,
    const std::unordered_map<std::string, std::string>& headers) {
    
    auto start_time = std::chrono::high_resolution_clock::now();
    ServeResult result;
    
    try {
        // Resolve the actual file path
        std::string file_path = resolve_file_path(request_path);
        if (file_path.empty()) {
            result.status = ServeResult::NOT_FOUND;
            return result;
        }
        
        // Security checks
        if (is_file_forbidden(file_path)) {
            result.status = ServeResult::FORBIDDEN;
            return result;
        }
        
        // Check if file exists
        if (!pyspeed_compat::file_exists(file_path)) {
            result.status = ServeResult::NOT_FOUND;
            return result;
        }
        
        // Get file info
        auto last_modified = pyspeed_compat::get_last_write_time(file_path);
        size_t file_size = pyspeed_compat::get_file_size(file_path);
        
        // Generate ETag
        std::string etag = generate_etag(file_path, last_modified);
        
        // Check If-None-Match header (ETag validation)
        auto if_none_match = headers.find("if-none-match");
        if (if_none_match != headers.end() && if_none_match->second == etag) {
            result.status = ServeResult::NOT_MODIFIED;
            result.etag = etag;
            result.last_modified = last_modified;
            stats_.not_modified_responses.fetch_add(1);
            return result;
        }
        
        // Check If-Modified-Since header
        auto if_modified_since = headers.find("if-modified-since");
        if (if_modified_since != headers.end()) {
            auto client_time = static_utils::parse_http_date(if_modified_since->second);
            if (last_modified <= client_time) {
                result.status = ServeResult::NOT_MODIFIED;
                result.etag = etag;
                result.last_modified = last_modified;
                stats_.not_modified_responses.fetch_add(1);
                return result;
            }
        }
        
        // Get cached file or load it
        auto cached_entry = get_cached_file(file_path);
        if (!cached_entry) {
            // Load file into cache
            cached_entry = std::make_shared<CacheEntry>();
            cached_entry->file_path = file_path;
            cached_entry->content_type = get_mime_type(file_path);
            cached_entry->etag = etag;
            cached_entry->last_modified = last_modified;
            cached_entry->file_size = file_size;
            
            if (file_size <= config_.max_file_size_mb * 1024 * 1024) {
                if (cached_entry->map_file()) {
                    cache_file(file_path, cached_entry);
                    stats_.cache_misses.fetch_add(1);
                } else {
                    result.status = ServeResult::INTERNAL_ERROR;
                    return result;
                }
            } else {
                // File too large for caching, serve directly
                stats_.cache_misses.fetch_add(1);
            }
        } else {
            stats_.cache_hits.fetch_add(1);
        }
        
        // Handle range requests
        auto range_header = headers.find("range");
        if (range_header != headers.end() && config_.enable_range_requests) {
            auto range = parse_range_header(range_header->second, file_size);
            if (range.is_valid) {
                result.is_partial_content = true;
                result.range_start = range.start;
                result.range_end = range.end;
                result.total_size = file_size;
                result.content_length = range.end - range.start + 1;
                result.data = static_cast<const char*>(cached_entry->mapped_data) + range.start;
                stats_.range_requests.fetch_add(1);
            } else {
                result.status = ServeResult::RANGE_NOT_SATISFIABLE;
                return result;
            }
        } else {
            result.content_length = file_size;
            result.data = cached_entry->mapped_data;
        }
        
        // Set response metadata
        result.content_type = cached_entry->content_type;
        result.etag = etag;
        result.last_modified = last_modified;
        
        // Handle compression
        bool should_compress_file = should_compress(result.content_type, result.content_length);
        bool client_accepts_gzip = accepts_gzip(headers);
        
        if (should_compress_file && client_accepts_gzip && !result.is_partial_content) {
            if (cached_entry->compressed_data.empty()) {
                // Compress and cache
                cached_entry->compressed_data = compress_content(result.data, result.content_length);
                cached_entry->is_compressed = true;
            }
            
            if (!cached_entry->compressed_data.empty()) {
                result.string_data = cached_entry->compressed_data;
                result.data = result.string_data.data();
                result.content_length = result.string_data.size();
                result.content_type += "; charset=utf-8";
                stats_.files_compressed.fetch_add(1);
            }
        }
        
        // Update access statistics
        cached_entry->last_accessed = std::chrono::steady_clock::now();
        cached_entry->access_count.fetch_add(1);
        
        result.status = ServeResult::SUCCESS;
        
    } catch (const std::exception& e) {
        result.status = ServeResult::INTERNAL_ERROR;
    }
    
    // Update performance statistics
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    
    stats_.files_served.fetch_add(1);
    stats_.bytes_served.fetch_add(result.content_length);
    stats_.total_serve_time_us.fetch_add(duration.count());
    
    return result;
}

std::string StaticFileHandler::resolve_file_path(const std::string& request_path) {
    // Find matching route
    std::string longest_match;
    std::string local_root;
    
    for (const auto& [route_path, route_root] : routes_) {
        if (request_path.substr(0, route_path.length()) == route_path) {
            if (route_path.length() > longest_match.length()) {
                longest_match = route_path;
                local_root = route_root;
            }
        }
    }
    
    if (longest_match.empty()) {
        // Use default root
        local_root = config_.root_directory;
        longest_match = "/";
    }
    
    // Build local file path
    std::string relative_path = request_path.substr(longest_match.length());
    if (relative_path.empty() || relative_path == "/") {
        relative_path = "/index.html"; // Default file
    }
    
    std::string file_path = local_root + relative_path;
    
    // Normalize and validate path
    file_path = static_utils::normalize_path(file_path);
    
    if (!static_utils::is_safe_path(file_path)) {
        return ""; // Path traversal attempt
    }
    
    return file_path;
}

std::string StaticFileHandler::get_mime_type(const std::string& file_path) {
    std::string extension = pyspeed_compat::get_extension(file_path);
    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
    
    auto it = mime_types_.find(extension);
    if (it != mime_types_.end()) {
        return it->second;
    }
    
    return "application/octet-stream"; // Default
}

std::string StaticFileHandler::generate_etag(const std::string& file_path, 
                                           std::chrono::system_clock::time_point last_modified) {
    // Simple ETag generation based on file path and modification time
    auto time_t = std::chrono::system_clock::to_time_t(last_modified);
    std::ostringstream oss;
    oss << std::hex << std::hash<std::string>{}(file_path) << "-" << time_t;
    return '\"' + oss.str() + '\"';
}

bool StaticFileHandler::is_file_forbidden(const std::string& file_path) {
    std::string filename = pyspeed_compat::get_filename(file_path);
    
    // Check forbidden extensions
    for (const auto& ext : config_.forbidden_extensions) {
        if (pyspeed_compat::ends_with(file_path, ext)) {
            return true;
        }
    }
    
    // Check hidden prefixes
    for (const auto& prefix : config_.hidden_prefixes) {
        if (pyspeed_compat::starts_with(filename, prefix)) {
            return true;
        }
    }
    
    return false;
}

bool StaticFileHandler::should_compress(const std::string& content_type, size_t file_size) {
    if (!config_.enable_compression || file_size < config_.compression_threshold) {
        return false;
    }
    
    for (const auto& compressible_type : config_.compression_types) {
        if (content_type.find(compressible_type) != std::string::npos) {
            return true;
        }
    }
    
    return false;
}

std::shared_ptr<StaticFileHandler::CacheEntry> StaticFileHandler::get_cached_file(const std::string& file_path) {
#if PYSPEED_HAS_SHARED_MUTEX
#if PYSPEED_HAS_SHARED_MUTEX
    std::shared_lock<std::shared_mutex> lock(cache_mutex_);
#else
    std::lock_guard<std::mutex> lock(cache_mutex_);
#endif
#else
    std::lock_guard<std::mutex> lock(cache_mutex_);
#endif
    
    auto it = cache_.find(file_path);
    if (it != cache_.end()) {
        auto entry = it->second;
        
        // Check if cache entry is still valid
        if (entry->is_valid()) {
            return entry;
        } else {
            // Entry is stale, remove it
            lock.~shared_lock();
#if PYSPEED_HAS_SHARED_MUTEX
#if PYSPEED_HAS_SHARED_MUTEX
            std::unique_lock<std::shared_mutex> write_lock(cache_mutex_);
#else
            std::lock_guard<std::mutex> write_lock(cache_mutex_);
#endif
#else
            std::lock_guard<std::mutex> write_lock(cache_mutex_);
#endif
            cache_.erase(file_path);
            current_cache_size_ -= entry->file_size;
        }
    }
    
    return nullptr;
}

void StaticFileHandler::cache_file(const std::string& file_path, std::shared_ptr<CacheEntry> entry) {
#if PYSPEED_HAS_SHARED_MUTEX
    std::unique_lock<std::shared_mutex> lock(cache_mutex_);
#else
    std::lock_guard<std::mutex> lock(cache_mutex_);
#endif
    
    // Check if we need to evict entries
    size_t max_cache_bytes = config_.max_cache_size_mb * 1024 * 1024;
    if (current_cache_size_ + entry->file_size > max_cache_bytes) {
        evict_lru_entries(entry->file_size);
    }
    
    cache_[file_path] = entry;
    current_cache_size_ += entry->file_size;
}

void StaticFileHandler::evict_lru_entries(size_t bytes_needed) {
    // Simple LRU eviction based on last_accessed time
    std::vector<std::pair<std::chrono::steady_clock::time_point, std::string>> entries;
    
    for (const auto& [path, entry] : cache_) {
        entries.emplace_back(entry->last_accessed, path);
    }
    
    std::sort(entries.begin(), entries.end());
    
    size_t freed_bytes = 0;
    for (const auto& [time, path] : entries) {
        auto entry = cache_[path];
        freed_bytes += entry->file_size;
        current_cache_size_ -= entry->file_size;
        cache_.erase(path);
        
        if (freed_bytes >= bytes_needed) {
            break;
        }
    }
}

void StaticFileHandler::initialize_mime_types() {
    mime_types_ = {
        // Text types
        {\".html\", \"text/html\"},
        {\".htm\", \"text/html\"},
        {\".css\", \"text/css\"},
        {\".js\", \"application/javascript\"},
        {\".json\", \"application/json\"},
        {\".xml\", \"text/xml\"},
        {\".txt\", \"text/plain\"},
        
        // Image types
        {\".png\", \"image/png\"},
        {\".jpg\", \"image/jpeg\"},
        {\".jpeg\", \"image/jpeg\"},
        {\".gif\", \"image/gif\"},
        {\".svg\", \"image/svg+xml\"},
        {\".ico\", \"image/x-icon\"},
        {\".webp\", \"image/webp\"},
        
        // Font types
        {\".woff\", \"font/woff\"},
        {\".woff2\", \"font/woff2\"},
        {\".ttf\", \"font/ttf\"},
        {\".eot\", \"application/vnd.ms-fontobject\"},
        
        // Video types
        {\".mp4\", \"video/mp4\"},
        {\".webm\", \"video/webm\"},
        {\".ogg\", \"video/ogg\"},
        
        // Audio types
        {\".mp3\", \"audio/mpeg\"},
        {\".wav\", \"audio/wav\"},
        {\".flac\", \"audio/flac\"},
        
        // Archive types
        {\".zip\", \"application/zip\"},
        {\".gz\", \"application/gzip\"},
        {\".tar\", \"application/x-tar\"},
        
        // Document types
        {\".pdf\", \"application/pdf\"},
        {\".doc\", \"application/msword\"},
        {\".docx\", \"application/vnd.openxmlformats-officedocument.wordprocessingml.document\"}
    };
}

StaticFileHandler::Range StaticFileHandler::parse_range_header(const std::string& range_header, size_t file_size) {
    Range range;
    
    if (!pyspeed_compat::starts_with(range_header, "bytes=")) {
        return range;
    }
    
    std::string range_spec = range_header.substr(6); // Skip "bytes="
    auto dash_pos = range_spec.find('-');
    if (dash_pos == std::string::npos) {
        return range;
    }
    
    try {
        std::string start_str = range_spec.substr(0, dash_pos);
        std::string end_str = range_spec.substr(dash_pos + 1);
        
        if (start_str.empty() && end_str.empty()) {
            return range; // Invalid
        }
        
        if (start_str.empty()) {
            // Suffix range: -500 (last 500 bytes)
            size_t suffix_length = std::stoull(end_str);
            range.start = file_size > suffix_length ? file_size - suffix_length : 0;
            range.end = file_size - 1;
        } else if (end_str.empty()) {
            // Prefix range: 500- (from byte 500 to end)
            range.start = std::stoull(start_str);
            range.end = file_size - 1;
        } else {
            // Full range: 500-999
            range.start = std::stoull(start_str);
            range.end = std::stoull(end_str);
        }
        
        // Validate range
        if (range.start < file_size && range.end < file_size && range.start <= range.end) {
            range.is_valid = true;
        }
        
    } catch (...) {
        // Invalid range format
    }
    
    return range;
}

std::string StaticFileHandler::compress_content(const void* data, size_t size) {
    return static_utils::compress_gzip(data, size);
}

bool StaticFileHandler::accepts_gzip(const std::unordered_map<std::string, std::string>& headers) {
    auto accept_encoding = headers.find(\"accept-encoding\");
    if (accept_encoding != headers.end()) {
        return accept_encoding->second.find(\"gzip\") != std::string::npos;
    }
    return false;
}

bool StaticFileHandler::add_route(const std::string& url_path, const std::string& local_path) {
    routes_[url_path] = local_path;
    return true;
}

void StaticFileHandler::remove_route(const std::string& url_path) {
    routes_.erase(url_path);
}

std::vector<std::string> StaticFileHandler::list_routes() const {
    std::vector<std::string> routes;
    for (const auto& [url_path, local_path] : routes_) {
        routes.push_back(url_path + \" -> \" + local_path);
    }
    return routes;
}

void StaticFileHandler::clear_cache() {
#if PYSPEED_HAS_SHARED_MUTEX
    std::unique_lock<std::shared_mutex> lock(cache_mutex_);
#else
    std::lock_guard<std::mutex> lock(cache_mutex_);
#endif
    cache_.clear();
    current_cache_size_ = 0;
}

// ServeResult destructor
StaticFileHandler::ServeResult::~ServeResult() {
    // No cleanup needed since we don't own the memory-mapped data
}

// Utility functions implementation
namespace static_utils {

std::string get_file_extension(const std::string& file_path) {
    auto pos = file_path.find_last_of('.');
    if (pos != std::string::npos) {
        return file_path.substr(pos);
    }
    return \"\";
}

std::string normalize_path(const std::string& path) {
    // Simple path normalization - remove .. and . components
    std::filesystem::path fs_path(path);
    return fs_path.lexically_normal().string();
}

bool is_safe_path(const std::string& path) {
    // Check for directory traversal attempts
    return path.find(\"..\") == std::string::npos && 
           path.find(\"//\") == std::string::npos;
}

std::string format_http_date(std::chrono::system_clock::time_point time_point) {
    auto time_t = std::chrono::system_clock::to_time_t(time_point);
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&time_t), \"%a, %d %b %Y %H:%M:%S GMT\");
    return oss.str();
}

std::chrono::system_clock::time_point parse_http_date(const std::string& date_str) {
    // Simplified HTTP date parsing
    std::tm tm = {};
    std::istringstream iss(date_str);
    iss >> std::get_time(&tm, \"%a, %d %b %Y %H:%M:%S\");
    
    auto time_t = std::mktime(&tm);
    return std::chrono::system_clock::from_time_t(time_t);
}

std::string compress_gzip(const void* data, size_t size) {
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    
    if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        return \"\"; // Compression failed
    }
    
    zs.next_in = static_cast<Bytef*>(const_cast<void*>(data));
    zs.avail_in = static_cast<uInt>(size);
    
    std::string compressed;
    compressed.resize(size + (size / 10) + 16); // Estimate compressed size
    
    zs.next_out = reinterpret_cast<Bytef*>(compressed.data());
    zs.avail_out = static_cast<uInt>(compressed.size());
    
    int ret = deflate(&zs, Z_FINISH);
    deflateEnd(&zs);
    
    if (ret == Z_STREAM_END) {
        compressed.resize(zs.total_out);
        return compressed;
    }
    
    return \"\"; // Compression failed
}

} // namespace static_utils

} // namespace pyspeed