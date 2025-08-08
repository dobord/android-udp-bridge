# SSH Key Authentication Support

## Обзор

В Android UDP Bridge приложение добавлена поддержка аутентификации по SSH ключам в дополнение к существующей аутентификации по паролю.

## Методы аутентификации

### 1. Аутентификация по паролю (существующий метод)
- Простая аутентификация с использованием логина и пароля
- Подходит для быстрого тестирования и простых случаев использования

### 2. Аутентификация по приватному ключу (новый метод)
- Более безопасный метод аутентификации
- Поддерживает ключи с парольной фразой и без неё
- Рекомендуется для продакшн использования

## Интерфейс пользователя

### Выбор метода аутентификации
- Radio buttons для переключения между методами:
  - "Password" - аутентификация по паролю
  - "Private Key" - аутентификация по ключу

### Поля ввода для аутентификации по ключу
- **Private Key Path**: путь к файлу приватного ключа на устройстве
- **Key Passphrase**: парольная фраза для защищённого ключа (опционально)

## Технические детали

### JNI методы
```java
// Подключение с паролем
public native boolean connectToServer(String host, int port, String username, String password);

// Подключение с приватным ключом
public native boolean connectWithKey(String host, int port, String username, String privateKeyPath, String passphrase);
```

### Native функции C
```c
// Аутентификация по ключу
int ssh_userauth_publickey_auto(ssh_session session, const char *username, const char *passphrase);
int ssh_userauth_publickey(ssh_session session, const char *username, const ssh_key privkey);
int ssh_pki_import_privkey_file(const char *filename, const char *passphrase, void *auth_fn, void *auth_data, ssh_key *pkey);
void ssh_key_free(ssh_key key);
```

## Поддерживаемые форматы ключей

В текущей реализации с stub библиотекой поддерживаются любые файлы ключей (симуляция).
При интеграции с реальной libssh будут поддерживаться:
- RSA ключи
- DSA ключи 
- ECDSA ключи
- Ed25519 ключи
- OpenSSH формат
- PEM формат

## Использование

### Настройка подключения с ключом
1. Выберите "Private Key" в разделе Authentication Method
2. Введите путь к приватному ключу (например: /sdcard/ssh_keys/id_rsa)
3. При необходимости введите парольную фразу
4. Нажмите "Connect"

### Пример путей к ключам на Android
- `/sdcard/ssh_keys/id_rsa` - внешнее хранилище
- `/data/data/com.example.sshtunnel/files/keys/id_rsa` - внутреннее хранилище приложения
- `/storage/emulated/0/Download/my_key` - папка загрузок

## Безопасность

### Рекомендации по безопасности
- Используйте ключи с парольными фразами
- Храните ключи в защищённом хранилище приложения
- Не оставляйте ключи в общедоступных папках
- Регулярно обновляйте ключи

### Права доступа к файлам
Приложению требуются права на чтение файлов для загрузки приватных ключей:
```xml
<uses-permission android:name="android.permission.READ_EXTERNAL_STORAGE" />
```

## Отладка

### Логирование аутентификации
Все операции аутентификации логируются с тегом "SSHTunnel":
```
I/SSHTunnel: Attempting to connect to server:22 with user username using key /path/to/key
I/SSHTunnel: SSH key authentication successful
```

### Возможные ошибки
- "SSH key authentication failed" - неверный ключ или парольная фраза
- "Failed to create SSH session" - проблемы с подключением
- "Please specify private key path" - не указан путь к ключу

## Будущие улучшения

1. **Интеграция с Android Keystore** - безопасное хранение ключей
2. **Генерация ключей в приложении** - создание новых ключей
3. **Поддержка SSH-Agent** - использование системного агента
4. **Импорт ключей из файлов** - UI для выбора файлов ключей
5. **Сертификаты SSH** - поддержка certificate-based аутентификации
