cmake_minimum_required(VERSION 3.18.1)

project("sshtunnel")

# Legacy flag removed: udp2tcp path is now mandatory.
set(_AUTO_UDP2TCP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../third_party/udp2tcp)
set(_UDP2TCP_HEADER_PATH ${_AUTO_UDP2TCP_DIR}/include/udp2tcp/c_api.h)
if (NOT EXISTS ${_UDP2TCP_HEADER_PATH})
    message(FATAL_ERROR "udp2tcp headers not found at ${_UDP2TCP_HEADER_PATH}. Ensure submodule is fetched: git submodule update --init --recursive")
endif()

# Configure search path modes for libraries and includes (Android cross-compilation friendly)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)

# Path to installed prebuilt libraries (OpenSSL only; mbedTLS removed)
set(LIBSSH_PREBUILT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../prebuilt/libssh)
set(OPENSSL_PREBUILT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../prebuilt/openssl)

# Early expose OpenSSL include directory (if present) before pulling in udp2tcp/libcoro
# so that libcoro TLS sources (#include <openssl/...>) compile when TLS feature is ON.
set(_OPENSSL_EARLY_INCLUDE_DIR ${OPENSSL_PREBUILT_DIR}/${ANDROID_ABI}/include)
if (EXISTS ${_OPENSSL_EARLY_INCLUDE_DIR}/openssl/ssl.h)
    message(STATUS "Early OpenSSL include detected at ${_OPENSSL_EARLY_INCLUDE_DIR}")
    include_directories(${_OPENSSL_EARLY_INCLUDE_DIR})

    # Provide imported OpenSSL targets BEFORE add_subdirectory(udp2tcp) so that any
    # find_package(OpenSSL) calls inside third_party code pick up our Android prebuilt
    # static libraries instead of host /usr/lib ones (avoids architecture mismatch during link).
    set(_OPENSSL_PREBUILT_LIB_DIR ${OPENSSL_PREBUILT_DIR}/${ANDROID_ABI}/lib)
    # FORCE override any previously cached host OpenSSL paths (in case CMake found /usr/lib/*.so earlier)
    set(OPENSSL_ROOT_DIR ${OPENSSL_PREBUILT_DIR}/${ANDROID_ABI} CACHE PATH "" FORCE)
    set(OPENSSL_INCLUDE_DIR ${_OPENSSL_EARLY_INCLUDE_DIR} CACHE PATH "" FORCE)
    unset(OPENSSL_SSL_LIBRARY CACHE)
    unset(OPENSSL_CRYPTO_LIBRARY CACHE)
    unset(OPENSSL_LIBRARIES CACHE)
    set(OPENSSL_SSL_LIBRARY ${_OPENSSL_PREBUILT_LIB_DIR}/libssl.a CACHE FILEPATH "" FORCE)
    set(OPENSSL_CRYPTO_LIBRARY ${_OPENSSL_PREBUILT_LIB_DIR}/libcrypto.a CACHE FILEPATH "" FORCE)
    set(OPENSSL_LIBRARIES "${OPENSSL_SSL_LIBRARY};${OPENSSL_CRYPTO_LIBRARY}" CACHE STRING "" FORCE)
    set(OPENSSL_FOUND TRUE CACHE BOOL "" FORCE)
    message(STATUS "Patched OpenSSL cache vars:\n  OPENSSL_SSL_LIBRARY=${OPENSSL_SSL_LIBRARY}\n  OPENSSL_CRYPTO_LIBRARY=${OPENSSL_CRYPTO_LIBRARY}")
    # Wipe any pkg-config discovered host libs (pkgcfg_lib__OPENSSL_*) and force them to our static archives
    unset(pkgcfg_lib__OPENSSL_ssl CACHE)
    unset(pkgcfg_lib__OPENSSL_crypto CACHE)
    set(pkgcfg_lib__OPENSSL_ssl ${OPENSSL_SSL_LIBRARY} CACHE FILEPATH "" FORCE)
    set(pkgcfg_lib__OPENSSL_crypto ${OPENSSL_CRYPTO_LIBRARY} CACHE FILEPATH "" FORCE)
    message(STATUS "Overrode pkgcfg_lib__OPENSSL_* to Android static archives")
    # Only define targets if they do not already exist (fresh build dir scenario expected).
    if (NOT TARGET OpenSSL::SSL AND EXISTS ${OPENSSL_SSL_LIBRARY})
        add_library(OpenSSL::SSL STATIC IMPORTED)
        set_target_properties(OpenSSL::SSL PROPERTIES IMPORTED_LOCATION ${OPENSSL_SSL_LIBRARY} INTERFACE_INCLUDE_DIRECTORIES ${OPENSSL_INCLUDE_DIR})
    endif()
    if (NOT TARGET OpenSSL::Crypto AND EXISTS ${OPENSSL_CRYPTO_LIBRARY})
        add_library(OpenSSL::Crypto STATIC IMPORTED)
        set_target_properties(OpenSSL::Crypto PROPERTIES IMPORTED_LOCATION ${OPENSSL_CRYPTO_LIBRARY} INTERFACE_INCLUDE_DIRECTORIES ${OPENSSL_INCLUDE_DIR})
    endif()
endif()

# Create the primary shared library (JNI) — only udp2tcp path remains
add_library(ssh_tunnel SHARED
    ../jni/ssh_tunnel.c
    ../jni/udp2tcp_client_adapter.cpp
)
add_definitions(-DUSE_UDP2TCP=1)

# Include directory for udp2tcp headers
set(UDP2TCP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../third_party/udp2tcp)
message(STATUS "Resolved UDP2TCP_DIR=${UDP2TCP_DIR}")
target_include_directories(ssh_tunnel PRIVATE ${UDP2TCP_DIR}/include)

# Enable C API and disable tests/example targets of udp2tcp for Android build
set(UDP2TCP_ENABLE_C_API ON CACHE BOOL "" FORCE)
set(UDP2TCP_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(LIBCORO_FEATURE_NETWORKING ON CACHE BOOL "" FORCE)
set(LIBCORO_FEATURE_TLS ON CACHE BOOL "" FORCE)
set(LIBCORO_EXTERNAL_DEPENDENCIES OFF CACHE BOOL "" FORCE)
add_subdirectory(${UDP2TCP_DIR} ${CMAKE_BINARY_DIR}/third_party/udp2tcp EXCLUDE_FROM_ALL)

include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_FLAGS "-std=c++20")
check_cxx_source_compiles("#include <stop_token>\nint main(){return 0;}" HAVE_STD_STOP_TOKEN)
if (NOT HAVE_STD_STOP_TOKEN)
    message(STATUS "std::stop_token not available in this NDK")
endif()
if (TARGET udp2tcp)
    target_link_libraries(ssh_tunnel PRIVATE udp2tcp)
else()
    message(FATAL_ERROR "udp2tcp target not created; build cannot continue")
endif()

# Import real or mock libssh depending on presence of prebuilt artifacts
set(LIBSSH_STATIC_LIB ${LIBSSH_PREBUILT_DIR}/${ANDROID_ABI}/lib/libssh.a)
set(OPENSSL_CRYPTO_LIB ${OPENSSL_PREBUILT_DIR}/${ANDROID_ABI}/lib/libcrypto.a)
set(OPENSSL_SSL_LIB ${OPENSSL_PREBUILT_DIR}/${ANDROID_ABI}/lib/libssl.a)

if (EXISTS ${LIBSSH_STATIC_LIB} AND EXISTS ${OPENSSL_CRYPTO_LIB} AND EXISTS ${OPENSSL_SSL_LIB})
    message(STATUS "Using prebuilt libssh (OpenSSL): ${LIBSSH_STATIC_LIB}")
    target_include_directories(ssh_tunnel PRIVATE
        ${LIBSSH_PREBUILT_DIR}/${ANDROID_ABI}/include
        ${OPENSSL_PREBUILT_DIR}/${ANDROID_ABI}/include
    )
    add_library(ssh STATIC IMPORTED)
    set_target_properties(ssh PROPERTIES IMPORTED_LOCATION ${LIBSSH_STATIC_LIB})
    add_library(crypto STATIC IMPORTED)
    add_library(ssl STATIC IMPORTED)
    set_target_properties(crypto PROPERTIES IMPORTED_LOCATION ${OPENSSL_CRYPTO_LIB})
    set_target_properties(ssl PROPERTIES IMPORTED_LOCATION ${OPENSSL_SSL_LIB})
    target_compile_definitions(ssh_tunnel PRIVATE USE_OPENSSL=1)
else()
    message(WARNING "Prebuilt libssh/OpenSSL not found; using mock libssh")
    add_library(ssh STATIC ../jni/libssh_mock.c)
    target_include_directories(ssh_tunnel PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../jni)
    target_compile_definitions(ssh_tunnel PRIVATE USE_LIBSSH_MOCK=1)
endif()

# Locate system libraries
find_library(log-lib log)

# Link libraries depending on which crypto backend is available
if (TARGET ssl AND TARGET crypto)
    target_link_libraries(ssh_tunnel PRIVATE ssh ssl crypto ${log-lib})
else()
    target_link_libraries(ssh_tunnel PRIVATE ssh ${log-lib})
endif()

# Common compile flags
target_compile_options(ssh_tunnel PRIVATE -Wall -Wextra)

# Ensure compatibility with Android 15 devices (16K page size)
target_link_options(ssh_tunnel PRIVATE -Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384)