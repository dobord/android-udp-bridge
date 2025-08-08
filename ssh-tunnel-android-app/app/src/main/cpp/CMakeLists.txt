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
target_include_directories(ssh_tunnel PRIVATE
    ${LIBSSH_PREBUILT_DIR}/${ANDROID_ABI}/include
)

# Импортируем статическую библиотеку libssh для текущего ABI
set(LIBSSH_STATIC_LIB ${LIBSSH_PREBUILT_DIR}/${ANDROID_ABI}/lib/libssh.a)
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

# Находим системные библиотеки
find_library(log-lib log)

# Связываем библиотеки: реальная libssh и лог
target_link_libraries(ssh_tunnel
    ssh
    mbedtls
    mbedx509
    mbedcrypto
    ${log-lib}
)

# Общие флаги компиляции
target_compile_options(ssh_tunnel PRIVATE -Wall -Wextra)