#include "formats.h"
#include <QUuid>
#include <bit>
#include <limits>

quint64 integer(QByteArrayView b, bool little) {
    quint64 v = 0;
    for (qsizetype i = 0; i < b.size(); ++i)
        v = (v << 8) | quint8(b[little ? b.size() - 1 - i : i]);
    return v;
}
QStringList inspect(QByteArrayView b, bool little, int base) {
    QStringList rows;
    for (int n : {1, 2, 4, 8}) {
        QString u = "Out of range", s = u, f = u;
        if (n <= b.size()) {
            auto bits = integer(b.first(n), little);
            u = QString::number(bits, base);
            bool negative = bits >> (n * 8 - 1);
            auto mask = std::numeric_limits<quint64>::max() >> (64 - n * 8);
            s = negative ? "-" + QString::number(((~bits) & mask) + 1, base) : u;
            if (n == 4)
                f = QString::number(std::bit_cast<float>(quint32(bits)), 'g', 9);
            if (n == 8)
                f = QString::number(std::bit_cast<double>(bits), 'g', 17);
        }
        rows << QString("uint%1\t%2").arg(n * 8).arg(u) << QString("int%1\t%2").arg(n * 8).arg(s);
        if (n >= 4)
            rows << QString("float%1\t%2").arg(n * 8).arg(f);
    }
    QString a = "Out of range", u = a;
    if (!b.empty()) {
        quint8 c = b[0];
        a = c >= 32 && c < 127 ? QString(QChar(c)) : c < 128 ? "Control" : "Non-ASCII";
        int len = c < 128                  ? 1
                  : c >= 0xc2 && c <= 0xdf ? 2
                  : c >= 0xe0 && c <= 0xef ? 3
                  : c >= 0xf0 && c <= 0xf4 ? 4
                                           : 0;
        u = !len                          ? "Invalid UTF-8"
            : len > b.size()              ? "Truncated UTF-8"
            : !b.first(len).isValidUtf8() ? "Invalid UTF-8"
                                          : QString::fromUtf8(b.first(len));
        if (len == 1 && c < 32)
            u = QString("U+%1").arg(c, 4, 16, QChar('0'));
    }
    return rows << ("ASCII\t" + a) << ("UTF-8\t" + u);
}
namespace {
struct Reader {
    const Snapshot &s;
    qint64 base, size;
    bool fits(quint64 at, quint64 n) const {
        return at <= quint64(size) && n <= quint64(size) - at;
    }
    QByteArray bytes(qint64 at, qint64 n) const {
        return fits(at, n) ? s.read(base + at, n) : QByteArray();
    }
    quint64 num(qint64 at, int n, bool le) const {
        auto b = bytes(at, n);
        return b.size() == n ? integer(b, le) : 0;
    }
    QString name(qint64 at, int n) const {
        return QString::fromLatin1(bytes(at, n).split('\0').first());
    }
    Field field(QString title, qint64 at, qint64 n) const { return {title, base + at, n, {}}; }
};
void issue(Field &root, QString why = "Truncated or invalid structure") {
    root.children.append({why, root.offset, 0, {}});
}
constexpr int limit = 1024;
void mach(Reader r, Field &root) {
    auto magic = r.bytes(0, 4).toHex();
    bool le = magic == "cefaedfe" || magic == "cffaedfe";
    bool wide = magic == "feedfacf" || magic == "cffaedfe";
    root.label = QString("Mach-O %1-bit").arg(wide ? 64 : 32);
    int header = wide ? 32 : 28;
    if (!r.fits(0, header)) {
        issue(root);
        return;
    }
    auto n = [&](qint64 a) { return r.num(a, 4, le); };
    auto count = n(16), total = n(20);
    root.children << r.field(QString("CPU type  0x%1").arg(n(4), 0, 16), 4, 4)
                  << r.field(QString("File type  %1").arg(n(12)), 12, 4)
                  << r.field(QString("Load commands  %1").arg(count), 16, 4)
                  << r.field(QString("Flags  0x%1").arg(n(24), 0, 16), 24, 4);
    if (!r.fits(header, total)) {
        issue(root);
        return;
    }
    qint64 at = header, end = header + qint64(total);
    int budget = limit;
    for (quint64 i = 0; i < count; ++i) {
        if (--budget < 0) {
            issue(root, "Structure display limit reached");
            break;
        }
        if (end - at < 8) {
            issue(root);
            break;
        }
        auto cmd = n(at), sz = n(at + 4);
        if (sz < 8 || sz > quint64(end - at)) {
            issue(root);
            break;
        }
        Field node = r.field(QString("LC 0x%1").arg(cmd, 0, 16), at, sz);
        if (cmd == 1 || cmd == 25) {
            bool w = cmd == 25;
            int head = w ? 72 : 56, step = w ? 80 : 68;
            if (sz < quint64(head)) {
                issue(root);
                break;
            }
            node.label = "Segment  " + r.name(at + 8, 16);
            auto sections = n(at + (w ? 64 : 48));
            if (sections > (sz - head) / step) {
                issue(root);
                break;
            }
            for (quint64 j = 0; j < sections; ++j) {
                if (--budget < 0) {
                    issue(node, "Section display limit reached");
                    break;
                }
                qint64 off = at + head + j * step;
                auto file = n(off + (w ? 48 : 40)), len = r.num(off + (w ? 40 : 36), w ? 8 : 4, le);
                auto flags = n(off + (w ? 64 : 56)) & 255;
                auto section = r.field(r.name(off, 16), off, step);
                if (flags != 1 && flags != 12 && flags != 18) {
                    if (r.fits(file, len))
                        section.children << r.field("File data", file, len);
                    else
                        issue(section);
                }
                node.children << section;
            }
            auto address = r.num(at + 24, w ? 8 : 4, le);
            node.children << r.field(QString("VM address   0x%1").arg(address, 0, 16), at + 24,
                                     w ? 8 : 4);
        } else if (cmd == 0x1b) {
            node.label = "LC_UUID";
            if (sz < 24)
                issue(node);
            else
                node.children << r.field(
                    QUuid::fromRfc4122(r.bytes(at + 8, 16)).toString(QUuid::WithoutBraces), at + 8,
                    16);
        } else if (cmd == 2) {
            node.label = "LC_SYMTAB";
            if (sz < 24)
                issue(node);
            else {
                auto symbols = n(at + 8), count = n(at + 12), strings = n(at + 16),
                     stringBytes = n(at + 20);
                auto bytes = count * (wide ? 16 : 12);
                if (r.fits(symbols, bytes) && r.fits(strings, stringBytes)) {
                    node.children << r.field(QString("Symbols   %1").arg(count), symbols, bytes)
                                  << r.field("String table", strings, stringBytes);
                } else
                    issue(node);
            }
        } else if (cmd == 0x80000028) {
            node.label = "LC_MAIN";
            if (sz < 24)
                issue(node);
            else
                node.children << r.field(
                    QString("Entry offset   0x%1").arg(r.num(at + 8, 8, le), 0, 16), at + 8, 8);
        } else if (cmd == 0xc || cmd == 0xd || cmd == 0x80000018 || cmd == 0x8000001f) {
            node.label = cmd == 0xd ? "Library identity" : "Linked library";
            auto nameOffset = sz >= 24 ? n(at + 8) : 0;
            if (nameOffset < 24 || nameOffset >= sz)
                issue(node);
            else
                node.children << r.field(
                    r.name(at + nameOffset, int(std::min<quint64>(sz - nameOffset, 512))),
                    at + nameOffset, sz - nameOffset);
        }
        root.children << node;
        at += sz;
    }
}
bool isMach(QByteArray h) {
    return QList<QByteArray>{"feedface", "cefaedfe", "feedfacf", "cffaedfe"}.contains(h);
}
} // namespace
Field structure(const Snapshot &s) {
    Reader r{s, 0, s.size};
    Field root = {"Unknown", 0, s.size, {}};
    auto b = r.bytes(0, std::min<qint64>(16, s.size));
    auto h = b.left(4).toHex();
    if (isMach(h))
        mach(r, root);
    else if (QList<QByteArray>{"cafebabe", "bebafeca", "cafebabf", "bfbafeca"}.contains(h)) {
        bool le = h.endsWith("ca"), wide = h == "cafebabf" || h == "bfbafeca";
        int step = wide ? 32 : 20;
        root.label = "Mach-O universal";
        auto count = r.num(4, 4, le);
        if (!count || count > 64 || !r.fits(8, count * step)) {
            issue(root);
            return root;
        }
        for (quint64 i = 0; i < count; ++i) {
            qint64 at = 8 + i * step;
            auto off = r.num(at + 8, wide ? 8 : 4, le),
                 len = r.num(at + (wide ? 16 : 12), wide ? 8 : 4, le);
            if (!r.fits(off, len)) {
                issue(root);
                continue;
            }
            Reader slice{s, qint64(off), qint64(len)};
            Field node = slice.field("Unknown slice", 0, len);
            if (isMach(slice.bytes(0, 4).toHex()))
                mach(slice, node);
            node.label += QString(" · CPU 0x%1").arg(r.num(at, 4, le), 0, 16);
            root.children << node;
        }
    } else if (b.startsWith("\x7f"
                            "ELF")) {
        root.label = "ELF";
        bool wide = b.size() > 5 && b[4] == 2, le = b.size() > 5 && b[5] == 1;
        if (b.size() < 16 || (b[4] != 1 && b[4] != 2) || (b[5] != 1 && b[5] != 2) ||
            !r.fits(0, wide ? 64 : 52)) {
            issue(root);
            return root;
        }
        root.label += wide ? " 64-bit" : " 32-bit";
        root.children << r.field(QString("Machine  0x%1").arg(r.num(18, 2, le), 0, 16), 18, 2);
        auto off = r.num(wide ? 40 : 32, wide ? 8 : 4, le), step = r.num(wide ? 58 : 46, 2, le),
             count = r.num(wide ? 60 : 48, 2, le);
        if (count && (step < (wide ? 64u : 40u) || !r.fits(off, count * step)))
            issue(root);
        else
            for (quint64 i = 0; i < count && i < limit; ++i) {
                qint64 at = off + i * step;
                auto type = r.num(at + 4, 4, le),
                     file = r.num(at + (wide ? 24 : 16), wide ? 8 : 4, le),
                     len = r.num(at + (wide ? 32 : 20), wide ? 8 : 4, le);
                auto node = r.field(QString("Section %1 · type %2").arg(i).arg(type), at, step);
                if (type != 8 && len) {
                    if (r.fits(file, len))
                        node.children << r.field("File data", file, len);
                    else
                        issue(node);
                }
                root.children << node;
            }
    } else if (b.startsWith("MZ")) {
        root.label = "DOS header";
        if (!r.fits(60, 4)) {
            issue(root);
            return root;
        }
        auto off = r.num(60, 4, true);
        if (!r.fits(off, 24) || r.bytes(off, 4) != QByteArray("PE\0\0", 4))
            return root;
        root.label = "PE";
        auto count = r.num(off + 6, 2, true), opt = r.num(off + 20, 2, true),
             table = off + 24 + opt;
        root.children << r.field(QString("COFF machine  0x%1").arg(r.num(off + 4, 2, true), 0, 16),
                                 off + 4, 2);
        if (!r.fits(table, count * 40)) {
            issue(root);
            return root;
        }
        for (quint64 i = 0; i < count && i < limit; ++i) {
            auto at = table + i * 40, len = r.num(at + 16, 4, true), file = r.num(at + 20, 4, true);
            auto node = r.field(r.name(at, 8), at, 40);
            if (r.fits(file, len))
                node.children << r.field("Raw data", file, len);
            else
                issue(node);
            root.children << node;
        }
    } else if (b.startsWith(QByteArray::fromHex("89504e470d0a1a0a"))) {
        root.label = "PNG";
        qint64 at = 8;
        while (at < s.size && root.children.size() < limit) {
            auto len = r.num(at, 4, false);
            if (!r.fits(at, len + 12)) {
                issue(root);
                break;
            }
            auto type = r.name(at + 4, 4);
            auto node = r.field(type, at, len + 12);
            if (type == "IHDR" && len == 13)
                node.children << r.field(QString("%1 × %2 pixels")
                                             .arg(r.num(at + 8, 4, false))
                                             .arg(r.num(at + 12, 4, false)),
                                         at + 8, 8);
            root.children << node;
            at += len + 12;
            if (type == "IEND")
                break;
        }
    } else if (b.startsWith(QByteArray::fromHex("ffd8"))) {
        root.label = "JPEG";
        qint64 at = 2;
        int budget = 8192;
        while (at < s.size && root.children.size() < limit) {
            if (--budget == 0) {
                issue(root, "Marker scan limit reached");
                break;
            }
            if (!r.fits(at, 2) || r.num(at, 1, false) != 255) {
                issue(root);
                break;
            }
            auto mark = r.num(at + 1, 1, false);
            if (mark == 255) {
                ++at;
                continue;
            }
            if (mark == 217) {
                root.children << r.field("End of image", at, 2);
                break;
            }
            auto len = r.num(at + 2, 2, false);
            if (len < 2 || !r.fits(at, len + 2)) {
                issue(root);
                break;
            }
            auto node = r.field(QString("Marker FF%1").arg(mark, 2, 16, QChar('0')), at, len + 2);
            if ((mark == 192 || mark == 194) && len >= 8)
                node.children << r.field(QString("%1 × %2 pixels")
                                             .arg(r.num(at + 7, 2, false))
                                             .arg(r.num(at + 5, 2, false)),
                                         at + 5, 4);
            root.children << node;
            at += len + 2;
            if (mark == 218) {
                root.children << r.field("Scan and remaining markers", at, s.size - at);
                break;
            }
        }
    } else if (b.startsWith(QByteArray::fromHex("504b0304")) ||
               b.startsWith(QByteArray::fromHex("504b0506"))) {
        root.label = "ZIP";
        qint64 at = 0;
        while (at < s.size && root.children.size() < limit) {
            auto sig = r.bytes(at, 4).toHex();
            if (sig == "504b0506") {
                if (r.fits(at, 22))
                    root.children << r.field("End of central directory", at, 22);
                else
                    issue(root);
                break;
            }
            if (sig == "504b0102") {
                root.children << r.field("Central directory and trailer", at, s.size - at);
                break;
            }
            if (sig != "504b0304" || !r.fits(at, 30)) {
                issue(root);
                break;
            }
            auto len = r.num(at + 18, 4, true), name = r.num(at + 26, 2, true),
                 head = 30 + name + r.num(at + 28, 2, true);
            if (!r.fits(at, head)) {
                issue(root);
                break;
            }
            auto node = r.field(QString::fromUtf8(r.bytes(at + 30, std::min<quint64>(name, 256))),
                                at, head);
            if (r.num(at + 6, 2, true) & 8) {
                node.children << r.field("Data descriptor · length deferred", at + 6, 2);
                root.children << node;
                break;
            }
            if (!r.fits(at, head + len)) {
                issue(root);
                break;
            }
            node.children << r.field("Compressed data", at + head, len);
            root.children << node;
            at += head + len;
        }
    }
    return root;
}

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
Field applyTemplate(const Snapshot &s, QByteArrayView json, QString &error) {
    Field result{"Template", 0, s.size, {}};
    error.clear();
    if (json.size() > 65536) {
        error = "Template exceeds 64 KiB";
        return result;
    }
    QJsonParseError failure;
    auto obj = QJsonDocument::fromJson(json.toByteArray(), &failure).object();
    auto fields = obj["fields"].toArray();
    if (failure.error || fields.isEmpty() || fields.size() > 256) {
        error = "Expected a JSON object with 1–256 fields";
        return result;
    }
    auto title = obj["name"].toString().left(80);
    if (!title.isEmpty())
        result.label = title;
    for (auto item : fields) {
        auto f = item.toObject();
        auto label = f["name"].toString().left(80);
        auto at = f["offset"].toInteger(-1), len = f["length"].toInteger(-1);
        if (label.isEmpty() || at < 0 || len < 1) {
            error = "Each field requires a name, integer offset and positive length";
            return result;
        }
        if (at > s.size || len > s.size - at) {
            result.children << Field{label + " · out of range", std::min(at, s.size), 0, {}};
            continue;
        }
        result.children << Field{label, at, len, {}};
    }
    return result;
}
