cmake_minimum_required(VERSION 3.20)
project(minidragon LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# ── Size optimization flags ─────────────────────────────────────────
option(OPTIMIZE_SIZE "Optimize for binary size (-Os, LTO, strip)" ON)
if(OPTIMIZE_SIZE)
  # Optimize for size instead of speed
  string(REPLACE "-O2" "-Os" CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")
  string(REPLACE "-O2" "-Os" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
  string(REPLACE "-O3" "-Os" CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")
  string(REPLACE "-O3" "-Os" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")

  # Link-Time Optimization (LTO)
  if(MSVC)
    add_compile_options(/GL)
    add_link_options(/LTCG /OPT:REF /OPT:ICF)
  else()
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    add_compile_options(-fdata-sections -ffunction-sections)
    add_link_options(-Wl,--gc-sections)
  endif()
endif()

# OpenSSL (optional - needed for HTTPS/Telegram)
option(USE_OPENSSL "Enable OpenSSL for HTTPS support" ON)
if(USE_OPENSSL)
  find_package(OpenSSL QUIET)
  if(NOT OpenSSL_FOUND)
    message(WARNING "OpenSSL not found, building without HTTPS support")
    set(USE_OPENSSL OFF)
  endif()
endif()

# nlohmann/json
FetchContent_Declare(json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)
FetchContent_MakeAvailable(json)

# cpp-httplib (disable brotli/zlib to shrink binary)
set(HTTPLIB_USE_BROTLI_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_REQUIRE_BROTLI OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_ZLIB_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_REQUIRE_ZLIB OFF CACHE BOOL "" FORCE)
FetchContent_Declare(httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG v0.15.3
)
FetchContent_MakeAvailable(httplib)

# sqlite3 amalgamation (minimized build)
FetchContent_Declare(sqlite3
  URL https://www.sqlite.org/2024/sqlite-amalgamation-3450000.zip
)
FetchContent_GetProperties(sqlite3)
if(NOT sqlite3_POPULATED)
  FetchContent_Populate(sqlite3)
  add_library(sqlite3 STATIC ${sqlite3_SOURCE_DIR}/sqlite3.c)
  target_include_directories(sqlite3 PUBLIC ${sqlite3_SOURCE_DIR})
  # Disable unused SQLite features to shrink binary
  target_compile_definitions(sqlite3 PRIVATE
    SQLITE_DQS=0
    SQLITE_THREADSAFE=0
    SQLITE_DEFAULT_MEMSTATUS=0
    SQLITE_DEFAULT_WAL_SYNCHRONOUS=1
    SQLITE_LIKE_DOESNT_MATCH_BLOBS
    SQLITE_OMIT_DEPRECATED
    SQLITE_OMIT_PROGRESS_CALLBACK
    SQLITE_OMIT_SHARED_CACHE
    SQLITE_OMIT_AUTOINIT
    SQLITE_OMIT_COMPLETE
    SQLITE_OMIT_DECLTYPE
    SQLITE_OMIT_LOAD_EXTENSION
    SQLITE_OMIT_TCL_VARIABLE
    SQLITE_OMIT_TRACE
    SQLITE_OMIT_UTF16
    SQLITE_OMIT_GET_TABLE
    SQLITE_OMIT_AUTHORIZATION
    SQLITE_OMIT_EXPLAIN
    SQLITE_OMIT_INTEGRITY_CHECK
    SQLITE_OMIT_COMPILEOPTION_DIAGS
    SQLITE_ENABLE_FTS5
    SQLITE_MAX_EXPR_DEPTH=0
  )
  if(UNIX)
    target_link_libraries(sqlite3 PRIVATE pthread dl)
  endif()
endif()

# ── Core library (everything except main.cpp and gui/) ──────────────
file(GLOB_RECURSE LIB_SOURCES src/*.cpp)
list(FILTER LIB_SOURCES EXCLUDE REGEX "src/main\\.cpp$")
list(FILTER LIB_SOURCES EXCLUDE REGEX "src/gui/")

add_library(minidragon_core STATIC ${LIB_SOURCES})
target_include_directories(minidragon_core PUBLIC src)
target_link_libraries(minidragon_core PUBLIC
  nlohmann_json::nlohmann_json httplib::httplib sqlite3)
# Disable brotli (not needed, reduces binary size)
target_compile_definitions(minidragon_core PUBLIC CPPHTTPLIB_NO_BROTLI)
if(USE_OPENSSL)
  target_link_libraries(minidragon_core PUBLIC OpenSSL::SSL OpenSSL::Crypto)
  target_compile_definitions(minidragon_core PUBLIC CPPHTTPLIB_OPENSSL_SUPPORT)
endif()
if(UNIX)
  target_link_libraries(minidragon_core PUBLIC pthread)
endif()
if(WIN32)
  target_link_libraries(minidragon_core PUBLIC ws2_32 crypt32 winhttp)
endif()

# ── CLI executable ──────────────────────────────────────────────────
add_executable(minidragon src/main.cpp)
target_link_libraries(minidragon PRIVATE minidragon_core)
if(WIN32 AND NOT BUILD_GUI)
  # Static link for portable .exe
  target_link_options(minidragon PRIVATE -static)
  if(OPTIMIZE_SIZE)
    target_link_options(minidragon PRIVATE -Wl,--strip-all)
  endif()
endif()
if(UNIX AND OPTIMIZE_SIZE)
  target_link_options(minidragon PRIVATE -Wl,--strip-all)
endif()

# ── GUI executable (Mini Dragon) ────────────────────────────────────
option(BUILD_GUI "Build Mini Dragon GUI" OFF)
if(BUILD_GUI)
  add_subdirectory(src/gui)
endif()
