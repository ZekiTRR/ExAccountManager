#pragma once

#include <QString>
#include <QStringList>
#include <QVariant>

struct sqlite3;
struct sqlite3_stmt;

namespace ex::store {

// RAII wrapper over sqlite3*. WAL, busy_timeout, foreign_keys.
// Migrations create every table (schema version in PRAGMA user_version).
class Database {
public:
    Database() = default;
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    bool open(const QString& path);
    void close();
    bool isOpen() const { return m_db != nullptr; }
    sqlite3* handle() const { return m_db; }

    bool exec(const QString& sql, QString* err = nullptr);

    // Simple scalar query (SELECT count(*) FROM t WHERE ...).
    // Returns the first value of the first row.
    QVariant scalar(const QString& sql);

    int userVersion() const;
    bool setUserVersion(int v);

    QString lastError() const;

private:
    bool migrate();
    sqlite3* m_db = nullptr;
};

// RAII wrapper over a prepared statement.
class Stmt {
public:
    Stmt(sqlite3* db, const QString& sql);
    ~Stmt();

    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;

    bool isValid() const { return m_stmt != nullptr; }

    void bind(int idx, const QString& v);
    void bind(int idx, qint64 v);
    void bind(int idx, int v);
    void bind(int idx, bool v);
    void bind(int idx, const QByteArray& v);
    void bindNull(int idx);

    bool step();          // true when the string is present
    bool exec();          // for INSERT/UPDATE/DELETE
    QString colText(int idx) const;
    qint64 colInt(int idx) const;
    bool colBool(int idx) const;
    QByteArray colBlob(int idx) const;
    bool colIsNull(int idx) const;

    qint64 lastInsertRowid(sqlite3* db) const;

private:
    sqlite3_stmt* m_stmt = nullptr;
};

} // namespace ex::store
