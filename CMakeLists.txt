cmake_minimum_required(VERSION 3.16)
project(SearchEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
    add_compile_options(/W4 /utf-8 /EHsc)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(Threads REQUIRED)
find_package(Boost REQUIRED COMPONENTS locale)

# libpqxx: сначала CMake-конфиг (vcpkg, сборка из исходников), затем pkg-config (Linux-пакеты)
find_package(libpqxx CONFIG QUIET)
if(TARGET libpqxx::pqxx)
    set(PQXX_TARGET libpqxx::pqxx)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(PQXX REQUIRED IMPORTED_TARGET libpqxx)
    set(PQXX_TARGET PkgConfig::PQXX)
endif()

# Общий код обеих программ
add_library(search_common STATIC
    common/config.cpp
    common/database.cpp
    common/text_processor.cpp
    common/thread_pool.cpp
    common/console.cpp
)
target_include_directories(search_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/common)
target_link_libraries(search_common PUBLIC ${PQXX_TARGET} Boost::locale Threads::Threads)

# ICU в Boost.Locale по умолчанию не используется: в сборке Boost под Windows
# ICU требует внешний файл данных icudt*.dat (иначе U_MISSING_RESOURCE_ERROR).
# Без ICU используется бэкенд winapi (Windows) или posix/std (Linux, macOS).
option(SEARCH_ENGINE_USE_ICU "Использовать бэкенд ICU в Boost.Locale" OFF)
if(SEARCH_ENGINE_USE_ICU)
    target_compile_definitions(search_common PRIVATE SEARCH_ENGINE_USE_ICU)
endif()

add_executable(indexer indexer/main.cpp indexer/indexer.cpp)
target_link_libraries(indexer PRIVATE search_common)

add_executable(searcher searcher/main.cpp searcher/search_engine.cpp)
target_link_libraries(searcher PRIVATE search_common)

# Копируем config.ini рядом с исполняемыми файлами
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.ini ${CMAKE_CURRENT_BINARY_DIR}/config.ini COPYONLY)
