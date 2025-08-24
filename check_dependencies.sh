#!/bin/bash

# PySpeed Web Container - Dependency Check Script
# This script helps identify missing dependencies and provides installation instructions

echo "🔍 PySpeed Web Container - Dependency Check"
echo "=============================================="
echo ""

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Check functions
check_command() {
    if command -v "$1" &> /dev/null; then
        echo -e "${GREEN}✓${NC} $1 is available"
        return 0
    else
        echo -e "${RED}✗${NC} $1 is not available"
        return 1
    fi
}

check_pkg_config() {
    if pkg-config --exists "$1" 2>/dev/null; then
        local version=$(pkg-config --modversion "$1" 2>/dev/null)
        echo -e "${GREEN}✓${NC} $1 is available (version: $version)"
        return 0
    else
        echo -e "${RED}✗${NC} $1 is not available"
        return 1
    fi
}

check_python_package() {
    if python3 -c "import $1" 2>/dev/null; then
        local version=$(python3 -c "import $1; print(getattr($1, '__version__', 'unknown'))" 2>/dev/null)
        echo -e "${GREEN}✓${NC} Python $1 is available (version: $version)"
        return 0
    else
        echo -e "${RED}✗${NC} Python $1 is not available"
        return 1
    fi
}

# Track missing dependencies
missing_deps=()

echo "📋 Checking Build Tools:"
echo "------------------------"
if ! check_command "g++"; then missing_deps+=("g++"); fi
if ! check_command "cmake"; then missing_deps+=("cmake"); fi
if ! check_command "make"; then missing_deps+=("make"); fi
if ! check_command "pkg-config"; then missing_deps+=("pkg-config"); fi

echo ""
echo "📋 Checking C++ Libraries:"
echo "---------------------------"

# Check for Boost libraries
boost_missing=0
echo "Checking Boost libraries..."
if ! check_pkg_config "boost"; then
    boost_missing=1
    # Try alternative check
    if [ -d "/usr/include/boost" ] || [ -d "/usr/local/include/boost" ] || [ -d "/opt/homebrew/include/boost" ]; then
        echo -e "${YELLOW}⚠${NC} Boost headers found but pkg-config not configured"
    else
        echo -e "${RED}✗${NC} Boost libraries not found"
        missing_deps+=("boost")
    fi
fi

# Check for pybind11
echo ""
echo "📋 Checking Python Integration:"
echo "--------------------------------"
if ! check_command "python3"; then missing_deps+=("python3"); fi
if ! check_python_package "pybind11"; then missing_deps+=("pybind11"); fi

echo ""
echo "📋 Checking Optional Dependencies:"
echo "-----------------------------------"
check_pkg_config "zlib"
check_command "git"

# Detect OS and provide installation commands
echo ""
echo "💾 System Information:"
echo "----------------------"

if [[ "$OSTYPE" == "linux-gnu"* ]]; then
    echo "Operating System: Linux"
    distro=""
    if [ -f /etc/os-release ]; then
        distro=$(grep ^ID= /etc/os-release | cut -d= -f2 | tr -d '"')
    fi
    echo "Distribution: $distro"
elif [[ "$OSTYPE" == "darwin"* ]]; then
    echo "Operating System: macOS"
elif [[ "$OSTYPE" == "msys" || "$OSTYPE" == "cygwin" ]]; then
    echo "Operating System: Windows"
else
    echo "Operating System: Unknown ($OSTYPE)"
fi

# Provide installation instructions
if [ ${#missing_deps[@]} -gt 0 ]; then
    echo ""
    echo -e "${YELLOW}⚠ Missing Dependencies Found${NC}"
    echo "=================================="
    echo ""
    echo "Missing packages: ${missing_deps[*]}"
    echo ""
    echo "📥 Installation Instructions:"
    echo "------------------------------"
    
    if [[ "$OSTYPE" == "linux-gnu"* ]]; then
        echo -e "${BLUE}Ubuntu/Debian:${NC}"
        echo "sudo apt-get update"
        echo "sudo apt-get install build-essential cmake pkg-config python3 python3-dev"
        echo "sudo apt-get install libboost-all-dev zlib1g-dev"
        echo "pip3 install pybind11"
        echo ""
        echo -e "${BLUE}CentOS/RHEL/Fedora:${NC}"
        echo "sudo yum install gcc-c++ cmake pkgconfig python3 python3-devel"
        echo "sudo yum install boost-devel zlib-devel"
        echo "pip3 install pybind11"
        echo ""
    elif [[ "$OSTYPE" == "darwin"* ]]; then
        echo -e "${BLUE}macOS (Homebrew):${NC}"
        echo "brew install cmake boost python3"
        echo "pip3 install pybind11"
        echo ""
        echo -e "${BLUE}macOS (MacPorts):${NC}"
        echo "sudo port install cmake boost python38"
        echo "pip3 install pybind11"
        echo ""
    elif [[ "$OSTYPE" == "msys" || "$OSTYPE" == "cygwin" ]]; then
        echo -e "${BLUE}Windows (MSYS2):${NC}"
        echo "pacman -S mingw-w64-x86_64-cmake mingw-w64-x86_64-boost"
        echo "pacman -S mingw-w64-x86_64-python mingw-w64-x86_64-python-pip"
        echo "pip install pybind11"
        echo ""
    fi
    
    echo "🔧 Alternative: Build with reduced functionality"
    echo "If you cannot install all dependencies, PySpeed will:"
    echo "• Compile with reduced functionality when dependencies are missing"
    echo "• Provide clear error messages explaining what's needed"
    echo "• Fall back to Python-only mode where possible"
    echo ""
    
else
    echo ""
    echo -e "${GREEN}🎉 All Dependencies Available!${NC}"
    echo "================================"
    echo ""
    echo "You can now build PySpeed Web Container with full functionality:"
    echo ""
    echo "  cd /path/to/pyspeed-web-container"
    echo "  make build"
    echo ""
    echo "Or use the automated deployment:"
    echo ""
    echo "  ./deploy.sh deploy"
    echo ""
fi

echo "📚 Additional Information:"
echo "--------------------------"
echo "• Documentation: README.md"
echo "• C++ Error Fixes: CPP_ERROR_FIXES.md"
echo "• Docker Deployment: DOCKER_DEPLOYMENT.md"
echo "• Build Issues: Run this script again after installing dependencies"
echo ""

# Create simple test compilation
echo "🧪 Quick Compilation Test:"
echo "---------------------------"

cat > /tmp/pyspeed_test.cpp << 'EOF'
#include <iostream>
#include <string>
#include <memory>

// Test compatibility layer
#ifdef __has_include
  #if __has_include(<boost/version.hpp>)
    #include <boost/version.hpp>
    #define HAS_BOOST 1
  #else
    #define HAS_BOOST 0
  #endif
#else
  #define HAS_BOOST 0
#endif

int main() {
    std::cout << "C++ Compilation Test: ";
    
#if HAS_BOOST
    std::cout << "✓ Boost available (version " << BOOST_VERSION / 100000 << "." 
              << BOOST_VERSION / 100 % 1000 << "." << BOOST_VERSION % 100 << ")" << std::endl;
#else
    std::cout << "⚠ Boost not available - will use fallback mode" << std::endl;
#endif
    
    std::cout << "C++ Standard: " << __cplusplus << std::endl;
    return 0;
}
EOF

echo "Compiling test program..."
if g++ -std=c++11 /tmp/pyspeed_test.cpp -o /tmp/pyspeed_test 2>/dev/null; then
    echo -e "${GREEN}✓${NC} Basic C++ compilation works"
    /tmp/pyspeed_test
    rm -f /tmp/pyspeed_test
else
    echo -e "${RED}✗${NC} Basic C++ compilation failed"
fi

rm -f /tmp/pyspeed_test.cpp

echo ""
echo "🏁 Dependency check completed!"
echo "Run 'make build' or './deploy.sh deploy' to continue."