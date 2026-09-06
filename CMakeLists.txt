cmake_minimum_required(VERSION 3.16)

# Project setup
project(MCLang VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# --------------------------------------------------
# Utils - vendored static library (submodule).
# Built directly here rather than via add_subdirectory so its (currently
# broken) examples/ tree is not part of this build.
# --------------------------------------------------
file(GLOB_RECURSE UTILS_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/Utils/src/*.cpp)

add_library(utils STATIC ${UTILS_SOURCES})
target_compile_features(utils PUBLIC cxx_std_23)
target_include_directories(utils
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib/Utils/include
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lib/Utils/src)

# --------------------------------------------------
# NewSyntaxParser - parser engine as a static library (Syntax/ + Tokenizer/
# only; skip its main.cpp and tests, and avoid re-adding Utils).
# --------------------------------------------------
file(GLOB SYNPARSER_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/NewSyntaxParser/src/Syntax/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/NewSyntaxParser/src/Tokenizer/*.cpp)

add_library(synparser STATIC ${SYNPARSER_SOURCES})
target_compile_features(synparser PUBLIC cxx_std_23)
target_include_directories(synparser PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/NewSyntaxParser/src
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/NewSyntaxParser/include)
target_link_libraries(synparser PUBLIC utils)

# --------------------------------------------------
# MCLang - standalone executable
# --------------------------------------------------
file(GLOB_RECURSE MCLANG_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

add_executable(MCLang ${MCLANG_SOURCES}
        src/AstBuilder.h)
target_compile_features(MCLang PRIVATE cxx_std_23)
target_link_libraries(MCLang PRIVATE utils synparser)
target_include_directories(MCLang PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
