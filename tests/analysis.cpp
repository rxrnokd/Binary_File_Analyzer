#include "analysis.h"
#include "formats.h"
#include <QTemporaryDir>
#include <stdexcept>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(#x);                                                          \
    } while (false)
int main() {
    QTemporaryDir tmp;
    auto open = [&](Document &d, QString name, QByteArray bytes) {
        QFile f(tmp.filePath(name));
        CHECK(f.open(QIODevice::WriteOnly));
        CHECK(f.write(bytes) == bytes.size());
        f.close();
        CHECK(d.open(f.fileName()));
    };
    Document d, b;
    open(d, "a", QByteArray(65535, 'A') + "XY" + QByteArray::fromHex("ed959c") + "ab");
    open(b, "b", "ABC");
    auto search = analyze(d, 0, d.size, Operation::Search, "XY");
    CHECK(search.hits.size() == 1 && search.hits.first().offset == 65535);
    auto strings = analyze(d, 0, d.size, Operation::Strings);
    CHECK(strings.hits.size() == 1 && strings.hits[0].length == d.size &&
          strings.hits[0].text.startsWith("UTF-8"));
    auto diff = analyze(d, 0, d.size, Operation::Compare, {}, b);
    CHECK(diff.hits.size() == 1 && diff.hits[0].offset == 1 && diff.hits[0].length == d.size - 1);
    auto stats = analyze(d, 0, 65535, Operation::Statistics);
    CHECK(stats.entropy == 0 && stats.frequency[65] == 65535);
    open(b, "small", "ab");
    CHECK(analyze(b, 0, 2, Operation::Statistics).entropy == 1);
    auto patterns = analyze(d, 0, d.size, Operation::Patterns);
    CHECK(patterns.hits.first().length == 65535);
    for (auto item : QList<QPair<QString, QString>>{{"feedfacf", "Mach-O 64-bit"},
                                                    {"cafebabe", "Mach-O universal"},
                                                    {"7f454c46", "ELF"},
                                                    {"ffd8", "JPEG"},
                                                    {"504b0304", "ZIP"},
                                                    {"89504e470d0a1a0a", "PNG"}}) {
        open(b, "fmt" + item.first, QByteArray::fromHex(item.first.toLatin1()) + 'x');
        auto tree = structure(b);
        CHECK(tree.label == item.second);
        CHECK(!tree.children.empty());
    }
    QByteArray pe(128, 0);
    pe.replace(0, 2, "MZ");
    pe[60] = 64;
    pe.replace(64, 2, "PE");
    open(b, "portable", pe);
    CHECK(structure(b).label == "PE");
    open(b, "periodic", QByteArray::fromHex("00abcdabcdabcdabcdff"));
    auto repeats = analyze(b, 0, b.size, Operation::Patterns);
    CHECK(repeats.hits.size() == 1 && repeats.hits[0].offset == 1 && repeats.hits[0].length == 8);
    QFile large(tmp.filePath("sparse"));
    CHECK(large.open(QIODevice::WriteOnly));
    CHECK(large.resize(5LL << 30));
    CHECK(large.seek(large.size() - 1));
    CHECK(large.write("Z") == 1);
    large.close();
    CHECK(b.open(large.fileName()));
    CHECK(b.read((5LL << 30) - 1, 8) == "Z");
}
