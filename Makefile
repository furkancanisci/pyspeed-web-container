# PySpeed Web Container Makefile
# Similar to cpythonwrapper but for web applications

.PHONY: help setup deps build clean test benchmark demo install dev-install check

# Variables
PYTHON := python3
PIP := pip3
VENV_DIR := venv
BUILD_DIR := build
SRC_DIR := src

# Default target
help:
	@echo "PySpeed Web Container - Build System"
	@echo "====================================="
	@echo ""
	@echo "Available commands:"
	@echo "  setup       - Create virtual environment and install dependencies"
	@echo "  deps        - Install system dependencies (Boost, CMake)"
	@echo "  build       - Build C++ extension modules"
	@echo "  rebuild     - Clean and rebuild everything"
	@echo "  clean       - Clean build artifacts"
	@echo "  clean-all   - Clean everything including virtual environment"
	@echo "  test        - Run quick functionality tests"
	@echo "  test-all    - Run comprehensive test suite"
	@echo "  benchmark   - Run performance benchmarks"
	@echo "  demo        - Run interactive demo with sample apps"
	@echo "  install     - Install package in development mode"
	@echo "  check       - Check if C++ extension is working"
	@echo ""
	@echo "Example usage:"
	@echo "  make setup && make build && make test"
	@echo ""

# Setup virtual environment and install dependencies
setup:
	@echo "Setting up PySpeed Web Container development environment..."
	$(PYTHON) -m venv $(VENV_DIR)
	$(VENV_DIR)/bin/pip install --upgrade pip setuptools wheel
	$(VENV_DIR)/bin/pip install -r requirements.txt
	@echo "✅ Setup complete! Activate with: source $(VENV_DIR)/bin/activate"

# Install system dependencies (macOS with Homebrew)
deps:
	@echo "Installing system dependencies..."
	@if command -v brew >/dev/null 2>&1; then \
		echo "Installing Boost and CMake via Homebrew..."; \
		brew install boost cmake; \
	elif command -v apt-get >/dev/null 2>&1; then \
		echo "Installing Boost and CMake via apt..."; \
		sudo apt-get update; \
		sudo apt-get install -y libboost-all-dev cmake build-essential; \
	elif command -v yum >/dev/null 2>&1; then \
		echo "Installing Boost and CMake via yum..."; \
		sudo yum install -y boost-devel cmake gcc-c++; \
	else \
		echo "❌ Please install Boost and CMake manually for your system"; \
		exit 1; \
	fi
	@echo "✅ System dependencies installed"

# Build C++ extension
build:
	@echo "Building C++ extensions..."
	@if [ ! -d "$(VENV_DIR)" ]; then \
		echo "❌ Virtual environment not found. Run 'make setup' first."; \
		exit 1; \
	fi
	mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && cmake .. && make -j$$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
	$(VENV_DIR)/bin/python setup.py build_ext --inplace
	@echo "✅ Build complete!"

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	rm -rf $(BUILD_DIR)
	rm -rf *.so
	rm -rf src/python/**/*.so
	rm -rf src/python/**/__pycache__
	rm -rf *.egg-info
	find . -name "*.pyc" -delete
	find . -name "__pycache__" -type d -exec rm -rf {} + 2>/dev/null || true
	@echo "✅ Cleaned build artifacts"

# Clean everything including virtual environment
clean-all: clean
	@echo "Cleaning everything..."
	rm -rf $(VENV_DIR)
	@echo "✅ Cleaned everything"

# Rebuild everything
rebuild: clean build

# Install in development mode
install: build
	@echo "Installing in development mode..."
	$(VENV_DIR)/bin/pip install -e .
	@echo "✅ Installed in development mode"

# Quick functionality test
test:
	@echo "Running quick functionality tests..."
	@if [ ! -f "pyspeed_accelerated*.so" ]; then \
		echo "❌ C++ extension not found. Run 'make build' first."; \
		exit 1; \
	fi
	$(VENV_DIR)/bin/python -c "import pyspeed_accelerated; print('✅ C++ extension loaded successfully')"
	@echo "✅ Quick tests passed"

# Comprehensive test suite
test-all: test
	@echo "Running comprehensive test suite..."
	$(VENV_DIR)/bin/python -m pytest tests/ -v
	@echo "✅ All tests passed"

# Performance benchmarks
benchmark:
	@echo "Running performance benchmarks..."
	@if [ ! -f "pyspeed_accelerated*.so" ]; then \
		echo "❌ C++ extension not found. Run 'make build' first."; \
		exit 1; \
	fi
	$(VENV_DIR)/bin/python src/python/pyspeed/benchmarks.py
	@echo "✅ Benchmarks complete"

# Interactive demo
demo:
	@echo "Starting interactive demo..."
	@if [ ! -f "pyspeed_accelerated*.so" ]; then \
		echo "❌ C++ extension not found. Run 'make build' first."; \
		exit 1; \
	fi
	$(VENV_DIR)/bin/python src/python/examples/demo.py
	@echo "✅ Demo complete"

# Check if extension is working
check:
	@echo "Checking C++ extension..."
	@if [ -f "pyspeed_accelerated*.so" ]; then \
		echo "✅ C++ extension found"; \
		$(VENV_DIR)/bin/python -c "import pyspeed_accelerated; print('✅ Extension loads successfully')"; \
	else \
		echo "❌ C++ extension not found. Run 'make build' first."; \
		exit 1; \
	fi

# Development helper - format code
format:
	@echo "Formatting code..."
	$(VENV_DIR)/bin/black src/python/
	$(VENV_DIR)/bin/isort src/python/
	@echo "✅ Code formatted"

# Development helper - lint code  
lint:
	@echo "Linting code..."
	$(VENV_DIR)/bin/flake8 src/python/
	@echo "✅ Code linted"

# Show system info
info:
	@echo "System Information:"
	@echo "==================="
	@echo "OS: $$(uname -s)"
	@echo "Architecture: $$(uname -m)"
	@echo "Python: $$($(PYTHON) --version)"
	@echo "CMake: $$(cmake --version | head -1)"
	@echo "Boost: $$(pkg-config --modversion boost 2>/dev/null || echo 'Not found via pkg-config')"
	@echo "Virtual Environment: $$(if [ -d '$(VENV_DIR)' ]; then echo 'Present'; else echo 'Missing'; fi)"
	@echo "C++ Extension: $$(if [ -f 'pyspeed_accelerated*.so' ]; then echo 'Built'; else echo 'Not built'; fi)"