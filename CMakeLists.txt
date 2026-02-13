cmake_minimum_required(VERSION 3.20)
project(minidragon LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

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

# cpp-httplib
FetchContent_Declare(httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG v0.15.3
)
FetchContent_MakeAvailable(httplib)

# sqlite3 amalgamation
FetchContent_Declare(sqlite3
  URL https://www.sqlite.org/2024/sqlite-amalgamation-3450000.zip
)
FetchContent_GetProperties(sqlite3)
if(NOT sqlite3_POPULATED)
  FetchContent_Populate(sqlite3)
  add_library(sqlite3 STATIC ${sqlite3_SOURCE_DIR}/sqlite3.c)
  target_include_directories(sqlite3 PUBLIC ${sqlite3_SOURCE_DIR})
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
endif()

# ── GUI executable (Mini Dragon) ────────────────────────────────────
option(BUILD_GUI "Build Mini Dragon GUI" OFF)
if(BUILD_GUI)
  add_subdirectory(src/gui)
endif()
