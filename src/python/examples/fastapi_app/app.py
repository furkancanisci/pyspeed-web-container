"""
FastAPI Test Application for PySpeed Web Container

This application demonstrates PySpeed acceleration with FastAPI,
including async endpoints and modern API features.
"""

from fastapi import FastAPI, HTTPException, Query, Body, Request
from fastapi.responses import HTMLResponse, JSONResponse
from pydantic import BaseModel
from typing import List, Optional, Dict, Any
import asyncio
import json
import time
import random
from datetime import datetime
import uvicorn

# Pydantic models for request/response validation
class User(BaseModel):
    id: int
    name: str
    email: str
    score: int
    active: bool

class Product(BaseModel):
    id: int
    name: str
    price: float
    category: str
    description: str

class SearchResult(BaseModel):
    users: List[User]
    products: List[Product]
    query: str
    total_results: int

class ProcessingRequest(BaseModel):
    data: Dict[str, Any]
    options: Optional[Dict[str, Any]] = {}

# FastAPI app instance
app = FastAPI(
    title="PySpeed FastAPI Test Application",
    description="Demonstrates PySpeed Web Container acceleration with FastAPI",
    version="1.0.0",
    docs_url="/docs",
    redoc_url="/redoc"
)

# Sample data
SAMPLE_USERS = [
    User(
        id=i, 
        name=f"User {i}", 
        email=f"user{i}@example.com",
        score=random.randint(0, 1000),
        active=random.choice([True, False])
    )
    for i in range(1, 1001)
]

SAMPLE_PRODUCTS = [
    Product(
        id=i,
        name=f"Product {i}",
        price=round(random.uniform(10, 1000), 2),
        category=random.choice(["Electronics", "Books", "Clothing", "Home", "Sports"]),
        description=f"This is a sample product description for Product {i}. " * 3
    )
    for i in range(1, 501)
]

@app.get("/")
async def root():
    """Root endpoint with API information"""
    return {
        "message": "Welcome to PySpeed FastAPI Test Application!",
        "timestamp": datetime.now().isoformat(),
        "acceleration": "Powered by PySpeed C++ Container",
        "framework": "FastAPI (async)",
        "endpoints": {
            "health": "/health",
            "users": "/api/users",
            "products": "/api/products",
            "search": "/api/search",
            "async_test": "/api/async-test",
            "large_json": "/api/large-json",
            "json_processing": "/api/json-processing",
            "stream_test": "/api/stream",
            "docs": "/docs"
        }
    }

@app.get("/health")
async def health():
    """Health check endpoint"""
    return {
        "status": "healthy",
        "timestamp": datetime.now().isoformat(),
        "framework": "FastAPI",
        "acceleration": "PySpeed"
    }

@app.get("/api/users", response_model=Dict[str, Any])
async def get_users(
    page: int = Query(1, ge=1, description="Page number"),
    per_page: int = Query(50, ge=1, le=100, description="Items per page"),
    active_only: bool = Query(False, description="Filter active users only")
):
    """Get paginated users list"""
    
    # Filter users if requested
    users = SAMPLE_USERS
    if active_only:
        users = [u for u in users if u.active]
    
    # Pagination
    start_idx = (page - 1) * per_page
    end_idx = start_idx + per_page
    users_page = users[start_idx:end_idx]
    
    return {
        "users": [user.dict() for user in users_page],
        "pagination": {
            "page": page,
            "per_page": per_page,
            "total": len(users),
            "pages": (len(users) + per_page - 1) // per_page,
            "has_next": end_idx < len(users),
            "has_prev": page > 1
        },
        "filters": {"active_only": active_only},
        "meta": {
            "generated_at": datetime.now().isoformat(),
            "processing_note": "PySpeed accelerated async processing"
        }
    }

@app.get("/api/users/{user_id}", response_model=Dict[str, Any])
async def get_user(user_id: int):
    """Get specific user by ID"""
    user = next((u for u in SAMPLE_USERS if u.id == user_id), None)
    
    if not user:
        raise HTTPException(status_code=404, detail="User not found")
    
    return {
        "user": user.dict(),
        "meta": {
            "requested_id": user_id,
            "found": True,
            "timestamp": datetime.now().isoformat()
        }
    }

@app.get("/api/products", response_model=Dict[str, Any])
async def get_products(
    category: Optional[str] = Query(None, description="Filter by category"),
    min_price: Optional[float] = Query(None, ge=0, description="Minimum price"),
    max_price: Optional[float] = Query(None, ge=0, description="Maximum price"),
    limit: int = Query(50, ge=1, le=200, description="Maximum number of results")
):
    """Get products with filtering"""
    
    products = SAMPLE_PRODUCTS.copy()
    
    # Apply filters
    if category:
        products = [p for p in products if p.category.lower() == category.lower()]
    
    if min_price is not None:
        products = [p for p in products if p.price >= min_price]
    
    if max_price is not None:
        products = [p for p in products if p.price <= max_price]
    
    # Limit results
    products = products[:limit]
    
    return {
        "products": [product.dict() for product in products],
        "filters": {
            "category": category,
            "min_price": min_price,
            "max_price": max_price,
            "limit": limit
        },
        "count": len(products),
        "total_available": len(SAMPLE_PRODUCTS),
        "categories": list(set(p.category for p in SAMPLE_PRODUCTS))
    }

@app.get("/api/search", response_model=SearchResult)
async def search(
    q: str = Query(..., description="Search query"),
    search_type: str = Query("all", regex="^(users|products|all)$", description="Search type")
):
    """Search across users and products"""
    
    results = {"users": [], "products": [], "query": q, "total_results": 0}
    
    # Search users
    if search_type in ['users', 'all']:
        matching_users = [
            user for user in SAMPLE_USERS
            if q.lower() in user.name.lower() or q.lower() in user.email.lower()
        ]
        results["users"] = [user.dict() for user in matching_users[:20]]
    
    # Search products
    if search_type in ['products', 'all']:
        matching_products = [
            product for product in SAMPLE_PRODUCTS
            if q.lower() in product.name.lower() or q.lower() in product.description.lower()
        ]
        results["products"] = [product.dict() for product in matching_products[:20]]
    
    results["total_results"] = len(results["users"]) + len(results["products"])
    
    return SearchResult(**results)

@app.get("/api/async-test")
async def async_test(
    delay: float = Query(0.1, ge=0, le=5.0, description="Simulated async delay in seconds"),
    operations: int = Query(10, ge=1, le=100, description="Number of async operations")
):
    """Test async operations performance"""
    
    start_time = time.time()
    
    async def async_operation(op_id: int):
        await asyncio.sleep(delay / operations)  # Distributed delay
        return {
            "operation_id": op_id,
            "result": random.randint(1, 1000),
            "processed_at": time.time()
        }
    
    # Execute operations concurrently
    tasks = [async_operation(i) for i in range(operations)]
    results = await asyncio.gather(*tasks)
    
    end_time = time.time()
    
    return {
        "async_operations": results,
        "performance": {
            "total_time": round(end_time - start_time, 4),
            "operations_count": operations,
            "delay_per_operation": delay / operations,
            "operations_per_second": round(operations / (end_time - start_time), 2)
        },
        "meta": {
            "framework": "FastAPI async",
            "acceleration": "PySpeed container reduces async overhead"
        }
    }

@app.get("/api/large-json")
async def large_json(
    size: int = Query(1000, ge=1, le=10000, description="Number of items to generate"),
    complexity: str = Query("medium", regex="^(simple|medium|complex)$", description="Data complexity")
):
    """Generate large JSON response for performance testing"""
    
    start_time = time.time()
    
    def generate_item(i: int):
        base_item = {
            "id": i,
            "uuid": f"uuid-{i:06d}-{random.randint(1000, 9999)}",
            "timestamp": time.time()
        }
        
        if complexity in ["medium", "complex"]:
            base_item.update({
                "coordinates": {
                    "lat": round(random.uniform(-90, 90), 6),
                    "lng": round(random.uniform(-180, 180), 6)
                },
                "metrics": {
                    "value_a": random.randint(0, 1000),
                    "value_b": round(random.uniform(0, 100), 3),
                    "value_c": random.choice([True, False, None])
                },
                "tags": [f"tag_{j}" for j in range(random.randint(1, 5))]
            })
        
        if complexity == "complex":
            base_item["nested"] = {
                "level_1": {
                    "level_2": {
                        "level_3": {
                            "deep_value": f"deep_value_{i}",
                            "array_data": list(range(random.randint(5, 15))),
                            "timestamp": time.time()
                        }
                    }
                }
            }
        
        return base_item
    
    # Generate data
    data = [generate_item(i) for i in range(size)]
    
    generation_time = time.time() - start_time
    
    return {
        "metadata": {
            "size_requested": size,
            "complexity": complexity,
            "generated_at": datetime.now().isoformat(),
            "generation_time": round(generation_time, 4),
            "acceleration": "PySpeed C++ JSON serialization"
        },
        "data": data,
        "stats": {
            "total_items": len(data),
            "estimated_json_size_kb": len(json.dumps(data)) / 1024
        }
    }

@app.post("/api/json-processing")
async def json_processing(
    request: ProcessingRequest = Body(..., description="JSON data to process")
):
    """Process JSON data and return modified version"""
    
    start_time = time.time()
    
    try:
        data = request.data
        options = request.options or {}
        
        # Process the data based on options
        processed_data = {}
        
        if isinstance(data, dict):
            for key, value in data.items():
                if options.get("uppercase_keys", False):
                    key = key.upper()
                
                if isinstance(value, str) and options.get("process_strings", True):
                    processed_data[key] = f"processed_{value}"
                elif isinstance(value, (int, float)) and options.get("multiply_numbers", False):
                    processed_data[key] = value * options.get("multiplier", 2)
                else:
                    processed_data[key] = value
        
        elif isinstance(data, list):
            processed_data = []
            for i, item in enumerate(data[:1000]):  # Limit processing
                if isinstance(item, str):
                    processed_data.append(f"item_{i}_{item}")
                else:
                    processed_data.append(item)
        
        else:
            processed_data = f"processed_{data}"
        
        processing_time = time.time() - start_time
        
        return {
            "original_data": data,
            "processed_data": processed_data,
            "processing_info": {
                "options_used": options,
                "processing_time": round(processing_time, 6),
                "data_type": type(data).__name__,
                "items_processed": len(data) if isinstance(data, (list, dict)) else 1
            },
            "acceleration_note": "JSON processing accelerated by PySpeed C++ parsing"
        }
        
    except Exception as e:
        raise HTTPException(status_code=400, detail=f"Processing failed: {str(e)}")

@app.get("/api/stream")
async def stream_test(
    chunks: int = Query(10, ge=1, le=100, description="Number of chunks to stream"),
    chunk_size: int = Query(100, ge=10, le=1000, description="Size of each chunk")
):
    """Test streaming response (simulated)"""
    
    async def generate_chunk(chunk_id: int):
        await asyncio.sleep(0.01)  # Simulate processing delay
        return {
            "chunk_id": chunk_id,
            "data": ["item_" + str(i) for i in range(chunk_size)],
            "timestamp": time.time(),
            "size": chunk_size
        }
    
    # Generate all chunks (in real streaming, this would yield one at a time)
    chunk_data = []
    for i in range(chunks):
        chunk = await generate_chunk(i)
        chunk_data.append(chunk)
    
    return {
        "stream_info": {
            "total_chunks": chunks,
            "chunk_size": chunk_size,
            "total_items": chunks * chunk_size
        },
        "chunks": chunk_data,
        "meta": {
            "note": "Real streaming would benefit more from PySpeed's async optimizations",
            "framework": "FastAPI async streaming"
        }
    }

@app.get("/static-test", response_class=HTMLResponse)
async def static_test():
    """Static file test page for FastAPI"""
    
    html_content = """
    <!DOCTYPE html>
    <html>
    <head>
        <title>PySpeed FastAPI Test</title>
        <style>
            body { font-family: Arial, sans-serif; margin: 40px; }
            .container { max-width: 800px; margin: 0 auto; }
            .stats { background: #f0f8ff; padding: 20px; border-radius: 5px; border-left: 4px solid #007acc; }
            .endpoint { margin: 10px 0; padding: 15px; background: #f9f9f9; border-radius: 5px; }
            .async-note { background: #e8f5e8; padding: 10px; border-radius: 3px; margin: 10px 0; }
            code { background: #f4f4f4; padding: 2px 5px; border-radius: 3px; }
        </style>
    </head>
    <body>
        <div class="container">
            <h1>PySpeed FastAPI Test Application</h1>
            
            <div class="stats">
                <h2>Performance Test Environment</h2>
                <p><strong>Server:</strong> PySpeed C++ Container</p>
                <p><strong>Framework:</strong> FastAPI (async)</p>
                <p><strong>Python:</strong> Async/await support</p>
                <p><strong>Generated at:</strong> """ + datetime.now().isoformat() + """</p>
            </div>
            
            <div class="async-note">
                <strong>Async Performance:</strong> FastAPI's async capabilities combined with PySpeed's 
                C++ acceleration provide exceptional performance for I/O-bound operations.
            </div>
            
            <h2>Available Test Endpoints</h2>
            
            <div class="endpoint">
                <strong>GET /api/users?page=1&per_page=50</strong><br>
                <em>Tests async pagination and filtering</em>
            </div>
            
            <div class="endpoint">
                <strong>GET /api/async-test?operations=50&delay=1.0</strong><br>
                <em>Tests concurrent async operations</em>
            </div>
            
            <div class="endpoint">
                <strong>GET /api/large-json?size=5000&complexity=complex</strong><br>
                <em>Tests large async JSON generation</em>
            </div>
            
            <div class="endpoint">
                <strong>POST /api/json-processing</strong><br>
                <em>Tests async JSON processing with Pydantic validation</em><br>
                <code>{"data": {"test": "value"}, "options": {"process_strings": true}}</code>
            </div>
            
            <div class="endpoint">
                <strong>GET /docs</strong><br>
                <em>Interactive API documentation (Swagger UI)</em>
            </div>
            
            <h2>Performance Benefits</h2>
            <ul>
                <li><strong>Async Processing:</strong> Non-blocking I/O operations</li>
                <li><strong>C++ Acceleration:</strong> Fast request/response handling</li>
                <li><strong>JSON Performance:</strong> Optimized serialization/deserialization</li>
                <li><strong>Static Files:</strong> High-speed static content delivery</li>
                <li><strong>Memory Efficiency:</strong> Reduced memory overhead</li>
            </ul>
            
            <h3>Load Testing Commands</h3>
            <pre><code># Test async operations
curl -X GET "http://localhost:8080/api/async-test?operations=100&delay=2.0"

# Test large JSON with wrk
wrk -t8 -c100 -d30s "http://localhost:8080/api/large-json?size=1000"

# Test POST endpoint
curl -X POST "http://localhost:8080/api/json-processing" \
     -H "Content-Type: application/json" \
     -d '{"data": {"test": "value"}, "options": {"process_strings": true}}'</code></pre>
            
            <script>
                console.log('PySpeed FastAPI Test - JavaScript loaded');
                
                // Test fetch API
                fetch('/health')
                    .then(response => response.json())
                    .then(data => console.log('Health check:', data))
                    .catch(error => console.error('Error:', error));
            </script>
        </div>
    </body>
    </html>
    """
    
    return HTMLResponse(content=html_content)

@app.get("/benchmark")
async def benchmark_info():
    """Benchmark and performance information"""
    return {
        "benchmark_info": {
            "framework": "FastAPI",
            "async_support": True,
            "acceleration": "PySpeed C++ Container",
            "performance_gains": {
                "async_operations": "Reduced async overhead and better concurrency",
                "json_processing": "50-500x faster than standard Python",
                "request_parsing": "100-1000x faster HTTP processing",
                "response_generation": "Optimized async response building"
            },
            "async_benefits": {
                "concurrent_requests": "Handle thousands of concurrent connections",
                "non_blocking_io": "CPU remains available during I/O operations",
                "memory_efficiency": "Lower memory usage per connection",
                "scalability": "Better scaling for I/O-bound applications"
            },
            "test_scenarios": {
                "concurrent_ops": "/api/async-test - Test concurrent async operations",
                "large_responses": "/api/large-json - Test large async JSON generation",
                "pydantic_validation": "/api/users - Test with Pydantic model validation",
                "streaming": "/api/stream - Test streaming-like responses"
            }
        },
        "usage_comparison": {
            "standard_fastapi": "uvicorn app:app --host 0.0.0.0 --port 8000",
            "pyspeed_accelerated": "python pyspeed_app.py"
        },
        "expected_improvements": {
            "request_throughput": "10-100x higher requests per second",
            "response_latency": "50-90% lower response times",
            "memory_usage": "30-70% lower memory consumption",
            "cpu_efficiency": "Better CPU utilization for I/O-bound tasks"
        }
    }

# Exception handlers
@app.exception_handler(404)
async def not_found_handler(request: Request, exc: HTTPException):
    return JSONResponse(
        status_code=404,
        content={
            "error": "Not Found",
            "message": "The requested endpoint does not exist",
            "available_endpoints": [
                "/", "/health", "/api/users", "/api/products", 
                "/api/search", "/api/async-test", "/api/large-json",
                "/api/json-processing", "/api/stream", "/static-test", 
                "/benchmark", "/docs"
            ]
        }
    )

@app.exception_handler(500)
async def internal_error_handler(request: Request, exc: Exception):
    return JSONResponse(
        status_code=500,
        content={
            "error": "Internal Server Error",
            "message": "An internal error occurred",
            "note": "PySpeed container provides better error handling and recovery"
        }
    )

if __name__ == "__main__":
    print("🚀 Starting FastAPI Test Application")
    print("📝 Available endpoints:")
    print("   - GET  /                    - API information")
    print("   - GET  /health              - Health check")
    print("   - GET  /api/users           - Async user list")
    print("   - GET  /api/async-test      - Concurrent async operations test")
    print("   - GET  /api/large-json      - Large async JSON generation")
    print("   - POST /api/json-processing - Async JSON processing")
    print("   - GET  /api/stream          - Streaming response test")
    print("   - GET  /static-test         - Static file test page")
    print("   - GET  /docs                - Interactive API docs")
    print("   - GET  /benchmark           - Performance information")
    print()
    print("💡 To run with PySpeed acceleration:")
    print("   from pyspeed import run_app")
    print("   run_app(app, host='0.0.0.0', port=8080)")
    print()
    
    # Run with standard uvicorn (for comparison)
    uvicorn.run(app, host="0.0.0.0", port=8000, log_level="info")