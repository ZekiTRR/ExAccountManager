#pragma once

#include "core/models/domain.h"

#include <QList>
#include <QRect>

namespace ex::platform {

// Управление окнами клиентов Roblox: позиция/размер [feature №2], скрытие
// без кражи фокуса, авто-раскладка сеткой (RM), переименование заголовков.
class WindowMgr {
public:
    // Применить WindowProfile к главному окну процесса.
    // state=Hidden использует ShowWindow(SW_HIDE) — окно скрыто, не свёрнуто.
    static bool applyProfile(qint64 pid, const ex::model::WindowProfile& profile);

    // Показать/скрыть без кражи фокуса (SW_SHOW_NO_ACTIVATE / SW_HIDE).
    static bool showNoActivate(qint64 pid);
    static bool hide(qint64 pid);

    // Минимизация через PostMessage WM_SYSCOMMAND/SC_MINIMIZE —
    // не активирует окно (приём RAM).
    static bool minimizeNoFocus(qint64 pid);

    // Фокус на окно (с фолбеком AttachThreadInput как в RM).
    static bool focus(qint64 pid);

    // Заголовок окна: чтение/установка (переименование в имя аккаунта).
    static QString title(qint64 pid);
    static bool setTitle(qint64 pid, const QString& title);

    // Авто-раскладка окон списком PID в сетку по виртуальному рабочему столу
    // (1 — весь экран, 2 — рядом, 3 — 2+1, 4 — 2x2, ...). Компенсация
    // невидимых DWM-границ (приём RM).
    static bool arrangeGrid(const QList<qint64>& pids);

    // Разложить окна сеткой с произвольным размером ячейки (например, 100×100).
    // Окна идут слева направо и переносятся на следующую строку.
    static bool arrangeGridSized(const QList<qint64>& pids, int width, int height,
                                 int gap = 8);

    // Изменить размер окна, сохранив его текущую позицию.
    static bool resizeWindow(qint64 pid, int width, int height);

    // Текущий rect главного окна процесса.
    static QRect windowRect(qint64 pid);

    // Рабочая область основного монитора (без панели задач).
    static QRect primaryWorkArea();
};

} // namespace ex::platform
