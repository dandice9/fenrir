#include "../src/database_query.hpp"
#include <iostream>

// Just test that it compiles
int main() {
    std::cout << "Query builder compiles successfully!\n";
    
    // Test concepts at compile time
    static_assert(fenrir::SelectQuery<fenrir::query_state<fenrir::select_query_tag, false, false, false, false>>);
    static_assert(fenrir::InsertQuery<fenrir::query_state<fenrir::insert_query_tag, false, false, false, false>>);
    static_assert(fenrir::UpdateQuery<fenrir::query_state<fenrir::update_query_tag, false, false, false, false>>);
    static_assert(fenrir::DeleteQuery<fenrir::query_state<fenrir::delete_query_tag, false, false, false, false>>);
    
    std::cout << "All concepts validated at compile time!\n";
    
    return 0;
}
