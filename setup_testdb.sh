#!/bin/bash
# Setup script for Fenrir PostgreSQL test database

set -e

echo "Setting up PostgreSQL test database for Fenrir..."
echo ""

# Check if PostgreSQL is installed
if ! command -v psql &> /dev/null; then
    echo "Error: PostgreSQL (psql) is not installed or not in PATH"
    echo "On macOS: brew install postgresql"
    echo "On Ubuntu: sudo apt install postgresql"
    exit 1
fi

# Check if PostgreSQL is running
if ! pg_isready -q; then
    echo "Error: PostgreSQL server is not running"
    echo "On macOS: brew services start postgresql"
    echo "On Ubuntu: sudo systemctl start postgresql"
    exit 1
fi

echo "Creating test database and user..."
echo ""

# Create database and user
psql postgres << 'EOF' 2>&1 | grep -v "already exists" || true
-- Drop existing objects if they exist
DROP DATABASE IF EXISTS testdb;
DROP USER IF EXISTS testuser;

-- Create test database
CREATE DATABASE testdb;

-- Create test user
CREATE USER testuser WITH PASSWORD 'testpass';

-- Grant privileges
GRANT ALL PRIVILEGES ON DATABASE testdb TO testuser;

-- Connect to test database and grant schema privileges
\c testdb
GRANT ALL ON SCHEMA public TO testuser;
GRANT ALL PRIVILEGES ON ALL TABLES IN SCHEMA public TO testuser;
GRANT ALL PRIVILEGES ON ALL SEQUENCES IN SCHEMA public TO testuser;
ALTER DEFAULT PRIVILEGES IN SCHEMA public GRANT ALL ON TABLES TO testuser;
ALTER DEFAULT PRIVILEGES IN SCHEMA public GRANT ALL ON SEQUENCES TO testuser;
EOF

echo ""
echo "✓ Database setup complete!"
echo ""
echo "Connection details:"
echo "  Database: testdb"
echo "  User:     testuser"
echo "  Password: testpass"
echo "  Host:     localhost"
echo "  Port:     5432"
echo ""
echo "Connection string:"
echo "  host=localhost port=5432 dbname=testdb user=testuser password=testpass"
echo ""
echo "You can now build and run the tests:"
echo "  cd build"
echo "  cmake .."
echo "  make -j\$(nproc)"
echo "  ctest --verbose"
echo ""
