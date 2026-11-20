#include "window.h"
#include <stdexcept>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(#x);                                                          \
    } while (false)
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QTemporaryDir tmp;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, tmp.path());
    app.setOrganizationName("test");
    app.setApplicationName("hexforge");
    QFile f(tmp.filePath("document.bin"));
    CHECK(f.open(QIODevice::WriteOnly));
    CHECK(f.write("ABCD") == 4);
    f.close();
    Window w;
    w.show();
    CHECK(w.open(f.fileName()));
    app.processEvents();
    auto e = w.editor();
    CHECK(e && !e->doc.writable);
    auto type = [&](QChar c) {
        QKeyEvent key(QEvent::KeyPress, c.unicode(), Qt::NoModifier, QString(c));
        QApplication::sendEvent(e, &key);
    };
    type('4');
    type('6');
    CHECK(e->doc.read(0, 4) == "ABCD");
    auto trigger = [&](QString label) {
        for (auto a : w.findChildren<QAction *>())
            if (a->text() == label) {
                a->trigger();
                return;
            }
        throw std::runtime_error("missing action");
    };
    trigger("Edit mode");
    type('4');
    type('6');
    CHECK(e->doc.read(0, 4) == "FBCD");
    trigger("Undo");
    CHECK(e->doc.read(0, 4) == "ABCD");
    trigger("Redo");
    CHECK(w.save(false));
    CHECK(!e->doc.writable);
    CHECK(f.open(QIODevice::ReadOnly));
    CHECK(f.readAll() == "FBCD");
    trigger("Analyze file");
    QElapsedTimer timer;
    timer.start();
    bool ready = false;
    while (!ready && timer.elapsed() < 5000) {
        app.processEvents();
        for (auto l : w.findChildren<QLabel *>())
            if (l->text().contains(QString::fromLatin1(
                    QCryptographicHash::hash("FBCD", QCryptographicHash::Sha256).toHex())))
                ready = true;
        QThread::msleep(2);
    }
    CHECK(ready);
    CHECK(w.close());
}
