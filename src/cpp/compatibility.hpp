#pragma once

// Compatibility layer for different C++ standards and missing dependencies

#include <string>
#include <memory>
#include <functional>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <atomic>
#include <stdexcept>

// System headers
#ifdef _WIN32
    #include <windows.h>
    #include <sys/stat.h>
    #include <direct.h>
    #define mkdir(path, mode) _mkdir(path)
#else
    #include <sys/stat.h>
    #include <unistd.h>
#endif

// Check for C++17 features
#if __cplusplus >= 201703L
    #define PYSPEED_HAS_CPP17 1
    #define PYSPEED_HAS_FILESYSTEM 1
    #define PYSPEED_HAS_VARIANT 1
    #define PYSPEED_HAS_STRING_VIEW 1
#else
    #define PYSPEED_HAS_CPP17 0
    #define PYSPEED_HAS_FILESYSTEM 0
    #define PYSPEED_HAS_VARIANT 0
    #define PYSPEED_HAS_STRING_VIEW 0
#endif

// Check for C++14 features
#if __cplusplus >= 201402L
    #define PYSPEED_HAS_CPP14 1
    #define PYSPEED_HAS_SHARED_MUTEX 1
#else
    #define PYSPEED_HAS_CPP14 0
    #define PYSPEED_HAS_SHARED_MUTEX 0
#endif

// Boost libraries availability check
#ifdef BOOST_VERSION
    #define PYSPEED_HAS_BOOST 1
#else
    #define PYSPEED_HAS_BOOST 0
#endif

// pybind11 availability check
#ifdef PYBIND11_VERSION_MAJOR
    #define PYSPEED_HAS_PYBIND11 1
#else
    #define PYSPEED_HAS_PYBIND11 0
#endif

// Fallback implementations for missing features
namespace pyspeed_compat {

#if !PYSPEED_HAS_VARIANT
    // Simple fallback variant that just throws runtime errors
    // This allows compilation but with reduced functionality
    template<typename... Types>
    class variant {
        void* ptr_ = nullptr;
        size_t type_id_ = 0;
        
    public:
        template<typename T>
        variant(const T& value) : type_id_(1) {
            // Just store a copy - simplified implementation
        }
        
        template<typename T>
        T& get() { 
            throw std::runtime_error("PySpeed C++ extensions not fully available. Please install Boost and pybind11.");
        }
        
        template<typename T>
        const T& get() const { 
            throw std::runtime_error("PySpeed C++ extensions not fully available. Please install Boost and pybind11.");
        }
        
        template<typename T>
        bool holds_alternative() const {
            return false; // Simplified - always false
        }
    };
    
    template<typename T, typename Variant>
    bool holds_alternative(const Variant& v) { 
        return v.template holds_alternative<T>(); 
    }
    
    template<typename T, typename Variant>
    T& get(Variant& v) { 
        return v.template get<T>(); 
    }
    
    template<typename T, typename Variant>
    const T& get(const Variant& v) { 
        return v.template get<T>(); 
    }
    
    template<typename Visitor, typename Variant>
    std::string visit(Visitor&& vis, const Variant& var) {
        // Return a helpful error message
        return "PySpeed C++ extensions not available";
    }
#endif

#if !PYSPEED_HAS_STRING_VIEW
    // Simplified string_view for C++11/14 compatibility
    class string_view {
        const char* data_;
        size_t size_;
        
    public:
        string_view(const char* data, size_t size) : data_(data), size_(size) {}
        string_view(const std::string& str) : data_(str.data()), size_(str.size()) {}
        
        const char* data() const { return data_; }
        size_t size() const { return size_; }
        std::string to_string() const { return std::string(data_, size_); }
    };
#endif

    // String utility functions
    inline bool starts_with(const std::string& str, const std::string& prefix) {
        return str.size() >= prefix.size() && 
               str.substr(0, prefix.size()) == prefix;
    }
    
    inline bool ends_with(const std::string& str, const std::string& suffix) {
        return str.size() >= suffix.size() && 
               str.substr(str.size() - suffix.size()) == suffix;
    }
    
    // File system utilities for non-C++17 builds
    inline bool file_exists(const std::string& path) {
        #ifdef _WIN32
            DWORD attrs = GetFileAttributesA(path.c_str());
            return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
        #else
            struct stat buffer;
            return (stat(path.c_str(), &buffer) == 0) && S_ISREG(buffer.st_mode);
        #endif
    }
    
    inline bool directory_exists(const std::string& path) {
        #ifdef _WIN32
            DWORD attrs = GetFileAttributesA(path.c_str());
            return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
        #else
            struct stat buffer;
            return (stat(path.c_str(), &buffer) == 0) && S_ISDIR(buffer.st_mode);
        #endif
    }
    
    inline bool create_directory(const std::string& path) {
        #ifdef _WIN32
            return CreateDirectoryA(path.c_str(), NULL) != 0;
        #else
            return mkdir(path.c_str(), 0755) == 0;
        #endif
    }
    
    inline size_t get_file_size(const std::string& path) {
        #ifdef _WIN32
            WIN32_FILE_ATTRIBUTE_DATA fileInfo;
            if (GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &fileInfo)) {
                LARGE_INTEGER size;
                size.HighPart = fileInfo.nFileSizeHigh;
                size.LowPart = fileInfo.nFileSizeLow;
                return static_cast<size_t>(size.QuadPart);
            }
            return 0;
        #else
            struct stat st;
            if (stat(path.c_str(), &st) == 0) {
                return static_cast<size_t>(st.st_size);
            }
            return 0;
        #endif
    }
    
    inline std::chrono::system_clock::time_point get_last_write_time(const std::string& path) {
        #ifdef _WIN32
            WIN32_FILE_ATTRIBUTE_DATA fileInfo;
            if (GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &fileInfo)) {
                ULARGE_INTEGER ull;
                ull.LowPart = fileInfo.ftLastWriteTime.dwLowDateTime;
                ull.HighPart = fileInfo.ftLastWriteTime.dwHighDateTime;
                auto duration = std::chrono::nanoseconds((ull.QuadPart - 116444736000000000ULL) * 100);
                return std::chrono::system_clock::time_point(
                    std::chrono::duration_cast<std::chrono::system_clock::duration>(duration));
            }
        #else
            struct stat st;
            if (stat(path.c_str(), &st) == 0) {
                return std::chrono::system_clock::from_time_t(st.st_mtime);
            }
        #endif
        return std::chrono::system_clock::now();
    }
    
    // Extract filename from path
    inline std::string get_filename(const std::string& path) {
        auto pos = path.find_last_of("/\\");
        return pos != std::string::npos ? path.substr(pos + 1) : path;
    }
    
    // Extract file extension
    inline std::string get_extension(const std::string& path) {
        auto filename = get_filename(path);
        auto pos = filename.find_last_of('.');
        return pos != std::string::npos ? filename.substr(pos) : "";
    }

} // namespace pyspeed_compat

// Make compatibility functions available globally if needed
#if !PYSPEED_HAS_CPP17
namespace std {
    #if !PYSPEED_HAS_VARIANT
    using pyspeed_compat::variant;
    using pyspeed_compat::holds_alternative;
    using pyspeed_compat::visit;
    #endif
    
    #if !PYSPEED_HAS_STRING_VIEW
    using pyspeed_compat::string_view;
    #endif
}
#endif