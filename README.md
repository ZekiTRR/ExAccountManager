# ExAccountManager

Мега-менеджер аккаунтов Roblox, объединяющий лучшие функции трёх менеджеров
(RAM / ic3w0lf22, RM / Rust, Astro) плюс уникальные возможности:
лимиты ОЗУ, произвольные размеры окон клиентов, массовый запуск на разные
сервера/один сервер/VIP, выборочный запуск на VIP по ссылкам, вход по
логину-паролю (CDP-браузер) и по cookie, аналитика времени фарма.

## Стек

- **C++20**, **Qt 6** (Widgets, Network, Test)
- **SQLite** (хранение), **libsodium** (AES-256-GCM + Argon2id)
- Windows 10/11 x64; для auth-ticket используется системный `curl.exe`
  (Schannel) — Qt-клиент получает 401 на CSRF-retry Roblox; без curl
  остаётся Qt-fallback.

## Сборка

Требуется: Visual Studio 2022 (MSVC), CMake ≥ 3.24, Qt 6.5+ (msvc_64),
Ninja. Пути к VS/Qt вшиты в `scripts/env.bat` — поправь при необходимости.

```bat
scripts\env.bat
cmake --preset release
cmake --build --preset release
build\release\ExAccountManager.exe
```

После сборки один раз: `windeployqt --release build\release\ExAccountManager.exe`
(DLL уже задеплоены в текущей сборке — exe запускается двойным кликом).

Тесты: `build\release\ex_tests.exe` (13 тестов: крипто, парсер, URI, WS-кадры, store, ledger, лимиты).

Диагностика хранилища секретов: `build\release\vault_check.exe [пароль]`.

## Текущий статус (M0–M2 + CDP-вход)

**Готово:**
- Хранение: SQLite + envelope-шифрование AES-256-GCM (device-ключ в
  Credential Manager), атомарные записи, бэкапы, самолечение vault
  (авто-пересоздание при пустом хранилище).
- Аккаунты: добавление по cookie (валидация до сохранения, Qt + curl
  fallback), bulk-импорт (user:pass / user:pass:cookie / cookie / файлы),
  группы, presence, аватары, ревалидация, бан-детектор.
- **Вход по логину и паролю через CDP**: находит Edge/Chrome, запускает с
  временным профилем, автозаполняет форму (React-safe), перехватывает
  HttpOnly cookie .ROBLOSECURITY, сохраняет пароль в vault для ре-логина.
  Bulk-вход: по 2 окна сеткой. Если приложение запущено от администратора,
  браузер запускается с понижением прав (токен explorer) — Chrome 136+
  отказывается от remote debugging в повышенном контексте.
- Запуск: roblox-player URI, CSRF, auth-ticket, share-ссылки, режимы
  «один сервер / разные сервера / VIP», без FPS-лимита (legacy-ключ снимается).
- Инстансы: атрибуция PID→аккаунт (launchtime-токен + чтение PEB),
  kill-verified, тримминг ОЗУ, жёсткий лимит рабочего набора (RAM) без
  Job Object, CPU через Job Object с мягким fallback (приоритет/EcoQoS),
  принудительный размер окон (SWP_NOSENDCHANGING), позиция/скрытие/сетка.
- Мультиинстанс: владение ROBLOX_singletonMutex (очередь, adopt).
- Мониторинг: CPU/RAM по процессам, ledger сессий (время фарма за сегодня).
- UI: тёмная/светлая темы, tray, single-instance, RU; крестик полностью закрывает приложение.

**Дальше (M3–M6):** watcher логов + auto-rejoin, макросы DSL + планировщик,
RAM-лимиты как профили в UI, window-профили в UI, HTTP API (RAM-совместимый),
Nexus, Discord RPC, вебхуки, Asset Manager, OAuth, миграция с RAM.

## Архитектура

```
src/
  core/       доменная логика (QtCore/QtNetwork, без Widgets)
    util/     логирование со скраббингом секретов, пути, атомарный FS
    models/   Account, Group, Preset, LaunchTarget, WindowProfile, RamLimit
    crypto/   envelope-шифрование AES-256-GCM (device/пароль режимы)
    net/      WebSocket-клиент RFC 6455 (для CDP)
    store/    SQLite (аккаунты, группы, пресеты, настройки, статистика)
    roblox/   API-клиент (CSRF, auth-ticket, presence, серверы),
              bulk-импорт, CDP-вход через Edge/Chrome
    launcher/ roblox-player URI, очереди запуска, ClientSettings (снятие FPS-капа)
    instances/ атрибуция PID→аккаунт, монитор с телеметрией
  platform/   Win32: процессы (PEB), окна, Job Objects, Credential Manager,
              мьютекс мультиинстанса, запуск с понижением прав
  ui/         Qt Widgets: главное окно, tray, темы, экраны
tests/        тесты (QtTest) — 14 шт.
tools/        vault_check (диагностика)
third_party/  sqlite3 (amalgamation), libsodium (prebuilt static)
```

## Принципы

1. **Секреты никогда не лежат открытым текстом** — envelope-шифрование,
   ключ в Windows Credential Manager (device-режим) или Argon2id (пароль).
2. **Скраббинг секретов** на уровне логгера (backstop-фильтр).
3. **Атомарные записи** всего on-disk состояния + самолечение из бэкапов.
4. **Никогда не убивать чужой процесс** без тройной верификации
   (имя образа + launchtime-токен + create_time).
5. **План ≠ действие**: планировщики — чистые данные, исполняются сервисом.
