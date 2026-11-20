#pragma once
#include <QFile>
#include <QMutex>
#include <QUndoStack>
#include <memory>

struct Source {
    QFile file;
    QMutex mutex;
    explicit Source(const QString &path) : file(path) {}
};
struct Piece {
    qint64 offset, length;
    QByteArray bytes; // Empty means original file.
};
class Snapshot {
  public:
    qint64 size = 0;
    QByteArray read(qint64 offset, qint64 count) const;
    bool modified(qint64 offset) const;

  protected:
    std::shared_ptr<Source> source;
    QList<Piece> pieces;
    QList<qint64> starts;
};
class Document : public Snapshot {
  public:
    QString path, error;
    bool writable = false;
    QUndoStack history;
    bool open(const QString &name);
    bool edit(qint64 at, qint64 remove, const QByteArray &insert);
    bool save(const QString &name);
    bool extract(const QString &name, qint64 at, qint64 count);

  private:
    bool writeRange(const QString &name, qint64 at, qint64 count);
    QByteArray sourceStamp;
};
