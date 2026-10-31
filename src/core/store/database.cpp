#include "core/store/database.h"

#include "core/util/log.h"

#include <sqlite3.h>

namespace ex::store {

Database::~Database()
{
    close();
}

bool Database::open(const QString& path)
{
    close();
    sqlite3* db = nullptr;
    if (sqlite3_open16(path.utf16(), &db) != SQLITE_OK) {
        if (db)
            sqlite3_close(db);
        EXLOG_ERROR("db") << "cannot open database:" << path;
        return false;
    }
    m_db = db;
    exec(QStringLiteral("PRAGMA journal_mode=WAL;"));
    exec(QStringLiteral("PRAGMA synchronous=NORMAL;"));
    exec(QStringLiteral("PRAGMA busy_timeout=5000;"));
    exec(QStringLiteral("PRAGMA foreign_keys=ON;"));
    return migrate();
}

void Database::close()
{
    if (m_db) {
        sqlite3_close_v2(m_db);
        m_db = nullptr;
    }
}

bool Database::exec(const QString& sql, QString* err)
{
    char* errMsg = nullptr;
    if (sqlite3_exec(m_db, sql.toUtf8().constData(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
        if (err)
            *err = QString::fromUtf8(errMsg);
        EXLOG_WARN("db") << "exec failed:" << (errMsg ? errMsg : "?") << "sql:" << sql.left(200);
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

QVariant Database::scalar(const QString& sql)
{
    Stmt s(m_db, sql);
    if (!s.isValid() || !s.step())
        return {};
    return s.colText(0);
}

int Database::userVersion() const
{
    // The C-API has no direct function — read the PRAGMA
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "PRAGMA user_version", -1, &stmt, nullptr) != SQLITE_OK)
        return 0;
    int v = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        v = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return v;
}

bool Database::setUserVersion(int v)
{
    return exec(QStringLiteral("PRAGMA user_version = %1;").arg(v));
}

QString Database::lastError() const
{
    return QString::fromUtf8(sqlite3_errmsg(m_db));
}

bool Database::migrate()
{
    // ---------------------------------------------------------------- schema v1
    if (userVersion() < 1) {
        const char* kV1 = R"SQL(
CREATE TABLE IF NOT EXISTS groups (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL UNIQUE,
    color TEXT,
    collapsed INTEGER NOT NULL DEFAULT 0,
    sort_order INTEGER NOT NULL DEFAULT 0
);
CREATE TABLE IF NOT EXISTS accounts (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER NOT NULL DEFAULT 0,
    username TEXT NOT NULL,
    display_name TEXT DEFAULT '',
    alias TEXT DEFAULT '',
    description TEXT DEFAULT '',
    group_id INTEGER REFERENCES groups(id) ON DELETE SET NULL,
    avatar_url TEXT DEFAULT '',
    favorite INTEGER NOT NULL DEFAULT 0,
    sort_order INTEGER NOT NULL DEFAULT 0,
    priority INTEGER NOT NULL DEFAULT 0,
    tags TEXT NOT NULL DEFAULT '[]',
    fields TEXT NOT NULL DEFAULT '{}',
    has_session INTEGER NOT NULL DEFAULT 0,
    cookie_expired INTEGER NOT NULL DEFAULT 0,
    verified INTEGER NOT NULL DEFAULT 0,
    moderation TEXT,
    last_used TEXT,
    last_validated TEXT,
    saved_place_id INTEGER NOT NULL DEFAULT 0,
    saved_job_id TEXT DEFAULT '',
    saved_private_server_code TEXT DEFAULT '',
    browser_tracker_id INTEGER DEFAULT 0,
    window_profile TEXT,
    ram_limit TEXT,
    created_at TEXT NOT NULL DEFAULT (datetime('now'))
);
CREATE UNIQUE INDEX IF NOT EXISTS idx_accounts_username ON accounts(username);
CREATE INDEX IF NOT EXISTS idx_accounts_group ON accounts(group_id);

CREATE TABLE IF NOT EXISTS presets (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    target TEXT NOT NULL,
    fps_cap INTEGER NOT NULL DEFAULT 0,
    group_id INTEGER NOT NULL DEFAULT 0,
    note TEXT DEFAULT '',
    window TEXT
);

CREATE TABLE IF NOT EXISTS private_servers (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    owner_id INTEGER NOT NULL DEFAULT 0,
    name TEXT DEFAULT '',
    place_id INTEGER NOT NULL DEFAULT 0,
    code TEXT NOT NULL,
    added TEXT,
    game_name TEXT DEFAULT '',
    game_icon_url TEXT DEFAULT ''
);

CREATE TABLE IF NOT EXISTS settings (
    key TEXT PRIMARY KEY,
    value TEXT
);

-- Секреты: только зашифрованные блобы (AES-256-GCM под data key vault.dat).
-- Сами ключи шифрования в файле НЕТ.
CREATE TABLE IF NOT EXISTS secret_entries (
    account_id INTEGER PRIMARY KEY REFERENCES accounts(id) ON DELETE CASCADE,
    cookie BLOB,
    password BLOB
);

CREATE TABLE IF NOT EXISTS activity (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    ts TEXT NOT NULL DEFAULT (datetime('now')),
    kind TEXT NOT NULL,
    account_id INTEGER,
    detail TEXT DEFAULT ''
);

-- Ledger сессий для аналитики: сколько аккаунт провёл в игре.
CREATE TABLE IF NOT EXISTS session_ledger (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    account_id INTEGER NOT NULL,
    place_id INTEGER NOT NULL DEFAULT 0,
    job_id TEXT DEFAULT '',
    started_at TEXT NOT NULL,
    ended_at TEXT,
    ended_reason TEXT DEFAULT '',
    pid INTEGER DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_ledger_account ON session_ledger(account_id, started_at);

CREATE TABLE IF NOT EXISTS macro_defs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL UNIQUE,
    source TEXT NOT NULL,
    created_at TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at TEXT
);

CREATE TABLE IF NOT EXISTS schedule_tasks (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    time_hhmm TEXT NOT NULL,
    days TEXT NOT NULL DEFAULT '1234567',
    action TEXT NOT NULL,
    payload TEXT DEFAULT '{}',
    enabled INTEGER NOT NULL DEFAULT 1,
    last_run TEXT
);
)SQL";
        if (!exec(QString::fromUtf8(kV1)))
            return false;
        setUserVersion(1);
        EXLOG_INFO("db") << "schema v1 created";
    }
    // ---------------------------------------------------------------- schema v2
    // Index deduplicating by UserId (RAM); usernames can change.
    if (userVersion() < 2) {
        exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_accounts_user_id ON accounts(user_id);"));
        setUserVersion(2);
        EXLOG_INFO("db") << "schema v2 applied";
    }
    // Future migrations: if (userVersion() < 3) { ...; setUserVersion(3); }
    return true;
}

// ------------------------------------------------------------------ Stmt

Stmt::Stmt(sqlite3* db, const QString& sql)
{
    if (sqlite3_prepare_v2(db, sql.toUtf8().constData(), -1, &m_stmt, nullptr) != SQLITE_OK) {
        EXLOG_WARN("db") << "prepare failed:" << sqlite3_errmsg(db) << "sql:" << sql.left(200);
        m_stmt = nullptr;
    }
}

Stmt::~Stmt()
{
    if (m_stmt)
        sqlite3_finalize(m_stmt);
}

void Stmt::bind(int idx, const QString& v)
{
    sqlite3_bind_text16(m_stmt, idx, v.utf16(), static_cast<int>(v.size() * sizeof(ushort)), SQLITE_TRANSIENT);
}

void Stmt::bind(int idx, qint64 v)
{
    sqlite3_bind_int64(m_stmt, idx, v);
}

void Stmt::bind(int idx, int v)
{
    sqlite3_bind_int(m_stmt, idx, v);
}

void Stmt::bind(int idx, bool v)
{
    sqlite3_bind_int(m_stmt, idx, v ? 1 : 0);
}

void Stmt::bind(int idx, const QByteArray& v)
{
    sqlite3_bind_blob64(m_stmt, idx, v.constData(), static_cast<sqlite3_uint64>(v.size()), SQLITE_TRANSIENT);
}

void Stmt::bindNull(int idx)
{
    sqlite3_bind_null(m_stmt, idx);
}

bool Stmt::step()
{
    return m_stmt && sqlite3_step(m_stmt) == SQLITE_ROW;
}

bool Stmt::exec()
{
    return m_stmt && sqlite3_step(m_stmt) == SQLITE_DONE;
}

QString Stmt::colText(int idx) const
{
    const unsigned char* t = sqlite3_column_text(m_stmt, idx);
    if (!t)
        return {};
    return QString::fromUtf8(reinterpret_cast<const char*>(t),
                             static_cast<qsizetype>(sqlite3_column_bytes(m_stmt, idx)));
}

qint64 Stmt::colInt(int idx) const
{
    return sqlite3_column_int64(m_stmt, idx);
}

bool Stmt::colBool(int idx) const
{
    return sqlite3_column_int(m_stmt, idx) != 0;
}

QByteArray Stmt::colBlob(int idx) const
{
    const void* p = sqlite3_column_blob(m_stmt, idx);
    const int n = sqlite3_column_bytes(m_stmt, idx);
    if (!p || n <= 0)
        return {};
    return QByteArray(reinterpret_cast<const char*>(p), n);
}

bool Stmt::colIsNull(int idx) const
{
    return sqlite3_column_type(m_stmt, idx) == SQLITE_NULL;
}

qint64 Stmt::lastInsertRowid(sqlite3* db) const
{
    return sqlite3_last_insert_rowid(db);
}

} // namespace ex::store
