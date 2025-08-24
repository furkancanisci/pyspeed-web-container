"""
PySpeed Container - Main container interface for web applications.

This module provides the core container functionality that wraps Python web applications
with high-performance C++ acceleration.
"""

import logging
import asyncio
from typing import Any, Dict, Callable, Optional
from concurrent.futures import ThreadPoolExecutor

try:
    import pyspeed_accelerated
except ImportError:
    logging.error("C++ acceleration module not found. Run 'make build' first.")
    raise

logger = logging.getLogger(__name__)

class WebApplicationAdapter:
    """Base adapter for web applications."""
    
    def __init__(self, app: Any):
        self.app = app
        self.framework_name = "unknown"
    
    def handle_request(self, request: pyspeed_accelerated.Request) -> pyspeed_accelerated.Response:
        """Handle incoming HTTP request and return response."""
        raise NotImplementedError("Subclasses must implement handle_request")
    
    def setup(self):
        """Perform any setup required for the adapter."""
        pass
    
    def cleanup(self):
        """Cleanup resources when container stops."""
        pass

class FlaskAdapter(WebApplicationAdapter):
    """Adapter for Flask applications."""
    
    def __init__(self, app: Any):
        super().__init__(app)
        self.framework_name = "flask"
        
        # Import Flask components
        try:
            from flask import Flask
            from werkzeug.test import Client
            from werkzeug.serving import WSGIRequestHandler
            from werkzeug.wrappers import Response as WerkzeugResponse
            import io
            
            self.Flask = Flask
            self.Client = Client
            self.WerkzeugResponse = WerkzeugResponse
            self.io = io
        except ImportError:
            raise ImportError("Flask not found. Install with: pip install flask")
    
    def handle_request(self, request: pyspeed_accelerated.Request) -> pyspeed_accelerated.Response:
        """Handle Flask request using WSGI interface."""
        try:
            # Build WSGI environ
            environ = self._build_environ(request)
            
            # Call Flask application
            start_response_called = []
            response_data = []
            
            def start_response(status, headers, exc_info=None):
                start_response_called.append((status, headers))
            
            # Get response from Flask app
            response_iter = self.app(environ, start_response)
            
            # Collect response body
            body = b''.join(response_iter)
            
            # Get status and headers
            if start_response_called:
                status_line, headers = start_response_called[0]
                status_code = int(status_line.split(' ', 1)[0])
            else:
                status_code = 500
                headers = []
            
            # Build PySpeed response
            response = pyspeed_accelerated.Response()
            response.status_code = status_code
            response.body = body.decode('utf-8', errors='replace')
            
            # Add headers
            for name, value in headers:
                response.headers[name.lower()] = value
            
            return response
            
        except Exception as e:
            logger.error(f"Flask adapter error: {e}")
            return pyspeed_accelerated.make_error_response(500, f"Flask error: {str(e)}")
    
    def _build_environ(self, request: pyspeed_accelerated.Request) -> Dict[str, Any]:
        """Build WSGI environ from PySpeed request."""
        environ = {
            'REQUEST_METHOD': request.method,
            'SCRIPT_NAME': '',
            'PATH_INFO': request.path,
            'QUERY_STRING': request.query_string,
            'CONTENT_TYPE': request.content_type or '',
            'CONTENT_LENGTH': str(request.content_length),
            'SERVER_NAME': 'localhost',
            'SERVER_PORT': '8080',
            'SERVER_PROTOCOL': request.protocol_version,
            'wsgi.version': (1, 0),
            'wsgi.url_scheme': 'http',
            'wsgi.input': self.io.BytesIO(request.body.encode('utf-8')),
            'wsgi.errors': self.io.StringIO(),
            'wsgi.multithread': True,
            'wsgi.multiprocess': False,
            'wsgi.run_once': False,
        }
        
        # Add headers
        for name, value in request.headers.items():
            key = f"HTTP_{name.upper().replace('-', '_')}"
            environ[key] = value
        
        return environ

class FastAPIAdapter(WebApplicationAdapter):
    """Adapter for FastAPI applications."""
    
    def __init__(self, app: Any):
        super().__init__(app)
        self.framework_name = "fastapi"
        self.executor = ThreadPoolExecutor(max_workers=4)
        
        try:
            import asyncio
            from starlette.applications import Starlette
            from starlette.requests import Request as StarletteRequest
            from starlette.responses import Response as StarletteResponse
            from starlette.testclient import TestClient
            
            self.asyncio = asyncio
            self.StarletteRequest = StarletteRequest
            self.StarletteResponse = StarletteResponse
            self.TestClient = TestClient
            
        except ImportError:
            raise ImportError("FastAPI/Starlette not found. Install with: pip install fastapi")
    
    def handle_request(self, request: pyspeed_accelerated.Request) -> pyspeed_accelerated.Response:
        """Handle FastAPI request using ASGI interface."""
        try:
            # Use TestClient for now - in production, we'd implement full ASGI
            client = self.TestClient(self.app)
            
            # Build request parameters
            method = request.method.lower()
            url = request.path
            if request.query_string:
                url += '?' + request.query_string
            
            headers = dict(request.headers)
            data = request.body if request.body else None
            
            # Make request through test client
            if method == 'get':
                response = client.get(url, headers=headers)
            elif method == 'post':
                response = client.post(url, headers=headers, data=data)
            elif method == 'put':
                response = client.put(url, headers=headers, data=data)
            elif method == 'delete':
                response = client.delete(url, headers=headers)
            else:
                response = client.request(method, url, headers=headers, data=data)
            
            # Convert to PySpeed response
            pyspeed_response = pyspeed_accelerated.Response()
            pyspeed_response.status_code = response.status_code
            pyspeed_response.body = response.text
            
            # Add headers
            for name, value in response.headers.items():
                pyspeed_response.headers[name.lower()] = value
            
            return pyspeed_response
            
        except Exception as e:
            logger.error(f"FastAPI adapter error: {e}")
            return pyspeed_accelerated.make_error_response(500, f"FastAPI error: {str(e)}")
    
    def cleanup(self):
        """Cleanup thread pool."""
        if self.executor:
            self.executor.shutdown(wait=True)

class GenericAdapter(WebApplicationAdapter):
    """Generic adapter for unknown frameworks."""
    
    def __init__(self, app: Any):
        super().__init__(app)
        self.framework_name = "generic"
    
    def handle_request(self, request: pyspeed_accelerated.Request) -> pyspeed_accelerated.Response:
        """Handle generic request by returning request information."""
        try:
            import json
            
            # Create response with request information
            response_data = {
                "framework": "generic",
                "method": request.method,
                "path": request.path,
                "headers": dict(request.headers),
                "query_params": dict(request.query_params),
                "form_data": dict(request.form_data),
                "cookies": dict(request.cookies),
                "content_type": request.content_type,
                "content_length": request.content_length,
                "is_json": request.is_valid_json,
                "body_preview": request.body[:200] + "..." if len(request.body) > 200 else request.body,
                "message": "Processed by PySpeed Generic Adapter"
            }
            
            return pyspeed_accelerated.make_json_response(
                json.dumps(response_data, indent=2)
            )
            
        except Exception as e:
            logger.error(f"Generic adapter error: {e}")
            return pyspeed_accelerated.make_error_response(500, f"Generic error: {str(e)}")

def create_adapter(app: Any, framework: str = "auto") -> WebApplicationAdapter:
    """
    Create appropriate adapter for the given web application.
    
    Args:
        app: Web application instance
        framework: Framework type ('flask', 'fastapi', 'generic', 'auto')
    
    Returns:
        Appropriate adapter instance
    """
    if framework == "auto":
        framework = detect_framework(app)
    
    if framework == "flask":
        return FlaskAdapter(app)
    elif framework == "fastapi":
        return FastAPIAdapter(app)
    else:
        logger.warning(f"Unknown framework '{framework}', using generic adapter")
        return GenericAdapter(app)

def detect_framework(app: Any) -> str:
    """
    Auto-detect web framework from application instance.
    
    Args:
        app: Web application instance
    
    Returns:
        Detected framework name
    """
    app_type = type(app).__name__
    module_name = type(app).__module__
    
    # Check module name first
    if 'flask' in module_name.lower():
        return 'flask'
    elif 'fastapi' in module_name.lower() or 'starlette' in module_name.lower():
        return 'fastapi'
    elif 'django' in module_name.lower():
        return 'django'
    
    # Check app characteristics
    if hasattr(app, 'wsgi_app') and hasattr(app, 'route'):
        return 'flask'
    elif hasattr(app, 'router') and hasattr(app, 'middleware'):
        return 'fastapi'
    elif hasattr(app, 'urls'):
        return 'django'
    
    logger.warning(f"Could not detect framework for {app_type} from {module_name}")
    return 'generic'