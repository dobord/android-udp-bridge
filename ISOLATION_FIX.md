# Исправление проблемы изолированности в CI/CD

## Описание проблемы

При параллельной сборке OpenSSL и libssh для разных архитектур Android возникала проблема race conditions:

1. **Общие исходники**: Все раннеры матрицы использовали один каталог для исходников
2. **Конфликт скачивания**: Несколько процессов одновременно скачивали файлы в одно место  
3. **Пересечение директорий**: Копирование исходников могло конфликтовать

## Решение

### 1. Изолированные рабочие директории

Каждая архитектура теперь использует собственную рабочую директорию:

```bash
# В workflow
export ARCH_WORK_DIR="$(pwd)/build_${{ matrix.arch }}"

# В скрипте сборки
if [ -n "$ARCH_WORK_DIR" ]; then
    WORK_DIR="$ARCH_WORK_DIR/openssl_build"
else
    WORK_DIR="$(pwd)/openssl_build"
fi
```

### 2. Предварительное скачивание

Исходники скачиваются один раз в общий кеш:

```yaml
- name: Pre-download sources to avoid conflicts
  run: |
    mkdir -p /tmp/source_cache
    wget -O "/tmp/source_cache/openssl-3.5.0.tar.gz" "..."
    wget "/tmp/source_cache/libssh-0.11.2.tar.xz" "..."
```

### 3. Блокировки файлов

При необходимости используются блокировки с помощью `flock`:

```bash
LOCK_FILE="/tmp/openssl_download_${ANDROID_ABI}.lock"
(
    flock -x -w 300 200
    # Скачивание с блокировкой
) 200>"$LOCK_FILE"
```

### 4. Раздельная загрузка артефактов

Каждая архитектура загружает только свои файлы:

```yaml
- name: Upload prebuilt libraries
  with:
    name: prebuilt-libs-${{ matrix.arch }}
    path: |
      ssh-tunnel-android-app/app/src/main/prebuilt/openssl/${{ matrix.arch }}/
      ssh-tunnel-android-app/app/src/main/prebuilt/libssh/${{ matrix.arch }}/
```

## Результат

- ✅ Полная изоляция между архитектурами  
- ✅ Отсутствие race conditions
- ✅ Надёжная параллельная сборка
- ✅ Быстрая сборка без конфликтов

## Тестирование

Используйте скрипт для проверки изолированности:

```bash
./test_isolation.sh
```
