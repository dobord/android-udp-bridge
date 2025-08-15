cmake_minimum_required(VERSION 3.18.1)

project("sshtunnel")

# Опция использования новой схемы udp2tcp вместо legacy UDP Bridge
option(USE_UDP2TCP "Enable new udp2tcp adapter (excludes legacy udp_listener / protocol sources)" OFF)

# Устанавливаем пути для поиска библиотек
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)

# Путь к установленной реальной libssh (заполняется build_openssl.sh)
set(LIBSSH_PREBUILT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../prebuilt/libssh)
set(OPENSSL_PREBUILT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../prebuilt/openssl)
set(MBEDTLS_PREBUILT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../prebuilt/mbedtls)

# Early expose OpenSSL include directory (if present) before pulling in udp2tcp/libcoro
# so that libcoro TLS sources (#include <openssl/...>) can compile when TLS feature is ON.
set(_OPENSSL_EARLY_INCLUDE_DIR ${OPENSSL_PREBUILT_DIR}/${ANDROID_ABI}/include)
if (EXISTS ${_OPENSSL_EARLY_INCLUDE_DIR}/openssl/ssl.h)
    message(STATUS "Early OpenSSL include detected at ${_OPENSSL_EARLY_INCLUDE_DIR}")
    include_directories(${_OPENSSL_EARLY_INCLUDE_DIR})

    # Provide imported OpenSSL targets BEFORE add_subdirectory(udp2tcp) so that any
    # find_package(OpenSSL) calls inside third_party code pick up our Android prebuilt
    # static libraries instead of host /usr/lib ones (which caused arch mismatch at link).
    set(_OPENSSL_PREBUILT_LIB_DIR ${OPENSSL_PREBUILT_DIR}/${ANDROID_ABI}/lib)
    # FORCE перезаписываем ранее закэшированные пути find_package(OpenSSL), если до этого конфиг уже находил host /usr/lib/*.so
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

# Создаем нашу основную библиотеку
if (USE_UDP2TCP)
    message(STATUS "Building with USE_UDP2TCP=ON (legacy UDP bridge sources excluded)")
    add_library(ssh_tunnel SHARED
        ../jni/ssh_tunnel.c
        ../jni/udp2tcp_client_adapter.cpp
    )
    target_compile_definitions(ssh_tunnel PRIVATE USE_UDP2TCP=1)
    # Подключаем include для заголовков udp2tcp (требует импорт кода в third_party/udp2tcp)
    # Из папки app/src/main/cpp до корня репозитория нужно подняться на 5 уровней (cpp -> main -> src -> app -> ssh-tunnel-android-app -> repo root)
    # Предыдущее значение имело только 4 «..» и указывало внутрь ssh-tunnel-android-app/third_party (которого нет), потому заголовки не находились.
    set(UDP2TCP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../third_party/udp2tcp)
    message(STATUS "Resolved UDP2TCP_DIR=${UDP2TCP_DIR}")
    if (EXISTS ${UDP2TCP_DIR}/include/udp2tcp/c_api.h)
    target_include_directories(ssh_tunnel PRIVATE ${UDP2TCP_DIR}/include)
    else()
        message(WARNING "udp2tcp headers not found under ${UDP2TCP_DIR}/include — убедитесь, что репозиторий импортирован.\n        См. docs/MIGRATION_UDP2TCP.md")
    endif()
    # Включаем C API и отключаем тесты/примерные цели udp2tcp для Android сборки
    if (EXISTS ${UDP2TCP_DIR}/CMakeLists.txt)
        set(UDP2TCP_ENABLE_C_API ON CACHE BOOL "" FORCE)
        set(UDP2TCP_BUILD_SHARED OFF CACHE BOOL "" FORCE)
        set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
        # Включаем полноценные возможности libcoro (networking + TLS)
        set(LIBCORO_FEATURE_NETWORKING ON CACHE BOOL "" FORCE)
        set(LIBCORO_FEATURE_TLS ON CACHE BOOL "" FORCE)
    # libcoro будет пропускать find_package(OpenSSL) после нашего патча (guard по существующим TARGET'ам),
    # поэтому просто оставляем встроенные зависимости (c-ares vendored) и не включаем EXTERNAL_DEPENDENCIES.
    set(LIBCORO_EXTERNAL_DEPENDENCIES OFF CACHE BOOL "" FORCE)
    # (Removed shim include for <stop_token>; NDK 29 provides standard header)
        add_subdirectory(${UDP2TCP_DIR} ${CMAKE_BINARY_DIR}/third_party/udp2tcp EXCLUDE_FROM_ALL)
        # Пытаемся скомпилировать простой тест с <stop_token>; если не удаётся, не критично — просто подключим shim.
        include(CheckCXXSourceCompiles)
        set(CMAKE_REQUIRED_FLAGS "-std=c++20")
        check_cxx_source_compiles("#include <stop_token>\nint main(){return 0;}" HAVE_STD_STOP_TOKEN)
        if (NOT HAVE_STD_STOP_TOKEN)
            message(STATUS "std::stop_token отсутствует в NDK")
        endif()
        if (TARGET udp2tcp)
            target_link_libraries(ssh_tunnel PRIVATE udp2tcp)
        else()
            message(WARNING "udp2tcp target not created; adapter symbols могут не линковаться")
        endif()
    else()
        message(WARNING "Не найден CMakeLists udp2tcp; пропускаем встроенную сборку")
    endif()
else()
    add_library(ssh_tunnel SHARED
        ../jni/ssh_tunnel.c
        ../jni/udp_listener.c
        ../jni/tcp_connection_manager.c
        ../jni/client_manager.c
        ../jni/udp_bridge_protocol.c
        ../jni/udp_bridge_service_jni.c
        ../jni/udp2tcp_client_adapter.cpp  # оставляем адаптер для JNI методов, но не активируем через флаг
    )
endif()

# Импортируем реальную или моковую libssh в зависимости от наличия пребиилдов
set(LIBSSH_STATIC_LIB ${LIBSSH_PREBUILT_DIR}/${ANDROID_ABI}/lib/libssh.a)
set(OPENSSL_CRYPTO_LIB ${OPENSSL_PREBUILT_DIR}/${ANDROID_ABI}/lib/libcrypto.a)
set(OPENSSL_SSL_LIB ${OPENSSL_PREBUILT_DIR}/${ANDROID_ABI}/lib/libssl.a)

# Проверяем наличие OpenSSL сборки (новый способ)
if (EXISTS ${LIBSSH_STATIC_LIB} AND EXISTS ${OPENSSL_CRYPTO_LIB} AND EXISTS ${OPENSSL_SSL_LIB})
    message(STATUS "Using prebuilt libssh with OpenSSL: ${LIBSSH_STATIC_LIB}")
    message(STATUS "OpenSSL crypto: ${OPENSSL_CRYPTO_LIB}")
    message(STATUS "OpenSSL ssl: ${OPENSSL_SSL_LIB}")
    
    target_include_directories(ssh_tunnel PRIVATE
        ${LIBSSH_PREBUILT_DIR}/${ANDROID_ABI}/include
        ${OPENSSL_PREBUILT_DIR}/${ANDROID_ABI}/include
    )

    add_library(ssh STATIC IMPORTED)
    set_target_properties(ssh PROPERTIES IMPORTED_LOCATION ${LIBSSH_STATIC_LIB})

    # Импортируем статические библиотеки OpenSSL
    add_library(crypto STATIC IMPORTED)
    add_library(ssl STATIC IMPORTED)
    set_target_properties(crypto PROPERTIES IMPORTED_LOCATION ${OPENSSL_CRYPTO_LIB})
    set_target_properties(ssl PROPERTIES IMPORTED_LOCATION ${OPENSSL_SSL_LIB})
    
    target_compile_definitions(ssh_tunnel PRIVATE USE_OPENSSL=1)
    
# Проверяем наличие mbedTLS сборки (старый способ для обратной совместимости)
elseif (EXISTS ${LIBSSH_STATIC_LIB})
    set(MBEDTLS_CRYPTO_LIB ${MBEDTLS_PREBUILT_DIR}/${ANDROID_ABI}/lib/libmbedcrypto.a)
    set(MBEDTLS_X509_LIB   ${MBEDTLS_PREBUILT_DIR}/${ANDROID_ABI}/lib/libmbedx509.a)
    set(MBEDTLS_TLS_LIB    ${MBEDTLS_PREBUILT_DIR}/${ANDROID_ABI}/lib/libmbedtls.a)
    
    if (EXISTS ${MBEDTLS_CRYPTO_LIB} AND EXISTS ${MBEDTLS_X509_LIB} AND EXISTS ${MBEDTLS_TLS_LIB})
        message(STATUS "Using prebuilt libssh with mbedTLS: ${LIBSSH_STATIC_LIB}")
        target_include_directories(ssh_tunnel PRIVATE
            ${LIBSSH_PREBUILT_DIR}/${ANDROID_ABI}/include
            ${MBEDTLS_PREBUILT_DIR}/${ANDROID_ABI}/include
        )

        add_library(ssh STATIC IMPORTED)
        set_target_properties(ssh PROPERTIES IMPORTED_LOCATION ${LIBSSH_STATIC_LIB})

        # Импортируем статические библиотеки mbedTLS
        add_library(mbedcrypto STATIC IMPORTED)
        add_library(mbedx509   STATIC IMPORTED)
        add_library(mbedtls    STATIC IMPORTED)
        set_target_properties(mbedcrypto PROPERTIES IMPORTED_LOCATION ${MBEDTLS_CRYPTO_LIB})
        set_target_properties(mbedx509   PROPERTIES IMPORTED_LOCATION ${MBEDTLS_X509_LIB})
        set_target_properties(mbedtls    PROPERTIES IMPORTED_LOCATION ${MBEDTLS_TLS_LIB})
        
        target_compile_definitions(ssh_tunnel PRIVATE USE_MBEDTLS=1)
    else()
        message(WARNING "libssh found but crypto libraries missing. Falling back to mock implementation.")
        add_library(ssh STATIC ../jni/libssh_mock.c)
        target_include_directories(ssh_tunnel PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../jni)
        target_compile_definitions(ssh_tunnel PRIVATE USE_LIBSSH_MOCK=1)
    endif()
else()
    message(WARNING "Prebuilt libssh not found at ${LIBSSH_STATIC_LIB}. Falling back to mock implementation.")
    add_library(ssh STATIC ../jni/libssh_mock.c)
    target_include_directories(ssh_tunnel PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../jni)
    target_compile_definitions(ssh_tunnel PRIVATE USE_LIBSSH_MOCK=1)
endif()

# Находим системные библиотеки
find_library(log-lib log)

# Связываем библиотеки в зависимости от того, что доступно
if (TARGET ssl AND TARGET crypto)
    # OpenSSL сборка
    target_link_libraries(ssh_tunnel PRIVATE ssh ssl crypto ${log-lib})
elseif (TARGET mbedtls AND TARGET mbedx509 AND TARGET mbedcrypto)
    # mbedTLS сборка
    target_link_libraries(ssh_tunnel PRIVATE ssh mbedtls mbedx509 mbedcrypto ${log-lib})
else()
    # Mock сборка
    target_link_libraries(ssh_tunnel PRIVATE ssh ${log-lib})
endif()

# Общие флаги компиляции
target_compile_options(ssh_tunnel PRIVATE -Wall -Wextra)

# Для совместимости с устройствами Android 15 с размером страницы 16K
target_link_options(ssh_tunnel PRIVATE -Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384)