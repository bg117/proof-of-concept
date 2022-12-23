cmake_minimum_required(VERSION 3.0)

set(CMAKE_CXX_STANDARD 17)

add_executable(fatfs "main.cpp" "fatfs_file_allocation_table.cpp" "include/fatfs/file_allocation_table.hpp" "include/fatfs/errors.hpp" "include/fatfs/priv/file_allocation_table__impl.hpp" "priv/fatfs_priv_file_allocation_table__impl.cpp" "include/fatfs/priv/priv.hpp")
target_include_directories(fatfs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)