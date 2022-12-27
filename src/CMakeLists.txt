cmake_minimum_required(VERSION 3.0)

set(CMAKE_CXX_STANDARD 20)

add_executable(fatfs "main.cpp" "FileAllocationTable.cpp" "include/fatfs/FileAllocationTable.hpp" "include/fatfs/Errors.hpp" "priv/include/fatfs/FileAllocationTable.impl.hpp" "priv/FileAllocationTable.impl.cpp" include/fatfs/Helpers.hpp include/fatfs/Structures.hpp Helpers.cpp String.cpp include/utilities/String.hpp)
target_include_directories(fatfs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/priv/include)
