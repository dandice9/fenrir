#!/bin/bash

# Fenrir Build Script
# Builds the library, tests, and examples

set -e

echo "╔════════════════════════════════════════╗"
echo "║   Building Fenrir C++20 Library       ║"
echo "╚════════════════════════════════════════╝"
echo ""

# Colors
GREEN='\033[0;32m'
BLUE='\033[0;34m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# Check for required tools
echo -e "${BLUE}Checking requirements...${NC}"

if ! command -v cmake &> /dev/null; then
    echo -e "${RED}✗ CMake not found${NC}"
    exit 1
fi

if ! command -v clang++ &> /dev/null; then
    echo -e "${RED}✗ Clang++ not found${NC}"
    exit 1
fi

# Check for libpq (lightweight option)
PG_CONFIG=""
if command -v pg_config &> /dev/null; then
    PG_CONFIG="pg_config"
elif [ -f "/opt/homebrew/opt/libpq/bin/pg_config" ]; then
    PG_CONFIG="/opt/homebrew/opt/libpq/bin/pg_config"
    export PATH="/opt/homebrew/opt/libpq/bin:$PATH"
elif [ -f "/usr/local/opt/libpq/bin/pg_config" ]; then
    PG_CONFIG="/usr/local/opt/libpq/bin/pg_config"
    export PATH="/usr/local/opt/libpq/bin:$PATH"
else
    echo -e "${RED}✗ PostgreSQL/libpq (pg_config) not found${NC}"
    echo "  Install with: brew install libpq (macOS)"
    exit 1
fi

echo -e "${GREEN}✓ Found pg_config: $PG_CONFIG${NC}"

echo -e "${GREEN}✓ All requirements met${NC}"
echo ""

# Use system clang++ explicitly
export CXX=/usr/bin/clang++
CLANG_VERSION=$($CXX --version | head -n 1)
echo "Using: $CLANG_VERSION"
echo "Note: AppleClang doesn't support std::expected yet"
echo ""

# Clean previous build
if [ -d "build" ]; then
    echo "Cleaning previous build..."
    rm -rf build
fi

# Create build directory
mkdir -p build
cd build

# Configure with CMake
echo -e "${BLUE}Configuring with CMake...${NC}"
cmake .. \
    -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
    -DCMAKE_CXX_STANDARD=20 \
    -DBUILD_TESTS=ON \
    -DBUILD_EXAMPLES=ON \
    -DCMAKE_BUILD_TYPE=Release

# Build
echo ""
echo -e "${BLUE}Building...${NC}"
cmake --build . -j$(sysctl -n hw.ncpu 2>/dev/null || nproc)

echo ""
echo -e "${GREEN}✓ Build complete!${NC}"
echo ""

# List built executables
echo "Built executables:"
echo "  - connection_test"
echo "  - query_test"
echo "  - transaction_test"
echo "  - pool_test"
echo "  - usage_example"
echo ""

# Offer to run tests
read -p "Run tests now? (y/N) " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    echo ""
    echo -e "${BLUE}Running tests...${NC}"
    echo "Note: Tests require PostgreSQL running with:"
    echo "  Database: testdb"
    echo "  User: testuser"
    echo "  Password: testpass"
    echo ""
    
    ctest --output-on-failure || echo -e "${RED}Tests failed (make sure PostgreSQL is configured)${NC}"
fi

echo ""
echo -e "${GREEN}╔════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║   Build Complete!                     ║${NC}"
echo -e "${GREEN}╚════════════════════════════════════════╝${NC}"
echo ""
echo "To run the example:"
echo "  cd build && ./usage_example"
