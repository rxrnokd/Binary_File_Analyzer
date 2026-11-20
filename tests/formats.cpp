#include "formats.h"
#include <QTemporaryDir>
#include <stdexcept>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(#x);                                                          \
    } while (false)
int main(int argc, char **argv) {
    CHECK(argc == 2);
    Document doc;
    CHECK(doc.open(argv[1]));
    auto root = structure(doc);
    CHECK(root.label == "Mach-O 64-bit");
    auto section = root.children.last().children.first();
    CHECK(section.label == "__sample");
    auto payload = section.children.first();
    CHECK(payload.offset == 184 && payload.length == doc.size - 184);
    auto thin = doc.read(0, doc.size);
    QByteArray fat = QByteArray::fromHex("cafebabe000000010100000c0000000000000020");
    auto len = quint32(thin.size());
    for (int shift : {24, 16, 8, 0})
        fat.append(char(len >> shift));
    fat += QByteArray(8, 0);
    doc.writable = true;
    CHECK(doc.edit(0, doc.size, fat + thin));
    root = structure(doc);
    CHECK(root.label == "Mach-O universal");
    CHECK(root.children.size() == 1);
    CHECK(root.children.first().children.last().children.first().children.first().offset == 216);
    CHECK(doc.edit(16, 4, QByteArray(4, char(255))));
    CHECK(structure(doc).children.first().label.contains("invalid"));
    CHECK(inspect(QByteArray::fromHex("fffffffe"), false, 10).contains("int32\t-2"));
    CHECK(inspect(QByteArray::fromHex("0000803f"), true, 10).contains("float32\t1"));
    CHECK(inspect(QByteArray::fromHex("f09f"), false, 10).last().contains("Truncated"));
    QString error;
    auto fields =
        applyTemplate(doc, R"({"fields":[{"name":"tail","offset":65535,"length":2}]})", error);
    CHECK(error.isEmpty() && fields.children.first().length == 0);
    applyTemplate(doc, "{", error);
    CHECK(!error.isEmpty());
}
