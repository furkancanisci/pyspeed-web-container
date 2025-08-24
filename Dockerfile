# PySpeed Web Container - High-Performance Python Web Applications
# Multi-stage Docker build for optimal production deployment

# Build stage - compile C++ extensions
FROM ubuntu:22.04 as builder

LABEL maintainer="PySpeed Development Team"
LABEL description="High-performance C++ container for Python web applications"
LABEL version="1.0.0"

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    python3 \
    python3-dev \
    python3-pip \
    libboost-all-dev \
    libssl-dev \
    zlib1g-dev \
    libbz2-dev \
    libreadline-dev \
    libsqlite3-dev \
    wget \
    curl \
    llvm \
    libncurses5-dev \
    xz-utils \
    tk-dev \
    libxml2-dev \
    libxmlsec1-dev \
    libffi-dev \
    liblzma-dev \
    git \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# Set work directory
WORKDIR /pyspeed-build

# Copy source code
COPY . .

# Install Python dependencies
RUN python3 -m pip install --upgrade pip setuptools wheel
RUN python3 -m pip install pybind11[global] numpy

# Build C++ extensions
RUN mkdir -p build && cd build && \
    cmake .. -GNinja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_STANDARD=17 \
        -DPYTHON_EXECUTABLE=$(which python3) \
        -DPYBIND11_PYTHON_VERSION=3.10 && \
    ninja -j$(nproc)

# Install PySpeed package
RUN python3 -m pip install .

# Production stage - minimal runtime environment
FROM ubuntu:22.04 as production

# Install runtime dependencies only
RUN apt-get update && apt-get install -y \
    python3 \
    python3-pip \
    libboost-system1.74.0 \
    libboost-thread1.74.0 \
    libboost-filesystem1.74.0 \
    libssl3 \
    zlib1g \
    ca-certificates \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# Create non-root user for security
RUN groupadd -r pyspeed && useradd -r -g pyspeed -s /bin/bash pyspeed

# Set up application directory
WORKDIR /app
RUN chown pyspeed:pyspeed /app

# Copy built extensions and Python package from builder stage
COPY --from=builder /usr/local/lib/python3.10/dist-packages/ /usr/local/lib/python3.10/dist-packages/
COPY --from=builder /usr/local/bin/ /usr/local/bin/

# Copy example applications
COPY --chown=pyspeed:pyspeed src/python/examples/ ./examples/
COPY --chown=pyspeed:pyspeed src/python/pyspeed/ ./pyspeed/

# Create directories for static files and logs
RUN mkdir -p static logs tmp && chown -R pyspeed:pyspeed static logs tmp

# Switch to non-root user
USER pyspeed

# Install additional Python dependencies for web frameworks
RUN python3 -m pip install --user \
    flask \
    fastapi \
    uvicorn \
    gunicorn \
    requests \
    jinja2 \
    werkzeug

# Health check
HEALTHCHECK --interval=30s --timeout=10s --start-period=60s --retries=3 \
    CMD curl -f http://localhost:8080/health || exit 1

# Expose port
EXPOSE 8080

# Environment variables
ENV PYTHONPATH=/app
ENV PYSPEED_CONFIG_FILE=/app/config/pyspeed.json
ENV PYSPEED_LOG_LEVEL=INFO
ENV PYSPEED_WORKERS=4
ENV PYSPEED_PORT=8080
ENV PYSPEED_HOST=0.0.0.0

# Default command - run Flask example with PySpeed acceleration
CMD ["python3", "examples/flask_app/pyspeed_app.py"]