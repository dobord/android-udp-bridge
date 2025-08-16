cmake_minimum_required(VERSION 3.20)
project(udp_bridge_desktop_test C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

# Reuse udp2tcp from third_party
set(UDP2TCP_DIR ${CMAKE_SOURCE_DIR}/../third_party/udp2tcp)
if (NOT EXISTS ${UDP2TCP_DIR}/include/udp2tcp/c_api.h)
  message(FATAL_ERROR "udp2tcp submodule missing: ${UDP2TCP_DIR}")
endif()

# Options similar to Android build
set(UDP2TCP_ENABLE_C_API ON CACHE BOOL "" FORCE)
set(UDP2TCP_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(LIBCORO_FEATURE_NETWORKING ON CACHE BOOL "" FORCE)
set(LIBCORO_FEATURE_TLS ON CACHE BOOL "" FORCE)
set(LIBCORO_EXTERNAL_DEPENDENCIES OFF CACHE BOOL "" FORCE)

add_subdirectory(${UDP2TCP_DIR} ${CMAKE_BINARY_DIR}/third_party/udp2tcp EXCLUDE_FROM_ALL)

# Sources reused from Android app JNI (adapter)
set(APP_JNI_DIR ${CMAKE_SOURCE_DIR}/../ssh-tunnel-android-app/app/src/main/jni)
set(SHARED_SOURCES
  ${APP_JNI_DIR}/udp2tcp_client_adapter.cpp
  ${APP_JNI_DIR}/ssh_tunnel.c
)
include_directories(${UDP2TCP_DIR}/include ${APP_JNI_DIR})

add_executable(udp-bridge-cli
  src/main.cpp
  ${SHARED_SOURCES}
)

# Link udp2tcp
if (TARGET udp2tcp)
  target_link_libraries(udp-bridge-cli PRIVATE udp2tcp pthread)
else()
  message(FATAL_ERROR "udp2tcp target not found")
endif()

# Find libssh (system)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBSSH REQUIRED libssh)
if (LIBSSH_FOUND)
  target_include_directories(udp-bridge-cli PRIVATE ${LIBSSH_INCLUDE_DIRS})
  target_link_libraries(udp-bridge-cli PRIVATE ${LIBSSH_LIBRARIES})
else()
  message(FATAL_ERROR "libssh not found; install libssh-dev")
endif()

# Define macro to enable desktop code paths in adapter (logs etc.)
add_definitions(-DSSHTUN_DESKTOP=1 -DUSE_UDP2TCP=1)

# Warnings
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(udp-bridge-cli PRIVATE -Wall -Wextra)
endif()
