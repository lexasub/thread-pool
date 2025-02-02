cmake_minimum_required(VERSION 3.5)
project(thread-pool)

add_executable("${PROJECT_NAME}_test"
        src/arg_parser.cpp
        src/synced_stream.cpp
        src/version.cpp
        tests/BS_thread_pool_test.cpp
)

target_include_directories("${PROJECT_NAME}_test" PRIVATE . src)