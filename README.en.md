# ExAccountManager

[Русская версия](README.md)

A Windows multi-account manager for Roblox that merges the best parts of three
existing managers (RAM / RM / Astro): cookie and browser login, bulk launch across
servers and VIP servers, RAM/CPU limits, arbitrary window sizes, per-process
attribution and farming analytics. The UI is Russian.

![Accounts](docs/images/accounts-dark.png)

## Features

| Area | What it does |
| --- | --- |
| Accounts | cookie added with validation before saving, bulk import (`user:pass[:cookie]`, cookie, file), groups, presence, avatars, revalidation, ban detection |
| Browser login | Edge/Chrome over CDP: form autofill, HttpOnly `.ROBLOSECURITY` capture, password kept in the vault for re-login, two windows in a grid |
| Launch | `roblox-player` URI, auth ticket, CSRF, share links; "one server / different servers / VIP" modes; pick a specific `RobloxPlayerBeta.exe` instead of the Windows handler |
| Instances | PID→account attribution (launchtime token + PEB read), kill-verified actions, hide/focus/grid, forced window size, "Multi Roblox" option (the manager holds `ROBLOX_singletonMutex`) |
| Limits | hard working-set cap (RAM) without a Job Object; CPU via Job Object, falling back to priority + EcoQoS |
| Analytics | CPU/RAM per process, session ledger, play time for today / 7 / 30 days |
| Storage | SQLite + AES-256-GCM envelope (libsodium), key in Credential Manager or Argon2id from a password, atomic writes and vault self-repair |

## Screens

| Analytics | Settings: Roblox client |
| --- | --- |
| ![Analytics: play time per account and hourly activity](docs/images/analytics-dark.png) | ![Settings: choose the installed Roblox client](docs/images/settings-roblox.png) |

| Light theme | Add accounts |
| --- | --- |
| ![Accounts screen in the light theme](docs/images/accounts-light.png) | ![Add accounts dialog: cookie, bulk import, login and password](docs/images/add-account.png) |

## Build

Requires Visual Studio 2022 (MSVC), CMake ≥ 3.24, Ninja and Qt 6.5+ (msvc2022_64).
VS/Qt paths live in `scripts/env.bat` — adjust them for your machine.

```bat
scripts\env.bat
cmake --preset release
cmake --build --preset release
build\release\ExAccountManager.exe
```

- Tests: `build\release\ex_tests.exe` — 15 tests (crypto, bulk parser, launch URI,
  WebSocket frames, store, ledger, limits, HTTP-cookie isolation, Roblox install lookup).
- Secret-storage diagnostics: `build\release\vault_check.exe [password]`.
- Portable mode: a `portable` marker file next to the exe keeps data in `data/`,
  otherwise `%APPDATA%\ExAccountManager` is used.
- Auth tickets go through the system `curl.exe` (Schannel) because the Qt client gets
  401 on the CSRF retry; without curl the Qt fallback still works.

## Layout

```
src/core/      domain, no Widgets: models, store (SQLite), crypto (envelope),
               roblox (API, bulk import, CDP login), launcher (URI, queues),
               instances (PID attribution), net (WebSocket), util (scrubbed logging)
src/platform/  Win32: processes (PEB read), windows, Job Objects, DPAPI/Credential Manager,
               multi-instance mutex, de-elevated launch
src/ui/        Qt Widgets: Material 3 themes, nav rail, screens, dialogs
tests/ tools/  15 QtTest cases + vault_check
third_party/   sqlite3 (amalgamation), libsodium (prebuilt static)
```

## Principles

1. Secrets are never stored in plain text and never reach the logs: envelope
   encryption plus scrubbing at the sink level.
2. Every on-disk write is atomic; backups and vault self-repair are built in.
3. Foreign processes are never killed — only after a triple check
   (image name + launchtime token + create_time).
4. No injection into the Roblox client — external Win32 calls only.
5. A plan is not an action: launch settings are data, a service executes them.
