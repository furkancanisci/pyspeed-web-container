#!/bin/bash
# PySpeed Web Container - Production Deployment Script

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
PROJECT_NAME="pyspeed-web-container"
DOCKER_IMAGE="pyspeed/web-container"
VERSION=$(cat VERSION 2>/dev/null || echo "1.0.0")

echo -e "${BLUE}🚀 PySpeed Web Container Deployment Script${NC}"
echo -e "${BLUE}============================================${NC}"
echo ""

# Function to print status
print_status() {
    echo -e "${GREEN}✓${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}⚠${NC} $1"
}

print_error() {
    echo -e "${RED}✗${NC} $1"
}

# Check if Docker is installed and running
check_docker() {
    print_status "Checking Docker installation..."
    
    if ! command -v docker &> /dev/null; then
        print_error "Docker is not installed. Please install Docker first."
        exit 1
    fi
    
    if ! docker info &> /dev/null; then
        print_error "Docker daemon is not running. Please start Docker."
        exit 1
    fi
    
    print_status "Docker is ready"
}

# Check if Docker Compose is available
check_docker_compose() {
    print_status "Checking Docker Compose..."
    
    if command -v docker-compose &> /dev/null; then
        DOCKER_COMPOSE="docker-compose"
    elif docker compose version &> /dev/null; then
        DOCKER_COMPOSE="docker compose"
    else
        print_error "Docker Compose is not available. Please install Docker Compose."
        exit 1
    fi
    
    print_status "Docker Compose is ready: $DOCKER_COMPOSE"
}

# Create necessary directories and files
setup_environment() {
    print_status "Setting up deployment environment..."
    
    # Create directories
    mkdir -p config static logs docker/ssl docker/grafana/dashboards docker/grafana/datasources
    
    # Create config file if it doesn't exist
    if [ ! -f "config/pyspeed.json" ]; then
        cat > config/pyspeed.json << EOF
{
    "server": {
        "host": "0.0.0.0",
        "port": 8080,
        "workers": 4,
        "max_request_size": 20971520,
        "keep_alive_timeout": 60
    },
    "performance": {
        "enable_compression": true,
        "enable_static_cache": true,
        "static_cache_size": 536870912,
        "use_memory_pool": true,
        "enable_zero_copy": true,
        "io_buffer_size": 131072
    },
    "security": {
        "rate_limit_requests_per_second": 100,
        "max_concurrent_connections": 1000,
        "enable_cors": true
    },
    "logging": {
        "level": "INFO",
        "enable_access_log": true,
        "enable_performance_log": true
    }
}
EOF
        print_status "Created default configuration file"
    fi
    
    # Create VERSION file
    echo "$VERSION" > VERSION
    
    print_status "Environment setup completed"
}

# Build Docker images
build_images() {
    print_status "Building Docker images..."
    
    echo "Building PySpeed Web Container image..."
    docker build -t "${DOCKER_IMAGE}:${VERSION}" -t "${DOCKER_IMAGE}:latest" .
    
    if [ $? -eq 0 ]; then
        print_status "Docker image built successfully: ${DOCKER_IMAGE}:${VERSION}"
    else
        print_error "Failed to build Docker image"
        exit 1
    fi
}

# Deploy services
deploy_services() {
    print_status "Deploying services with Docker Compose..."
    
    # Pull external images
    $DOCKER_COMPOSE pull nginx-lb redis-cache postgres-db prometheus grafana
    
    # Start services
    $DOCKER_COMPOSE up -d
    
    if [ $? -eq 0 ]; then
        print_status "Services deployed successfully"
    else
        print_error "Failed to deploy services"
        exit 1
    fi
}

# Wait for services to be healthy
wait_for_services() {
    print_status "Waiting for services to be ready..."
    
    # Wait for Flask app
    echo -n "Waiting for Flask app (port 8080)..."
    for i in {1..30}; do
        if curl -s http://localhost:8080/health > /dev/null 2>&1; then
            echo -e " ${GREEN}✓${NC}"
            break
        fi
        echo -n "."
        sleep 2
    done
    
    # Wait for FastAPI app
    echo -n "Waiting for FastAPI app (port 8081)..."
    for i in {1..30}; do
        if curl -s http://localhost:8081/health > /dev/null 2>&1; then
            echo -e " ${GREEN}✓${NC}"
            break
        fi
        echo -n "."
        sleep 2
    done
    
    # Wait for nginx
    echo -n "Waiting for nginx load balancer (port 80)..."
    for i in {1..30}; do
        if curl -s http://localhost/health > /dev/null 2>&1; then
            echo -e " ${GREEN}✓${NC}"
            break
        fi
        echo -n "."
        sleep 2
    done
    
    print_status "All services are ready!"
}

# Show deployment status
show_status() {
    echo ""
    echo -e "${BLUE}🎉 Deployment completed successfully!${NC}"
    echo ""
    echo -e "${GREEN}Service Endpoints:${NC}"
    echo "  • Flask App (direct):     http://localhost:8080"
    echo "  • FastAPI App (direct):   http://localhost:8081"
    echo "  • Load Balancer (nginx):  http://localhost:80"
    echo "  • Grafana Dashboard:      http://localhost:3000 (admin/pyspeed_admin)"
    echo "  • Prometheus Metrics:     http://localhost:9090"
    echo "  • PostgreSQL Database:    localhost:5432 (pyspeed/pyspeed_secure_password)"
    echo "  • Redis Cache:            localhost:6379"
    echo ""
    echo -e "${GREEN}Performance Testing:${NC}"
    echo "  # Load test Flask app"
    echo "  wrk -t12 -c400 -d30s --latency http://localhost:8080/api/test"
    echo ""
    echo "  # Load test FastAPI app"
    echo "  wrk -t12 -c400 -d30s --latency http://localhost:8081/api/users"
    echo ""
    echo "  # Test through load balancer"
    echo "  wrk -t12 -c400 -d30s --latency http://localhost/api/test"
    echo ""
    echo -e "${GREEN}Management Commands:${NC}"
    echo "  # View logs"
    echo "  $DOCKER_COMPOSE logs -f pyspeed-flask"
    echo "  $DOCKER_COMPOSE logs -f pyspeed-fastapi"
    echo ""
    echo "  # Scale services"
    echo "  $DOCKER_COMPOSE up -d --scale pyspeed-flask=3 --scale pyspeed-fastapi=2"
    echo ""
    echo "  # Stop all services"
    echo "  $DOCKER_COMPOSE down"
    echo ""
    echo -e "${YELLOW}Next Steps:${NC}"
    echo "  1. Visit http://localhost:3000 to view Grafana dashboards"
    echo "  2. Check service health at http://localhost/health"
    echo "  3. Run performance tests to validate speedup"
    echo "  4. Customize config/pyspeed.json for your needs"
}

# Clean up function
cleanup() {
    if [ "$1" = "full" ]; then
        print_warning "Performing full cleanup (removing volumes)..."
        $DOCKER_COMPOSE down -v --remove-orphans
        docker system prune -f
        print_status "Full cleanup completed"
    else
        print_status "Stopping services..."
        $DOCKER_COMPOSE down --remove-orphans
        print_status "Cleanup completed"
    fi
}

# Main deployment flow
main() {
    case "$1" in
        "build")
            check_docker
            setup_environment
            build_images
            ;;
        "deploy")
            check_docker
            check_docker_compose
            setup_environment
            build_images
            deploy_services
            wait_for_services
            show_status
            ;;
        "start")
            check_docker
            check_docker_compose
            deploy_services
            wait_for_services
            show_status
            ;;
        "stop")
            check_docker_compose
            cleanup
            ;;
        "clean")
            check_docker_compose
            cleanup full
            ;;
        "status")
            check_docker_compose
            $DOCKER_COMPOSE ps
            ;;
        "logs")
            check_docker_compose
            $DOCKER_COMPOSE logs -f "${2:-}"
            ;;
        *)
            echo -e "${BLUE}PySpeed Web Container Deployment Script${NC}"
            echo ""
            echo "Usage: $0 {build|deploy|start|stop|clean|status|logs}"
            echo ""
            echo "Commands:"
            echo "  build   - Build Docker images only"
            echo "  deploy  - Full deployment (build + start services)"
            echo "  start   - Start existing services"
            echo "  stop    - Stop all services"
            echo "  clean   - Stop services and remove volumes"
            echo "  status  - Show service status"
            echo "  logs    - Show service logs (optional: service name)"
            echo ""
            echo "Examples:"
            echo "  $0 deploy                    # Full deployment"
            echo "  $0 logs pyspeed-flask       # View Flask app logs"
            echo "  $0 status                   # Check service status"
            exit 1
            ;;
    esac
}

# Run main function with all arguments
main "$@"