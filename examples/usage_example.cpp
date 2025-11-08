#include <iostream>
#include "../src/fenrir.hpp"

using namespace fenrir;

constexpr const char* CONNECTION_STRING = "host=localhost dbname=testdb user=testuser password=testpass";

int main() {
    try {
        database_connection conn(CONNECTION_STRING);
        std::cout << "Connected to: " << conn.database_name() << "\n";
        
        auto result = conn.execute("SELECT version()");
        query_result qr(result);
        auto version = qr.get<std::string>(0, 0);
        std::cout << "PostgreSQL version: " << version.value_or("Unknown") << "\n";
        
        return 0;
    } catch (const database_error& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
