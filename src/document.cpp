#include "document.h"
#include <QDataStream>
#include <QFileInfo>
#include <QSaveFile>
#include <algorithm>
#include <functional>
#include <limits>
#include <sys/stat.h>
namespace {
QByteArray stamp(const struct stat &info) {
#ifdef Q_OS_MACOS
    const auto modified = info.st_mtimespec;
#else
    const auto modified = info.st_mtim;
#endif
    QByteArray result;
    QDataStream stream(&result, QIODevice::WriteOnly);
    stream << quint64(info.st_dev) << quint64(info.st_ino) << qint64(info.st_size)
           << qint64(modified.tv_sec) << qint64(modified.tv_nsec);
    return result;
}
} // namespace

QByteArray Snapshot::read(qint64 at, qint64 count) const {
    QByteArray out;
    if (at < 0 || at >= size || count <= 0)
        return out;
    count = std::min(count, size - at);
    auto i = std::upper_bound(starts.begin(), starts.end(), at) - starts.begin() - 1;
    QMutexLocker lock(&source->mutex);
    while (count && i < pieces.size()) {
        const auto &p = pieces[i];
        auto skip = at - starts[i], n = std::min(count, p.length - skip);
        if (p.bytes.isEmpty()) {
            if (!source->file.seek(p.offset + skip))
                return {};
            auto part = source->file.read(n);
            if (part.size() != n)
                return {};
            out += part;
        } else
            out.append(p.bytes.constData() + p.offset + skip, n);
        count -= n;
        at += n;
        ++i;
    }
    return out;
}
bool Snapshot::modified(qint64 at) const {
    if (at < 0 || at >= size)
        return false;
    auto i = std::upper_bound(starts.begin(), starts.end(), at) - starts.begin() - 1;
    return !pieces[i].bytes.isEmpty();
}
bool Document::open(const QString &name) {
    QFileInfo info(name);
    auto next = std::make_shared<Source>(info.canonicalFilePath());
    if (!info.isFile() || !next->file.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("Cannot read file: ") + name;
        return false;
    }
    struct stat opened{};
    if (fstat(next->file.handle(), &opened) != 0) {
        error = "Cannot inspect source file";
        return false;
    }
    history.clear();
    source = next;
    path = info.canonicalFilePath();
    writable = false;
    size = opened.st_size;
    sourceStamp = stamp(opened);
    pieces.clear();
    starts.clear();
    if (size) {
        pieces.append({0, size, {}});
        starts.append(0);
    }
    return true;
}
class Change final : public QUndoCommand {
    std::function<void(bool)> apply;

  public:
    explicit Change(std::function<void(bool)> fn) : apply(std::move(fn)) {}
    void redo() override { apply(true); }
    void undo() override { apply(false); }
};
bool Document::edit(qint64 at, qint64 remove, const QByteArray &insert) {
    if (!writable || at < 0 || at > size || remove < 0 || remove > size - at ||
        insert.size() > std::numeric_limits<qint64>::max() - (size - remove) ||
        (!remove && insert.isEmpty()))
        return false;
    QList<Piece> after;
    auto keep = [&](qint64 lo, qint64 hi) {
        for (qsizetype i = 0; i < pieces.size(); ++i) {
            auto p = pieces[i];
            auto a = std::max(lo, starts[i]), b = std::min(hi, starts[i] + p.length);
            if (b > a) {
                p.offset += a - starts[i];
                p.length = b - a;
                after.append(p);
            }
        }
    };
    keep(0, at);
    if (!insert.isEmpty())
        after.append({0, insert.size(), insert});
    keep(at + remove, size);
    history.push(new Change([this, before = pieces, after](bool forward) {
        pieces = forward ? after : before;
        starts.clear();
        size = 0;
        for (const auto &p : pieces) {
            starts.append(size);
            size += p.length;
        }
    }));
    return true;
}
bool Document::extract(const QString &name, qint64 at, qint64 count) {
    if (QFileInfo(name).canonicalFilePath() == path) {
        error = "Extraction requires a separate output file";
        return false;
    }
    return writeRange(name, at, count);
}
bool Document::writeRange(const QString &name, qint64 at, qint64 count) {
    auto current = [this] {
        struct stat info{};
        return ::stat(QFile::encodeName(path).constData(), &info) == 0 &&
               stamp(info) == sourceStamp;
    };
    if (!current()) {
        error = "Source has changed on disk";
        return false;
    }
    if (at < 0 || at > size || count < 0 || count > size - at) {
        error = "Invalid range";
        return false;
    }
    QSaveFile out(name);
    out.setDirectWriteFallback(false);
    if (!out.open(QIODevice::WriteOnly)) {
        error = out.errorString();
        return false;
    }
    while (count) {
        auto n = std::min<qint64>(count, 1 << 20);
        auto data = read(at, n);
        if (data.size() != n || out.write(data) != n) {
            error = "Incomplete file read or write";
            return false;
        }
        count -= n;
        at += n;
    }
    if (!current()) {
        error = "Source changed during write";
        return false;
    }
    if (!out.commit()) {
        error = out.errorString();
        return false;
    }
    return true;
}
bool Document::save(const QString &name) {
    if (!writable) {
        error = "Read-only document";
        return false;
    }
    if (!writeRange(name, 0, size))
        return false;
    return open(name);
}
