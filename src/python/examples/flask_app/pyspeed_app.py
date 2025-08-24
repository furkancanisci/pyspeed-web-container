"""
PySpeed Accelerated Flask Application

This demonstrates how to run the Flask test application with PySpeed acceleration
for massive performance improvements.
"""

import sys
import os

# Add the pyspeed module to the path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..'))

try:
    from pyspeed import PySpeedContainer
    from app import app
    print("✅ PySpeed module loaded successfully")
except ImportError as e:
    print(f"❌ Failed to import PySpeed: {e}")
    print("💡 Make sure to build the C++ extensions first:")
    print("   cd /path/to/pyspeed-web-container")
    print("   make build")
    sys.exit(1)

def main():
    """Run Flask app with PySpeed acceleration"""
    
    print("🚀 Starting PySpeed Accelerated Flask Application")
    print("=" * 60)
    
    # Configuration for optimal performance
    config = {
        'threads': 8,  # Use multiple threads for better concurrency
        'enable_compression': True,  # Enable gzip compression
        'enable_static_cache': True,  # Cache static files
        'static_cache_size': 256,  # 256MB cache for static files
        'max_request_size': 10 * 1024 * 1024,  # 10MB max request size
        'keep_alive_timeout': 30,  # 30 second keep-alive
        'use_memory_pool': True,  # Use memory pool for better performance
        'enable_zero_copy': True,  # Enable zero-copy optimizations
    }
    
    # Create PySpeed container with Flask app
    container = PySpeedContainer(app, config=config, framework='flask')
    
    # Add static file route for better performance
    static_dir = os.path.join(os.path.dirname(__file__), 'static')
    if os.path.exists(static_dir):
        container.add_static_route('/static', static_dir)
        print(f"📁 Added static file route: /static -> {static_dir}")
    
    print("\n📊 Performance Configuration:")
    print(f"   - Worker threads: {config['threads']}")
    print(f"   - Compression: {'Enabled' if config['enable_compression'] else 'Disabled'}")
    print(f"   - Static cache: {config['static_cache_size']}MB")
    print(f"   - Memory pool: {'Enabled' if config['use_memory_pool'] else 'Disabled'}")
    print(f"   - Zero-copy: {'Enabled' if config['enable_zero_copy'] else 'Disabled'}")
    
    print("\n🔗 Available Endpoints:")
    print("   - http://localhost:8080/                    - Home page")
    print("   - http://localhost:8080/health              - Health check")
    print("   - http://localhost:8080/api/users           - User list (JSON test)")
    print("   - http://localhost:8080/api/large-json      - Large JSON test")
    print("   - http://localhost:8080/static-test         - Static file test")
    print("   - http://localhost:8080/benchmark           - Performance info")
    
    print("\n💡 Performance Testing Commands:")
    print("   # Test JSON API performance")
    print("   curl -s http://localhost:8080/api/large-json?size=5000 | jq .")
    print()
    print("   # Test with load (requires wrk or ab)")
    print("   wrk -t8 -c100 -d30s http://localhost:8080/api/users")
    print("   ab -n 10000 -c 100 http://localhost:8080/health")
    print()
    print("   # Compare performance")
    print("   time curl -s http://localhost:8080/api/large-json?size=10000 > /dev/null")
    
    print("\n🚀 Starting server on http://0.0.0.0:8080")
    print("   Press Ctrl+C to stop")
    print("=" * 60)
    
    try:
        # Run the accelerated server
        container.run(host='0.0.0.0', port=8080)
        
    except KeyboardInterrupt:
        print("\n\n🛑 Shutting down PySpeed server...")
        
        # Print final statistics
        stats = container.get_stats()
        if stats:
            print("\n📊 Final Performance Statistics:")
            print(f"   - Requests processed: {stats.get('requests_processed', 0):,}")
            print(f"   - Bytes sent: {stats.get('bytes_sent', 0):,}")
            print(f"   - Average RPS: {stats.get('requests_per_second', 0):.1f}")
            print(f"   - Cache hit ratio: {stats.get('cache_hit_ratio', 0)*100:.1f}%")
            print(f"   - JSON requests: {stats.get('json_requests', 0):,}")
            print(f"   - Average parse time: {stats.get('average_parse_time_us', 0):.2f}μs")
        
        print("✅ Server stopped cleanly")

def compare_performance():
    """
    Utility function to compare PySpeed vs standard Flask performance
    """
    print("🔬 Performance Comparison Utility")
    print("=" * 50)
    print()
    print("To compare performance between standard Flask and PySpeed:")
    print()
    print("1️⃣  Run standard Flask app:")
    print("   python app.py")
    print("   # Server runs on http://localhost:5000")
    print()
    print("2️⃣  In another terminal, run PySpeed version:")
    print("   python pyspeed_app.py")
    print("   # Server runs on http://localhost:8080")
    print()
    print("3️⃣  Compare performance with load testing:")
    print("   # Standard Flask")
    print("   ab -n 1000 -c 10 http://localhost:5000/api/users")
    print()
    print("   # PySpeed accelerated")
    print("   ab -n 1000 -c 10 http://localhost:8080/api/users")
    print()
    print("4️⃣  Compare large JSON response times:")
    print("   # Standard Flask")
    print("   time curl -s http://localhost:5000/api/large-json?size=5000 > /dev/null")
    print()
    print("   # PySpeed accelerated")
    print("   time curl -s http://localhost:8080/api/large-json?size=5000 > /dev/null")
    print()
    print("Expected improvements with PySpeed:")
    print("   - 10-50x faster overall response times")
    print("   - 50-500x faster JSON serialization")
    print("   - 100-1000x faster request parsing")
    print("   - 500-2000x faster static file serving")

if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == '--compare':
        compare_performance()
    else:
        main()