cmake_minimum_required(VERSION 3.18.1)

project("sshtunnel")

# Устанавливаем пути для поиска библиотек
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)

# Путь к установленной реальной libssh (заполняется build_libssh.sh)
set(LIBSSH_PREBUILT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../prebuilt/libssh)
set(MBEDTLS_PREBUILT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../prebuilt/mbedtls)

# Создаем нашу основную библиотеку
add_library(ssh_tunnel SHARED
    ../jni/ssh_tunnel.c
)

# Подключаем заголовки реальной libssh для текущего ABI
# Импортируем реальную или моковую libssh в зависимости от наличия пребиилдов
set(LIBSSH_STATIC_LIB ${LIBSSH_PREBUILT_DIR}/${ANDROID_ABI}/lib/libssh.a)
if (EXISTS ${LIBSSH_STATIC_LIB})
    message(STATUS "Using prebuilt libssh: ${LIBSSH_STATIC_LIB}")
    target_include_directories(ssh_tunnel PRIVATE
        ${LIBSSH_PREBUILT_DIR}/${ANDROID_ABI}/include
    )

    add_library(ssh STATIC IMPORTED)
    set_target_properties(ssh PROPERTIES IMPORTED_LOCATION ${LIBSSH_STATIC_LIB})

    # Импортируем статические библиотеки mbedTLS (зависимости libssh при сборке с WITH_MBEDTLS)
    set(MBEDTLS_CRYPTO_LIB ${MBEDTLS_PREBUILT_DIR}/${ANDROID_ABI}/lib/libmbedcrypto.a)
    set(MBEDTLS_X509_LIB   ${MBEDTLS_PREBUILT_DIR}/${ANDROID_ABI}/lib/libmbedx509.a)
    set(MBEDTLS_TLS_LIB    ${MBEDTLS_PREBUILT_DIR}/${ANDROID_ABI}/lib/libmbedtls.a)
    add_library(mbedcrypto STATIC IMPORTED)
    add_library(mbedx509   STATIC IMPORTED)
    add_library(mbedtls    STATIC IMPORTED)
    set_target_properties(mbedcrypto PROPERTIES IMPORTED_LOCATION ${MBEDTLS_CRYPTO_LIB})
    set_target_properties(mbedx509   PROPERTIES IMPORTED_LOCATION ${MBEDTLS_X509_LIB})
    set_target_properties(mbedtls    PROPERTIES IMPORTED_LOCATION ${MBEDTLS_TLS_LIB})
else()
    message(WARNING "Prebuilt libssh not found at ${LIBSSH_STATIC_LIB}. Falling back to mock implementation.")
    add_library(ssh STATIC ../jni/libssh_mock.c)
    target_include_directories(ssh_tunnel PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../jni)
    target_compile_definitions(ssh_tunnel PRIVATE USE_LIBSSH_MOCK=1)
endif()

# Находим системные библиотеки
find_library(log-lib log)

# Связываем библиотеки: реальная libssh и лог
if (TARGET mbedtls AND TARGET mbedx509 AND TARGET mbedcrypto)
    target_link_libraries(ssh_tunnel
        ssh
        mbedtls
        mbedx509
        mbedcrypto
        ${log-lib}
    )
else()
    target_link_libraries(ssh_tunnel
        ssh
        ${log-lib}
    )
endif()

# Общие флаги компиляции
target_compile_options(ssh_tunnel PRIVATE -Wall -Wextra)

# Для совместимости с устройствами Android 15 с размером страницы 16K
target_link_options(ssh_tunnel PRIVATE -Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384)