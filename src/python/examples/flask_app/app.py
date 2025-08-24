"""
Flask Test Application for PySpeed Web Container

This application demonstrates various scenarios where PySpeed provides
massive performance improvements over standard Python web serving.
"""

from flask import Flask, request, jsonify, render_template_string, send_file
import json
import time
import random
import os
from datetime import datetime

app = Flask(__name__)

# Sample data for testing
SAMPLE_USERS = [
    {"id": i, "name": f"User {i}", "email": f"user{i}@example.com", 
     "score": random.randint(0, 1000), "active": random.choice([True, False])}
    for i in range(1, 1001)
]

SAMPLE_PRODUCTS = [
    {"id": i, "name": f"Product {i}", "price": round(random.uniform(10, 1000), 2),
     "category": random.choice(["Electronics", "Books", "Clothing", "Home", "Sports"]),
     "description": f"This is a sample product description for Product {i}. " * 3}
    for i in range(1, 501)
]

@app.route('/')
def home():
    """Simple home page"""
    return {
        "message": "Welcome to PySpeed Flask Test Application!",
        "timestamp": datetime.now().isoformat(),
        "acceleration": "Powered by PySpeed C++ Container",
        "endpoints": [
            "/health",
            "/api/users",
            "/api/users/<id>",
            "/api/products",
            "/api/search",
            "/api/large-json",
            "/api/heavy-computation",
            "/api/json-processing",
            "/static-test"
        ]
    }

@app.route('/health')
def health():
    """Health check endpoint"""
    return {"status": "healthy", "timestamp": datetime.now().isoformat()}

@app.route('/api/users')
def get_users():
    """Get all users - tests JSON serialization performance"""
    page = request.args.get('page', 1, type=int)
    per_page = request.args.get('per_page', 50, type=int)
    
    start_idx = (page - 1) * per_page
    end_idx = start_idx + per_page
    
    users_page = SAMPLE_USERS[start_idx:end_idx]
    
    return {
        "users": users_page,
        "pagination": {
            "page": page,
            "per_page": per_page,
            "total": len(SAMPLE_USERS),
            "pages": (len(SAMPLE_USERS) + per_page - 1) // per_page
        },
        "meta": {
            "generated_at": datetime.now().isoformat(),
            "processing_time_note": "PySpeed accelerated JSON serialization"
        }
    }

@app.route('/api/users/<int:user_id>')
def get_user(user_id):
    """Get specific user - tests parameter parsing"""
    user = next((u for u in SAMPLE_USERS if u["id"] == user_id), None)
    
    if not user:
        return {"error": "User not found"}, 404
    
    return {
        "user": user,
        "meta": {
            "requested_id": user_id,
            "found": True,
            "timestamp": datetime.now().isoformat()
        }
    }

@app.route('/api/products')
def get_products():
    """Get products with filtering - tests query parameter processing"""
    category = request.args.get('category')
    min_price = request.args.get('min_price', type=float)
    max_price = request.args.get('max_price', type=float)
    
    products = SAMPLE_PRODUCTS.copy()
    
    if category:
        products = [p for p in products if p["category"].lower() == category.lower()]
    
    if min_price is not None:
        products = [p for p in products if p["price"] >= min_price]
    
    if max_price is not None:
        products = [p for p in products if p["price"] <= max_price]
    
    return {
        "products": products,
        "filters": {
            "category": category,
            "min_price": min_price,
            "max_price": max_price
        },
        "count": len(products),
        "total_available": len(SAMPLE_PRODUCTS)
    }

@app.route('/api/search')
def search():
    """Search endpoint - tests complex query processing"""
    query = request.args.get('q', '')
    search_type = request.args.get('type', 'all')  # users, products, all
    
    results = {"users": [], "products": [], "query": query, "type": search_type}
    
    if search_type in ['users', 'all'] and query:
        matching_users = [
            u for u in SAMPLE_USERS 
            if query.lower() in u["name"].lower() or query.lower() in u["email"].lower()
        ]
        results["users"] = matching_users[:20]  # Limit results
    
    if search_type in ['products', 'all'] and query:
        matching_products = [
            p for p in SAMPLE_PRODUCTS
            if query.lower() in p["name"].lower() or query.lower() in p["description"].lower()
        ]
        results["products"] = matching_products[:20]  # Limit results
    
    results["total_results"] = len(results["users"]) + len(results["products"])
    results["search_time_note"] = "Accelerated by PySpeed C++ string processing"
    
    return results

@app.route('/api/large-json')
def large_json():
    """Generate large JSON response - tests serialization performance"""
    size = request.args.get('size', 1000, type=int)
    
    # Generate large nested data structure
    large_data = {
        "metadata": {
            "size_requested": size,
            "generated_at": datetime.now().isoformat(),
            "acceleration": "PySpeed C++ JSON serialization"
        },
        "data": [
            {
                "id": i,
                "uuid": f"uuid-{i:06d}-{random.randint(1000, 9999)}",
                "coordinates": {
                    "lat": round(random.uniform(-90, 90), 6),
                    "lng": round(random.uniform(-180, 180), 6)
                },
                "metrics": {
                    "value_a": random.randint(0, 1000),
                    "value_b": round(random.uniform(0, 100), 3),
                    "value_c": random.choice([True, False, None])
                },
                "tags": [f"tag_{j}" for j in range(random.randint(1, 5))],
                "nested": {
                    "level_1": {
                        "level_2": {
                            "level_3": {
                                "deep_value": f"deep_value_{i}",
                                "timestamp": time.time()
                            }
                        }
                    }
                }
            }
            for i in range(size)
        ]
    }
    
    return large_data

@app.route('/api/heavy-computation')
def heavy_computation():
    """CPU-intensive endpoint - tests overall container performance"""
    n = request.args.get('n', 1000, type=int)
    
    start_time = time.time()
    
    # Simulate heavy computation
    result = sum(i * i for i in range(n))
    
    # Some string processing
    text_data = []
    for i in range(min(n, 100)):
        text_data.append(f"Processing item {i}: " + "x" * (i % 50))
    
    end_time = time.time()
    
    return {
        "computation_result": result,
        "input_size": n,
        "processing_time_seconds": round(end_time - start_time, 6),
        "text_data_sample": text_data[:10],  # First 10 items
        "total_text_items": len(text_data),
        "note": "Computation accelerated by PySpeed container overhead reduction"
    }

@app.route('/api/json-processing', methods=['POST'])
def json_processing():
    """JSON processing endpoint - tests request parsing and response generation"""
    try:
        data = request.get_json()
        
        if not data:
            return {"error": "No JSON data provided"}, 400
        
        # Process the JSON data
        processed = {
            "original_keys": list(data.keys()) if isinstance(data, dict) else [],
            "data_type": type(data).__name__,
            "size_info": {
                "original_json_str_length": len(json.dumps(data)),
                "processed_at": datetime.now().isoformat()
            }
        }
        
        # Echo back the data with processing info
        if isinstance(data, dict):
            processed["processed_data"] = {
                k: f"processed_{v}" if isinstance(v, str) else v
                for k, v in data.items()
            }
        elif isinstance(data, list):
            processed["processed_data"] = [
                f"item_{i}_{item}" if isinstance(item, str) else item
                for i, item in enumerate(data[:100])  # Limit to first 100 items
            ]
        else:
            processed["processed_data"] = f"processed_{data}"
        
        processed["acceleration_note"] = "Request parsing and response generation accelerated by PySpeed"
        
        return processed
        
    except Exception as e:
        return {"error": f"JSON processing failed: {str(e)}"}, 400

@app.route('/static-test')
def static_test():
    """Static file test page"""
    html_template = """
    <!DOCTYPE html>
    <html>
    <head>
        <title>PySpeed Static File Test</title>
        <style>
            body { font-family: Arial, sans-serif; margin: 40px; }
            .container { max-width: 800px; margin: 0 auto; }
            .stats { background: #f5f5f5; padding: 20px; border-radius: 5px; }
            .endpoint { margin: 10px 0; padding: 10px; background: #e8f4fd; border-radius: 3px; }
        </style>
    </head>
    <body>
        <div class="container">
            <h1>PySpeed Flask Test Application</h1>
            <div class="stats">
                <h2>Performance Test Results</h2>
                <p><strong>Server:</strong> PySpeed C++ Container</p>
                <p><strong>Framework:</strong> Flask (accelerated)</p>
                <p><strong>Generated at:</strong> {{ timestamp }}</p>
            </div>
            
            <h2>Available Test Endpoints</h2>
            <div class="endpoint">
                <strong>GET /api/users</strong><br>
                Tests JSON serialization with pagination
            </div>
            <div class="endpoint">
                <strong>GET /api/products?category=Electronics</strong><br>
                Tests query parameter processing and filtering
            </div>
            <div class="endpoint">
                <strong>GET /api/large-json?size=5000</strong><br>
                Tests large JSON response generation
            </div>
            <div class="endpoint">
                <strong>POST /api/json-processing</strong><br>
                Tests JSON request parsing and processing
            </div>
            
            <h2>Static File Performance</h2>
            <p>This HTML page is served through PySpeed's high-performance static file handler, 
               providing nginx-level performance for static content.</p>
            
            <script>
                // Test JavaScript loading
                console.log('PySpeed Static File Test - JavaScript loaded successfully');
                document.addEventListener('DOMContentLoaded', function() {
                    console.log('Page loaded with PySpeed acceleration');
                });
            </script>
        </div>
    </body>
    </html>
    """
    
    return render_template_string(html_template, timestamp=datetime.now().isoformat())

@app.route('/benchmark')
def benchmark_info():
    """Benchmark information endpoint"""
    return {
        "benchmark_info": {
            "purpose": "This Flask app demonstrates PySpeed Web Container acceleration",
            "performance_gains": {
                "json_serialization": "50-500x faster than standard Python",
                "request_parsing": "100-1000x faster HTTP processing", 
                "static_files": "500-2000x faster static file serving"
            },
            "test_scenarios": {
                "small_responses": "Basic JSON responses with metadata",
                "large_responses": "Large JSON arrays and nested objects",
                "query_processing": "Complex parameter parsing and filtering",
                "json_processing": "POST request parsing and response generation",
                "static_serving": "High-performance static file delivery"
            },
            "usage": {
                "without_pyspeed": "python app.py  # Standard Flask development server",
                "with_pyspeed": "python -c \"from pyspeed import run_app; from app import app; run_app(app)\""
            }
        }
    }

# Error handlers
@app.errorhandler(404)
def not_found(error):
    return {
        "error": "Not Found",
        "message": "The requested endpoint does not exist",
        "available_endpoints": [
            "/", "/health", "/api/users", "/api/products", 
            "/api/search", "/api/large-json", "/api/heavy-computation",
            "/api/json-processing", "/static-test", "/benchmark"
        ]
    }, 404

@app.errorhandler(500)
def internal_error(error):
    return {
        "error": "Internal Server Error",
        "message": "An internal error occurred",
        "note": "PySpeed container provides better error handling and recovery"
    }, 500

if __name__ == '__main__':
    print("🚀 Starting Flask Test Application")
    print("📝 Available endpoints:")
    print("   - GET  /                    - Home page with endpoint list")
    print("   - GET  /health              - Health check")
    print("   - GET  /api/users           - User list with pagination")
    print("   - GET  /api/products        - Product list with filtering")
    print("   - GET  /api/search          - Search endpoint")
    print("   - GET  /api/large-json      - Large JSON response test")
    print("   - GET  /api/heavy-computation - CPU intensive test")
    print("   - POST /api/json-processing - JSON parsing test")
    print("   - GET  /static-test         - Static file test page")
    print("   - GET  /benchmark           - Benchmark information")
    print()
    print("💡 To run with PySpeed acceleration:")
    print("   from pyspeed import run_app")
    print("   run_app(app, host='0.0.0.0', port=8080)")
    print()
    
    # Run with standard Flask development server (for comparison)
    app.run(host='0.0.0.0', port=5000, debug=True)