#include "analysis.h"
#include <QCryptographicHash>
#include <cmath>
namespace {
double entropy(const std::array<quint64, 256> &freq, qint64 total) {
    double v = 0;
    for (auto n : freq)
        if (n) {
            auto p = double(n) / total;
            v -= p * std::log2(p);
        }
    return v;
}
} // namespace
Report analyze(const Snapshot &s, qint64 start, qint64 count, Operation op, QByteArray query,
               Snapshot other, const std::atomic_bool *cancel) {
    Report r;
    r.start = start;
    r.count = count;
    if (start < 0 || count < 0 || start > s.size || count > s.size - start ||
        (op == Operation::Search && query.isEmpty())) {
        r.error = "Invalid input range or search pattern";
        return r;
    }
    auto add = [&](qint64 offset, qint64 len, QString text) {
        if (r.hits.size() < 4096)
            r.hits << Finding{offset, len, text};
        else
            r.limited = true;
    };
    QCryptographicHash hash(QCryptographicHash::Sha256);
    std::array<quint64, 256> group{};
    r.regionSize = std::max<qint64>(256, count / 256 + (count % 256 != 0));
    qint64 inGroup = 0, runAt = 0, diff = -1, chars = 0, stringAt = 0;
    std::array<qint64, 9> streak{};
    std::array<char, 8> ring{};
    int need = 0;
    QByteArray carry, sequence, preview;
    bool unicode = false;
    auto finishPattern = [&](int period, qint64 end) {
        auto matched = streak[period];
        streak[period] = 0;
        if (matched < 3 * period || r.limited)
            return;
        auto length = matched + period, at = end - length;
        auto pattern = s.read(at, period);
        for (int sub = 1; sub < period; ++sub)
            if (period % sub == 0 && pattern.left(sub).repeated(period / sub) == pattern)
                return;
        add(at, length - length % period,
            "Repeated " + QString::fromLatin1(pattern.toHex(' ').toUpper()));
    };
    auto flushString = [&] {
        if (chars >= 4)
            add(stringAt, runAt - stringAt,
                QString(unicode ? "UTF-8   " : "ASCII   ") + QString::fromUtf8(preview) +
                    (chars > 120 ? "…" : ""));
        chars = 0;
        preview.clear();
        unicode = false;
    };
    auto printable = [&](QByteArrayView b, qint64 at) {
        if (!chars)
            stringAt = at;
        if (++chars <= 120)
            preview.append(b.data(), b.size());
        runAt = at + b.size();
        unicode |= b.size() > 1;
    };
    qint64 end = op == Operation::Compare ? std::max(s.size, other.size) : start + count;
    for (qint64 at = start; at < end;) {
        if (cancel && cancel->load()) {
            r.error = "Cancelled";
            return r;
        }
        auto n = std::min<qint64>(1 << 16, end - at);
        auto data = s.read(at, n);
        if (data.size() != std::clamp<qint64>(s.size - at, 0, n)) {
            r.error = "Source read failed";
            return r;
        }
        if (op == Operation::Search) {
            auto block = carry + data;
            qsizetype i = 0;
            while ((i = block.indexOf(query, i)) != -1) {
                add(at - carry.size() + i, query.size(), "Match");
                ++i;
                if (r.limited)
                    return r;
            }
            carry = block.right(std::min(block.size(), query.size() - 1));
        } else if (op == Operation::Compare) {
            auto b = other.read(at, n);
            if (b.size() != std::clamp<qint64>(other.size - at, 0, n)) {
                r.error = "Comparison read failed";
                return r;
            }
            for (qint64 i = 0; i < n; ++i) {
                bool different = i >= data.size() || i >= b.size() || data[i] != b[i];
                if (different && diff < 0)
                    diff = at + i;
                if (!different && diff >= 0) {
                    add(diff, at + i - diff, "Difference");
                    diff = -1;
                }
            }
        } else if (op == Operation::Statistics) {
            hash.addData(data);
            for (auto c : data) {
                ++r.frequency[quint8(c)];
                ++group[quint8(c)];
                if (++inGroup == r.regionSize) {
                    r.regions << entropy(group, inGroup);
                    inGroup = 0;
                    group.fill(0);
                }
            }
        } else
            for (qsizetype i = 0; i < data.size(); ++i) {
                quint8 c = data[i];
                auto pos = at + i;
                if (op == Operation::Patterns) {
                    for (int period = 1; period <= 8 && pos - start >= period; ++period) {
                        if (quint8(ring[(pos - period) % 8]) == c)
                            ++streak[period];
                        else
                            finishPattern(period, pos);
                    }
                    ring[pos % 8] = char(c);
                } else {
                    if (need && (c & 0xc0) != 0x80) {
                        flushString();
                        need = 0;
                        sequence.clear();
                    }
                    if (need) {
                        sequence += char(c);
                        if (--need == 0) {
                            if (sequence.isValidUtf8() &&
                                QChar::isPrint(QString::fromUtf8(sequence).toUcs4().first()))
                                printable(sequence, pos - sequence.size() + 1);
                            else
                                flushString();
                        }
                    } else if (c >= 32 && c < 127)
                        printable(QByteArrayView(data.constData() + i, 1), pos);
                    else if (c >= 0xc2 && c <= 0xf4) {
                        sequence = QByteArray(1, char(c));
                        need = c < 0xe0 ? 1 : c < 0xf0 ? 2 : 3;
                    } else
                        flushString();
                }
            }
        at += n;
    }
    if (op == Operation::Strings)
        flushString();
    if (op == Operation::Patterns)
        for (int period = 1; period <= 8; ++period)
            finishPattern(period, end);
    if (diff >= 0)
        add(diff, end - diff, "Difference");
    if (op == Operation::Statistics) {
        r.hash = hash.result().toHex();
        r.entropy = entropy(r.frequency, count);
        if (inGroup)
            r.regions << entropy(group, inGroup);
    }
    return r;
}
