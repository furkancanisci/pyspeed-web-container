from pybind11.setup_helpers import Pybind11Extension, build_ext
from pybind11 import get_cmake_dir
import pybind11
from pathlib import Path
import platform
from setuptools import setup, Extension, find_packages

# The main interface is through Pybind11Extension.
ext_modules = [
    Pybind11Extension(
        "pyspeed_accelerated",
        [
            "src/cpp/python_bridge.cpp",
            "src/cpp/http_server.cpp", 
            "src/cpp/request_parser.cpp",
            "src/cpp/response_builder.cpp",
            "src/cpp/static_handler.cpp",
            "src/cpp/json_accelerator.cpp",
        ],
        include_dirs=[
            # Path to pybind11 headers
            pybind11.get_include(),
            "src/cpp",
        ],
        libraries=["boost_system", "boost_thread"],
        cxx_std=17,
        define_macros=[("VERSION_INFO", '"1.0.0"')],
    ),
]

# Platform-specific compilation flags
if platform.system() == "Darwin":  # macOS
    for ext in ext_modules:
        ext.cxx_std = 17
        ext.extra_compile_args = ["-O3", "-march=native", "-std=c++17"]
        ext.extra_link_args = ["-undefined", "dynamic_lookup"]
elif platform.system() == "Linux":
    for ext in ext_modules:
        ext.cxx_std = 17
        ext.extra_compile_args = ["-O3", "-march=native", "-std=c++17"]
        ext.libraries.extend(["pthread"])
elif platform.system() == "Windows":
    for ext in ext_modules:
        ext.cxx_std = 17
        ext.extra_compile_args = ["/O2", "/std:c++17"]

# Read README for long description
this_directory = Path(__file__).parent
long_description = (this_directory / "README.md").read_text()

setup(
    name="pyspeed-web-container",
    version="1.0.0",
    author="Furkan Can Isci",
    author_email="",
    description="High-performance C++ container for Python web applications",
    long_description=long_description,
    long_description_content_type="text/markdown",
    url="https://github.com/furkancanisci/pyspeed-web-container",
    packages=find_packages(where="src/python"),
    package_dir={"": "src/python"},
    ext_modules=ext_modules,
    cmdclass={"build_ext": build_ext},
    python_requires=">=3.8",
    install_requires=[
        "pybind11>=2.10.0",
        "flask>=2.0.0",
        "fastapi>=0.68.0",
        "uvicorn>=0.15.0",
        "requests>=2.25.0",
    ],
    extras_require={
        "dev": [
            "pytest>=6.0",
            "pytest-benchmark>=3.4.0",
            "black>=21.0.0",
            "flake8>=3.9.0",
        ],
        "benchmark": [
            "wrk>=0.1.0",
            "matplotlib>=3.3.0",
            "pandas>=1.3.0",
        ]
    },
    classifiers=[
        "Development Status :: 4 - Beta",
        "Intended Audience :: Developers",
        "License :: OSI Approved :: MIT License",
        "Operating System :: OS Independent",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3.8",
        "Programming Language :: Python :: 3.9",
        "Programming Language :: Python :: 3.10",
        "Programming Language :: Python :: 3.11",
        "Programming Language :: C++",
        "Topic :: Internet :: WWW/HTTP :: HTTP Servers",
        "Topic :: Software Development :: Libraries :: Python Modules",
        "Topic :: System :: Networking",
    ],
    keywords="python, c++, web, performance, acceleration, http, server",
    project_urls={
        "Bug Reports": "https://github.com/furkancanisci/pyspeed-web-container/issues",
        "Source": "https://github.com/furkancanisci/pyspeed-web-container",
        "Documentation": "https://github.com/furkancanisci/pyspeed-web-container/blob/main/README.md",
    },
)