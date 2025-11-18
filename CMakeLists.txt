cmake_minimum_required(VERSION 3.20)
project(fenrir VERSION 1.0.0 LANGUAGES CXX)

# Set C++20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler flags for C++20 features
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # Check if AppleClang (uses polyfill for std::expected)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
        message(STATUS "AppleClang detected - using std::expected polyfill")
    endif()
    add_compile_options(-std=c++20 -stdlib=libc++)
    add_link_options(-stdlib=libc++)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    add_compile_options(-std=c++20)
endif()

# Include directories
find_package(Boost REQUIRED)
include_directories(${Boost_INCLUDE_DIRS})

# Find libpq (PostgreSQL client library)
# Try Homebrew paths first on macOS
if(APPLE)
    # Check common Homebrew locations
    set(LIBPQ_PATHS
        /opt/homebrew/opt/libpq          # Apple Silicon
        /usr/local/opt/libpq             # Intel Mac
        /opt/homebrew/opt/postgresql@16   # Versioned
        /opt/homebrew/opt/postgresql@15
        /usr/local/opt/postgresql@16
        /usr/local/opt/postgresql@15
    )
    
    foreach(LIBPQ_PATH ${LIBPQ_PATHS})
        if(EXISTS "${LIBPQ_PATH}/include")
            set(PostgreSQL_INCLUDE_DIR "${LIBPQ_PATH}/include")
            set(PostgreSQL_LIBRARY_DIR "${LIBPQ_PATH}/lib")
            break()
        endif()
    endforeach()
    
    # If found in Homebrew, set the variables
    if(PostgreSQL_INCLUDE_DIR)
        message(STATUS "Found libpq in Homebrew: ${PostgreSQL_INCLUDE_DIR}")
        find_library(PostgreSQL_LIBRARY pq PATHS ${PostgreSQL_LIBRARY_DIR} NO_DEFAULT_PATH)
    else()
        # Fallback to system package
        find_package(PostgreSQL REQUIRED)
    endif()
else()
    # Linux and other systems - use standard FindPostgreSQL
    find_package(PostgreSQL REQUIRED)
endif()

# Verify we found libpq
if(NOT PostgreSQL_LIBRARY)
    message(FATAL_ERROR "libpq not found. Install with: brew install libpq")
endif()

# Header-only library interface
add_library(fenrir INTERFACE)
target_include_directories(fenrir INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include>
    ${PostgreSQL_INCLUDE_DIR}
)
target_link_libraries(fenrir INTERFACE ${PostgreSQL_LIBRARY})
target_compile_features(fenrir INTERFACE cxx_std_20)

# Export compile commands for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Option to build tests
option(BUILD_TESTS "Build test programs" ON)
option(BUILD_EXAMPLES "Build example programs" ON)

# Tests
if(BUILD_TESTS)
    enable_testing()
    
    # Find Catch2
    find_package(Catch2 3 QUIET)
    if(NOT Catch2_FOUND)
        message(STATUS "Catch2 not found, fetching from GitHub...")
        include(FetchContent)
        FetchContent_Declare(
            Catch2
            GIT_REPOSITORY https://github.com/catchorg/Catch2.git
            GIT_TAG v3.5.0
        )
        FetchContent_MakeAvailable(Catch2)
        list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    endif()
    
    # Connection tests
    add_executable(connection_test tests/connection_test.cpp)
    target_link_libraries(connection_test PRIVATE fenrir Catch2::Catch2WithMain)
    add_test(NAME ConnectionTest COMMAND connection_test)
    
    # Query tests
    add_executable(query_test tests/query_test.cpp)
    target_link_libraries(query_test PRIVATE fenrir Catch2::Catch2WithMain)
    add_test(NAME QueryTest COMMAND query_test)
    
    # Transaction tests
    add_executable(transaction_test tests/transaction_test.cpp)
    target_link_libraries(transaction_test PRIVATE fenrir Catch2::Catch2WithMain)
    add_test(NAME TransactionTest COMMAND transaction_test)
    
    # Pool tests
    add_executable(pool_test tests/pool_test.cpp)
    target_link_libraries(pool_test PRIVATE fenrir Catch2::Catch2WithMain)
    add_test(NAME PoolTest COMMAND pool_test)
    
    # Stored procedure tests
    add_executable(stored_procedure_test tests/stored_procedure_test.cpp)
    target_link_libraries(stored_procedure_test PRIVATE fenrir Catch2::Catch2WithMain)
    add_test(NAME StoredProcedureTest COMMAND stored_procedure_test)
    
    # Catch2 test discovery
    if(Catch2_FOUND)
        include(Catch)
        catch_discover_tests(connection_test)
        catch_discover_tests(query_test)
        catch_discover_tests(transaction_test)
        catch_discover_tests(pool_test)
        catch_discover_tests(stored_procedure_test)
    endif()
endif()

# Examples
if(BUILD_EXAMPLES)
    add_executable(usage_example examples/usage_example.cpp)
    target_link_libraries(usage_example PRIVATE fenrir)
endif()

# Installation
include(GNUInstallDirs)

install(TARGETS fenrir
    EXPORT fenrirTargets
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY src/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/fenrir
    FILES_MATCHING PATTERN "*.hpp"
)

install(EXPORT fenrirTargets
    FILE fenrirTargets.cmake
    NAMESPACE fenrir::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fenrir
)

# Create package config file
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/fenrirConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/fenrirConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/fenrirConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fenrir
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/fenrirConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/fenrirConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fenrir
)
