cmake_minimum_required(VERSION 3.10)

project(HttpRemocon)

# C++20を有効にする
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSVCのソースファイルエンコーディングを設定
if(MSVC)
    add_compile_options(/utf-8)
endif()

# LibISDBのソースファイルを収集
file(GLOB_RECURSE LIBISDB_SOURCES 
    "LibISDB/LibISDB/Base/*.cpp"
    "LibISDB/LibISDB/Engine/*.cpp"
    "LibISDB/LibISDB/Filters/*.cpp"
    "LibISDB/LibISDB/TS/*.cpp"
    "LibISDB/LibISDB/EPG/*.cpp"
    "LibISDB/LibISDB/Utilities/*.cpp"
)

# LibISDBの静的ライブラリを作成
add_library(LibISDB STATIC ${LIBISDB_SOURCES})

# LibISDBのインクルードディレクトリを設定
target_include_directories(LibISDB PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/LibISDB"
)

# LibISDBのコンパイルオプション
target_compile_definitions(LibISDB PRIVATE
    LIBISDB_WCHAR
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# cpp-httplibを検索
find_package(httplib CONFIG REQUIRED)

# HttpRemoconをDLLとして作成（TVTestプラグイン）
add_library(HttpRemocon SHARED dllmain.cpp)

# HttpRemoconのインクルードディレクトリを設定
target_include_directories(HttpRemocon PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/LibISDB"
)

# HttpRemoconのコンパイルオプション
target_compile_definitions(HttpRemocon PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# ライブラリをリンク
target_link_libraries(HttpRemocon PRIVATE 
    httplib::httplib
    LibISDB
)

# DLLの出力名を.tvtpに設定
set_target_properties(HttpRemocon PROPERTIES 
    SUFFIX ".tvtp"
    PREFIX ""
)
