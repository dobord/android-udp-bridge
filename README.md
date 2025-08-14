# android-udp-bridge

Android приложение для безопасного проброса UDP трафика через SSH при помощи инкапсуляции `udp2tcp`.

> Документация реструктурирована: актуальные файлы в `docs/`, исторические и отчёты — в `docs_legacy/`.

## Быстрый обзор

| Компонент | Статус | Описание |
|-----------|--------|----------|
| SSH (libssh + расширенный stub) | Stable | Аутентификация пароль / ключ, port forwarding |
| udp2tcp интеграция | In progress | Замена кастомного UDP Bridge протокола |
| Legacy UDP Bridge | Deprecated | Вынесен в `docs_legacy/`, будет удалён после завершения миграции |

## Структура документации

Актуально (`docs/`):
- Архитектура: [`docs/TECH_SPEC_NEW_ARCHITECTURE.md`](docs/TECH_SPEC_NEW_ARCHITECTURE.md)
- Миграция: [`docs/MIGRATION_UDP2TCP.md`](docs/MIGRATION_UDP2TCP.md)
- План реализации / миграции: [`docs/IMPLEMENTATION_PLAN.md`](docs/IMPLEMENTATION_PLAN.md)
- CI/CD: [`docs/CI_CD_SUMMARY.md`](docs/CI_CD_SUMMARY.md)
- SSH реализация: [`docs/SSH_IMPLEMENTATION.md`](docs/SSH_IMPLEMENTATION.md)
- Расширенная SSH библиотека: [`docs/ADVANCED_SSH_LIBRARY.md`](docs/ADVANCED_SSH_LIBRARY.md)
- Аутентификация ключами: [`docs/SSH_KEY_AUTHENTICATION.md`](docs/SSH_KEY_AUTHENTICATION.md)

Legacy / отчёты (`docs_legacy/` примеры):
- Протокол / схема: `docs_legacy/UDP_BRIDGE_SCHEMA.md`
- Этапы PHASE / TASK отчёты
- Исправления / фиксы: `ARCH_FLAGS_FIX*.md`, `ARCHITECTURE_FIX_REPORT.md`

## Текущее состояние миграции
- Упрощение JNI: удаление внутренних listener / client manager в пользу вызова `udp2tcp` клиента
- Port forwarding остаётся основой защищённого транспорта
- После стабилизации: чистка legacy исходников и CI обновления

Репозиторий `udp2tcp`: git@github.com:dobord/udp2tcp.git

## Описание

Данное приложение предоставляет возможность создания SSH туннеля для UDP трафика на Android устройствах. Основные возможности (актуальная архитектура udp2tcp):

- Подключение к SSH серверу с использованием библиотеки libssh
- Поддержка двух методов аутентификации: пароль и приватный ключ
- Проброс UDP портов через tcp-инкапсуляцию (`udp2tcp`)
- Безопасная передача UDP данных через зашифрованное SSH соединение (SSH forwarding)
- Простой пользовательский интерфейс для настройки соединения

## Технические особенности

### Текущее (udp2tcp)
- Инкапсуляция UDP поверх одного надежного TCP потока через библиотеку / клиент `udp2tcp`
- SSH port forwarding (локальный или динамический) сохраняется для шифрования TCP транспортного канала
- Простая логика без собственного бинарного заголовка и таблиц клиентов в Android слое
- Снижение объема кода в JNI (устранение `udp_listener.*`, `udp_bridge_protocol.*`, client manager — будут удалены/упразднены поэтапно)
- Повторное использование проверенного кода `udp2tcp` вместо поддержки кастомного протокола

### Legacy (кастомный UDP Bridge протокол)
- Собственный заголовок (magic, version, crc32)
- Таблица клиентов и мультиплексирование MSG_DATA / MSG_PING / MSG_PONG
- Будет переведено в раздел «Legacy» и удалено после завершения миграции (см. `MIGRATION_UDP2TCP.md`)

## Использование

### Аутентификация по паролю
1. Введите данные SSH сервера (хост, порт, имя пользователя)
2. Выберите "Password" в разделе Authentication Method
3. Введите пароль
4. Нажмите "Connect"

### Аутентификация по приватному ключу
1. Введите данные SSH сервера (хост, порт, имя пользователя)
2. Выберите "Private Key" в разделе Authentication Method
3. Укажите путь к файлу приватного ключа на устройстве
4. При необходимости введите парольную фразу для ключа
5. Нажмите "Connect"

### Настройка UDP туннеля (udp2tcp)
1. После успешного подключения к SSH серверу
2. Введите локальный UDP порт (клиенты будут отправлять на него)
3. Укажите удалённый (target) UDP хост/порт (или используйте сохранённую конфигурацию)
4. Приложение установит/переиспользует SSH TCP порт‑форвардинг к порту сервера `udp2tcp`
5. Жмите "Start Forwarding" — локальный udp2tcp клиент связывается с удалённым udp2tcp сервером через SSH туннель

### Legacy режим (если включён в настройках разработчика)
Старый протокол запускает внутренний UDP listener + TCP connection manager. Он будет удалён после стабилизации udp2tcp.

## Документация (короткий индекс)
См. раздел "Структура документации" выше. Все новые материалы помещаются только в `docs/`.

## CI/CD и Автоматизация

Проект включает полную настройку CI/CD с помощью GitHub Actions:

### 🚀 Автоматические сборки
- **Pull Request Check** - автоматическая проверка кода и быстрая сборка при создании PR
- **Main Build** - полная сборка при push в основные ветки
- **Nightly Build** - ежедневные сборки для тестирования последних изменений
- **Release Build** - автоматическое создание релизов при создании тегов

### 📦 Релизы
Для создания нового релиза:
```bash
# Создать и отправить тег
git tag -a v1.0.0 -m "Release version 1.0.0"
git push origin v1.0.0
```

Это автоматически запустит:
- Сборку для всех архитектур (ARM64, ARMv7, x86, x86_64)
- Создание подписанных APK файлов
- Публикацию релиза на GitHub с changelog
- Генерацию SHA256 чексумм

### 🔧 Локальное тестирование
```bash
# Тестирование workflows локально (требует act)
./test-workflows.sh validate      # Проверка синтаксиса
./test-workflows.sh test-pr       # Тест PR workflow
./test-workflows.sh setup         # Настройка тестового окружения
```

Подробная документация: [.github/README.md](.github/README.md)

## Сборка и тестирование

### Требования
- Android NDK 25.1.8937393+
- CMake 3.18.1+
- Gradle 8.4+
- Java 21

### Prebuilt библиотеки (libssh + TLS backend)

Проект использует предварительно скомпилированные статические библиотеки (prebuilt) для ускорения сборки:

**Доступные библиотеки:**
- OpenSSL 3.5.0 + libssh 0.11.2
- mbedTLS 2.28.7 + libssh 0.11.2

**Архитектуры:** arm64-v8a, armeabi-v7a, x86_64, x86

**Команды сборки prebuilt:**
```bash
# Сборка всех архитектур (по умолчанию)
./build_openssl.sh                    # OpenSSL + libssh
./build_mbedtls.sh                    # mbedTLS + libssh

# Сборка конкретной архитектуры (для CI/CD)
ANDROID_ABI=arm64-v8a ./build_openssl.sh
ANDROID_ABI=x86_64 ./build_mbedtls.sh

# Пустая переменная = все архитектуры
ANDROID_ABI= ./build_openssl.sh
```

**Логика выбора архитектур:**
- Без `ANDROID_ABI` или `ANDROID_ABI=""` → собираются **все ABI**
- `ANDROID_ABI=конкретная_ABI` → собирается **только указанная ABI**

### Команды сборки
```bash
# Сборка проекта
cd ssh-tunnel-android-app
./gradlew build

# Запуск тестов
./test_advanced_ssh.sh
```

#### Windows (PowerShell)
```powershell
# 1) (Опционально) Убедитесь, что Android SDK доступен
# Если переменная ANDROID_HOME не задана, пропишите путь в local.properties:
#   ssh-tunnel-android-app\local.properties -> sdk.dir=E:\Android\Sdk

# 2) Собрать и установить prebuilt-библиотеки (mbedTLS + libssh)
cd E:\projects\android-udp-bridge
./build_mbedtls.bat

# 3) Собрать APK
./build_app.bat

# 4) (Опционально) Установить APK на устройство
adb install -r .\ssh-tunnel-android-app\app\build\outputs\apk\debug\app-debug.apk

# 5) (Опционально) Смотреть логи
adb logcat | Select-String -Pattern "(SSHTunnel|LibSSH_Advanced)"
```

### Установка на устройство
```bash
# Установка APK
adb install -r $PWD/ssh-tunnel-android-app/app/build/outputs/apk/debug/app-debug.apk

# Мониторинг логов
adb logcat | grep -E "(SSHTunnel|LibSSH_Advanced|udp2tcp)"

## FAQ (кратко)
Q: Где теперь искать старые отчёты PHASE / TASK?  
A: В каталоге `docs_legacy/`.

Q: Когда удалится legacy код?  
A: После успешных e2e тестов udp2tcp и обновления CI (см. MIGRATION документ).

Q: Нужно ли что-то менять в сборке при переходе?  
A: Нет, сборочные скрипты остаются прежними; добавится включение udp2tcp клиента.

## Лицензия
MIT (если не указано иначе в отдельных third_party папках).
```