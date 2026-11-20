#include "document.h"
#include "formats.h"
#include <QTemporaryDir>
#include <stdexcept>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(#x);                                                          \
    } while (false)
int main() {
    QTemporaryDir d;
    QString name = d.filePath("input");
    QFile file(name);
    CHECK(file.open(QIODevice::WriteOnly));
    CHECK(file.write("abcd") == 4);
    file.close();
    Document doc;
    CHECK(doc.open(name));
    CHECK(!doc.edit(0, 1, "!"));
    doc.writable = true;
    CHECK(doc.edit(1, 2, "XYZ"));
    CHECK(doc.read(0, 9) == "aXYZd");
    doc.history.undo();
    CHECK(doc.read(0, 9) == "abcd");
    doc.history.redo();
    CHECK(!doc.save(d.filePath("missing/out")));
    CHECK(file.open(QIODevice::ReadOnly));
    CHECK(file.readAll() == "abcd");
    file.close();
    CHECK(doc.save(name));
    CHECK(doc.read(0, 99) == "aXYZd");
    CHECK(!doc.extract(name, 0, 1));
    CHECK(doc.extract(d.filePath("range"), 1, 2));
    doc.writable = true;
    CHECK(!doc.edit(-1, 0, "A") && !doc.edit(doc.size, 1, "B"));
    CHECK(doc.edit(doc.size, 0, "!") && doc.edit(0, 1, {}));
    CHECK(doc.read(0, 9) == "XYZd!");
    CHECK(file.open(QIODevice::Append));
    CHECK(file.write("external") == 8);
    file.close();
    CHECK(!doc.save(name));
    CHECK(file.open(QIODevice::ReadOnly));
    CHECK(file.readAll() == "aXYZdexternal");
    CHECK(integer(QByteArray::fromHex("1234"), true) == 0x3412);
    CHECK(inspect(QByteArray::fromHex("ff"), true, 10).last().contains("Invalid"));
    CHECK(structure(doc).label == "Unknown");
}
