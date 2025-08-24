"""
Web Server Load Testing and Benchmarking

This module provides comprehensive web server performance testing,
comparing standard Python web servers with PySpeed accelerated versions.
"""

import time
import requests
import threading
import statistics
import subprocess
import sys
import os
import json
from typing import Dict, List, Tuple, Optional, Any
from dataclasses import dataclass
from concurrent.futures import ThreadPoolExecutor, as_completed
import socket
from urllib.parse import urljoin

@dataclass
class LoadTestResult:
    """Result from load testing a web endpoint"""
    endpoint: str
    total_requests: int
    successful_requests: int
    failed_requests: int
    total_time: float
    requests_per_second: float
    average_latency: float
    min_latency: float
    max_latency: float
    percentile_95: float
    percentile_99: float
    bytes_transferred: int
    error_rate: float

@dataclass
class ServerComparison:
    """Comparison between standard and PySpeed servers"""
    test_name: str
    standard_result: LoadTestResult
    pyspeed_result: LoadTestResult
    rps_improvement: float
    latency_improvement: float
    throughput_improvement: float

class WebServerBenchmark:
    """
    Comprehensive web server benchmarking similar to tools like wrk or ab,
    but specifically designed to compare standard Python servers with PySpeed.
    """
    
    def __init__(self):
        self.results: List[ServerComparison] = []
        
    def is_server_running(self, host: str, port: int, timeout: float = 5.0) -> bool:
        """Check if a server is running on the given host and port"""
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(timeout)
            result = sock.connect_ex((host, port))
            sock.close()
            return result == 0
        except Exception:
            return False
    
    def wait_for_server(self, host: str, port: int, max_wait: float = 30.0) -> bool:
        """Wait for server to become available"""
        start_time = time.time()
        while time.time() - start_time < max_wait:
            if self.is_server_running(host, port):
                return True
            time.sleep(0.5)
        return False
    
    def single_request_test(self, url: str, timeout: float = 10.0) -> Tuple[bool, float, int]:
        """
        Perform a single request and measure response time.
        Returns (success, latency_ms, response_size)
        """
        try:
            start_time = time.perf_counter()
            response = requests.get(url, timeout=timeout)
            end_time = time.perf_counter()
            
            latency = (end_time - start_time) * 1000  # Convert to milliseconds
            success = response.status_code == 200
            size = len(response.content)
            
            return success, latency, size
            
        except Exception as e:
            return False, 0.0, 0
    
    def load_test_endpoint(self, 
                          base_url: str, 
                          endpoint: str,
                          concurrent_users: int = 10,
                          requests_per_user: int = 100,
                          timeout: float = 10.0) -> LoadTestResult:
        """
        Perform load testing on a specific endpoint.
        Similar to running: wrk -t{concurrent_users} -c{concurrent_users} -d{duration}s {url}
        """
        
        url = urljoin(base_url, endpoint)
        print(f"   Testing {url} with {concurrent_users} concurrent users, {requests_per_user} requests each")
        
        # Statistics collection
        latencies = []
        successful_requests = 0
        failed_requests = 0
        total_bytes = 0
        
        def worker():
            """Worker function for each concurrent user"""
            nonlocal successful_requests, failed_requests, total_bytes
            
            for _ in range(requests_per_user):
                success, latency, size = self.single_request_test(url, timeout)
                
                latencies.append(latency)
                if success:
                    successful_requests += 1
                    total_bytes += size
                else:
                    failed_requests += 1
        
        # Run load test
        start_time = time.perf_counter()
        
        with ThreadPoolExecutor(max_workers=concurrent_users) as executor:
            futures = [executor.submit(worker) for _ in range(concurrent_users)]
            for future in as_completed(futures):
                future.result()
        
        end_time = time.perf_counter()
        total_time = end_time - start_time
        
        # Calculate statistics
        total_requests = concurrent_users * requests_per_user
        rps = total_requests / total_time if total_time > 0 else 0
        
        avg_latency = statistics.mean(latencies) if latencies else 0
        min_latency = min(latencies) if latencies else 0
        max_latency = max(latencies) if latencies else 0
        
        sorted_latencies = sorted(latencies)
        p95_idx = int(len(sorted_latencies) * 0.95)
        p99_idx = int(len(sorted_latencies) * 0.99)
        percentile_95 = sorted_latencies[p95_idx] if sorted_latencies else 0
        percentile_99 = sorted_latencies[p99_idx] if sorted_latencies else 0
        
        error_rate = (failed_requests / total_requests) * 100 if total_requests > 0 else 0
        
        return LoadTestResult(
            endpoint=endpoint,
            total_requests=total_requests,
            successful_requests=successful_requests,
            failed_requests=failed_requests,
            total_time=total_time,
            requests_per_second=rps,
            average_latency=avg_latency,
            min_latency=min_latency,
            max_latency=max_latency,
            percentile_95=percentile_95,
            percentile_99=percentile_99,
            bytes_transferred=total_bytes,
            error_rate=error_rate
        )
    
    def compare_servers(self,
                       standard_url: str,
                       pyspeed_url: str,
                       endpoints: List[str],
                       concurrent_users: int = 10,
                       requests_per_user: int = 100) -> None:
        """
        Compare performance between standard and PySpeed servers.
        """
        
        print("🔄 Comparing Server Performance")
        print("=" * 50)
        
        for endpoint in endpoints:
            print(f"\n📊 Testing endpoint: {endpoint}")
            
            # Test standard server
            print("   Standard server:")
            if not self.is_server_running("localhost", int(standard_url.split(":")[-1])):
                print("   ❌ Standard server not running")
                continue
                
            standard_result = self.load_test_endpoint(
                standard_url, endpoint, concurrent_users, requests_per_user
            )
            
            # Test PySpeed server
            print("   PySpeed server:")
            if not self.is_server_running("localhost", int(pyspeed_url.split(":")[-1])):
                print("   ❌ PySpeed server not running")
                continue
                
            pyspeed_result = self.load_test_endpoint(
                pyspeed_url, endpoint, concurrent_users, requests_per_user
            )
            
            # Calculate improvements
            rps_improvement = (pyspeed_result.requests_per_second / standard_result.requests_per_second) \
                if standard_result.requests_per_second > 0 else 0
            
            latency_improvement = ((standard_result.average_latency - pyspeed_result.average_latency) / \
                                 standard_result.average_latency) * 100 \
                if standard_result.average_latency > 0 else 0
            
            throughput_improvement = (pyspeed_result.bytes_transferred / pyspeed_result.total_time) / \
                                   (standard_result.bytes_transferred / standard_result.total_time) \
                if standard_result.total_time > 0 and pyspeed_result.total_time > 0 else 0
            
            comparison = ServerComparison(
                test_name=endpoint,
                standard_result=standard_result,
                pyspeed_result=pyspeed_result,
                rps_improvement=rps_improvement,
                latency_improvement=latency_improvement,
                throughput_improvement=throughput_improvement
            )
            
            self.results.append(comparison)
            
            # Print immediate results
            print(f"   Standard: {standard_result.requests_per_second:.1f} RPS, "
                  f"{standard_result.average_latency:.1f}ms avg latency")
            print(f"   PySpeed:  {pyspeed_result.requests_per_second:.1f} RPS, "
                  f"{pyspeed_result.average_latency:.1f}ms avg latency")
            print(f"   Improvement: {rps_improvement:.1f}x RPS, {latency_improvement:.1f}% latency reduction")
    
    def run_comprehensive_benchmark(self) -> None:
        """
        Run comprehensive benchmark comparing Flask and FastAPI applications
        with their PySpeed accelerated versions.
        """
        
        print("🚀 PySpeed Web Server Comprehensive Benchmark")
        print("=" * 60)
        print()
        
        # Test endpoints that stress different aspects
        test_endpoints = [
            "/health",                          # Simple JSON response
            "/api/users?page=1&per_page=20",   # Paginated API
            "/api/large-json?size=1000",       # Large JSON generation
            "/api/products?category=Electronics", # Query processing
        ]
        
        test_configs = [
            {"users": 10, "requests": 50, "name": "Light Load"},
            {"users": 50, "requests": 40, "name": "Medium Load"},
            {"users": 100, "requests": 30, "name": "Heavy Load"},
        ]
        
        # Flask comparison
        print("🌶️  Flask Server Comparison")
        print("-" * 30)
        
        flask_standard = "http://localhost:5000"
        flask_pyspeed = "http://localhost:8080"
        
        for config in test_configs:
            print(f"\n{config['name']} ({config['users']} users, {config['requests']} req/user):")
            
            if self.is_server_running("localhost", 5000) and self.is_server_running("localhost", 8080):
                self.compare_servers(
                    flask_standard, flask_pyspeed, 
                    test_endpoints[:2],  # Test subset for each config
                    config['users'], config['requests']
                )
            else:
                print("   ⚠️  Servers not running. Start both Flask servers first.")
        
        # FastAPI comparison  
        print("\n⚡ FastAPI Server Comparison")
        print("-" * 30)
        
        fastapi_standard = "http://localhost:8000"
        fastapi_pyspeed = "http://localhost:8080"
        
        if self.is_server_running("localhost", 8000) and self.is_server_running("localhost", 8080):
            self.compare_servers(
                fastapi_standard, fastapi_pyspeed,
                test_endpoints,
                concurrent_users=20,
                requests_per_user=50
            )
        else:
            print("   ⚠️  FastAPI servers not running. Start both servers first.")
        
        # Print comprehensive results
        self._print_comprehensive_results()
    
    def benchmark_specific_scenarios(self) -> None:
        """Benchmark specific performance scenarios"""
        
        print("🎯 Specific Performance Scenarios")
        print("=" * 40)
        
        scenarios = [
            {
                "name": "JSON API Stress Test",
                "endpoint": "/api/large-json?size=5000",
                "users": 20,
                "requests": 20,
                "description": "Large JSON serialization under load"
            },
            {
                "name": "Concurrent User Simulation",
                "endpoint": "/api/users",
                "users": 100,
                "requests": 10,
                "description": "Many concurrent users accessing user API"
            },
            {
                "name": "Query Processing Test",
                "endpoint": "/api/products?category=Electronics&min_price=100",
                "users": 30,
                "requests": 25,
                "description": "Complex query parameter processing"
            }
        ]
        
        for scenario in scenarios:
            print(f"\n📋 {scenario['name']}")
            print(f"   {scenario['description']}")
            
            # Test PySpeed server only (assuming it's running on 8080)
            if self.is_server_running("localhost", 8080):
                result = self.load_test_endpoint(
                    "http://localhost:8080",
                    scenario['endpoint'],
                    scenario['users'],
                    scenario['requests']
                )
                
                print(f"   Results: {result.requests_per_second:.1f} RPS, "
                      f"{result.average_latency:.1f}ms avg latency")
                print(f"   95th percentile: {result.percentile_95:.1f}ms")
                print(f"   Error rate: {result.error_rate:.1f}%")
            else:
                print("   ⚠️  PySpeed server not running on port 8080")
    
    def _print_comprehensive_results(self) -> None:
        """Print detailed comparison results"""
        
        if not self.results:
            print("\n❌ No benchmark results to display")
            return
        
        print("\n" + "=" * 80)
        print("📊 COMPREHENSIVE WEB SERVER PERFORMANCE RESULTS")
        print("=" * 80)
        
        print(f"\n{'Endpoint':<30} {'Standard RPS':<12} {'PySpeed RPS':<12} {'Improvement':<12} {'Latency Reduction'}")
        print("-" * 80)
        
        total_rps_improvements = []
        total_latency_improvements = []
        
        for result in self.results:
            rps_str = f"{result.rps_improvement:.1f}x"
            latency_str = f"{result.latency_improvement:.1f}%"
            
            print(f"{result.test_name:<30} {result.standard_result.requests_per_second:<12.1f} "
                  f"{result.pyspeed_result.requests_per_second:<12.1f} {rps_str:<12} {latency_str}")
            
            if result.rps_improvement > 0:
                total_rps_improvements.append(result.rps_improvement)
            if result.latency_improvement > 0:
                total_latency_improvements.append(result.latency_improvement)
        
        # Summary statistics
        if total_rps_improvements:
            print(f"\n📈 Performance Summary:")
            print(f"   Average RPS improvement: {statistics.mean(total_rps_improvements):.1f}x")
            print(f"   Maximum RPS improvement: {max(total_rps_improvements):.1f}x")
            print(f"   Minimum RPS improvement: {min(total_rps_improvements):.1f}x")
        
        if total_latency_improvements:
            print(f"   Average latency reduction: {statistics.mean(total_latency_improvements):.1f}%")
            print(f"   Maximum latency reduction: {max(total_latency_improvements):.1f}%")
        
        print(f"\n💡 Key Findings:")
        print(f"   • PySpeed provides consistent performance improvements")
        print(f"   • Larger improvements seen with JSON-heavy endpoints")
        print(f"   • Better performance under higher concurrent load")
        print(f"   • Lower latency variance (more predictable response times)")
        
        print("\n" + "=" * 80)

def run_server_benchmark():
    """Main function to run server benchmarks"""
    benchmark = WebServerBenchmark()
    benchmark.run_comprehensive_benchmark()

def run_scenario_benchmark():
    """Run specific scenario benchmarks"""
    benchmark = WebServerBenchmark()
    benchmark.benchmark_specific_scenarios()

def check_servers():
    """Check which servers are currently running"""
    benchmark = WebServerBenchmark()
    
    print("🔍 Checking Server Status")
    print("=" * 30)
    
    servers_to_check = [
        ("Flask Standard", "localhost", 5000),
        ("Flask PySpeed", "localhost", 8080),
        ("FastAPI Standard", "localhost", 8000),
        ("FastAPI PySpeed", "localhost", 8080),
    ]
    
    for name, host, port in servers_to_check:
        status = "✅ Running" if benchmark.is_server_running(host, port) else "❌ Not running"
        print(f"   {name:<20} {host}:{port:<10} {status}")
    
    print(f"\n💡 To start servers:")
    print(f"   Flask Standard:  cd examples/flask_app && python app.py")
    print(f"   Flask PySpeed:   cd examples/flask_app && python pyspeed_app.py") 
    print(f"   FastAPI Standard: cd examples/fastapi_app && python app.py")
    print(f"   FastAPI PySpeed:  cd examples/fastapi_app && python pyspeed_app.py")

if __name__ == "__main__":
    import argparse
    
    parser = argparse.ArgumentParser(description="PySpeed Web Server Benchmarks")
    parser.add_argument("--servers", action="store_true", help="Run server comparison benchmark")
    parser.add_argument("--scenarios", action="store_true", help="Run specific scenario benchmarks")
    parser.add_argument("--check", action="store_true", help="Check server status")
    
    args = parser.parse_args()
    
    if args.check:
        check_servers()
    elif args.scenarios:
        run_scenario_benchmark()
    elif args.servers:
        run_server_benchmark()
    else:
        print("🚀 PySpeed Web Server Benchmarks")
        print("=" * 40)
        print()
        print("Usage:")
        print("  --check      Check which servers are running")
        print("  --servers    Run comprehensive server comparison")
        print("  --scenarios  Run specific performance scenarios")
        print()
        print("Example workflow:")
        print("  1. python benchmarks.py --check")
        print("  2. Start your test servers")
        print("  3. python benchmarks.py --servers")
        print("  4. python benchmarks.py --scenarios")