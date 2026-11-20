#include "editor.h"
#include <QtWidgets>
#include <limits>
Editor::Editor(QWidget *parent) : QAbstractScrollArea(parent) {
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    setFocusPolicy(Qt::StrongFocus);
    setFrameShape(QFrame::NoFrame);
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int v) {
        top = lastRow() > INT_MAX ? qint64((long double)v / INT_MAX * lastRow()) : v;
        viewport()->update();
    });
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, viewport(),
            qOverload<>(&QWidget::update));
    connect(&doc.history, &QUndoStack::indexChanged, this, [this] {
        differences.clear();
        refresh();
    });
}
int Editor::cell() const { return fontMetrics().horizontalAdvance('0'); }
int Editor::line() const { return fontMetrics().height() + 7; }
int Editor::hexX() const { return (doc.size > 0xffffffffLL ? 19 : 11) * cell(); }
int Editor::asciiX() const { return hexX() + (3 * columns + 2) * cell(); }
qint64 Editor::lastRow() const {
    return std::max<qint64>(0, doc.size / columns - std::max(1, viewport()->height() / line() - 2) +
                                   1);
}
void Editor::scrollToRow(qint64 r) {
    top = std::clamp<qint64>(r, 0, lastRow());
    QSignalBlocker block(verticalScrollBar());
    verticalScrollBar()->setValue(lastRow() > INT_MAX ? int((long double)top / lastRow() * INT_MAX)
                                                      : int(top));
    viewport()->update();
}
void Editor::refresh() {
    cursor = std::min(cursor, doc.size);
    anchor = std::min(anchor, doc.size);
    verticalScrollBar()->setRange(0, int(std::min<qint64>(lastRow(), INT_MAX)));
    horizontalScrollBar()->setRange(
        0, std::max(0, asciiX() + (columns + 1) * cell() - viewport()->width()));
    scrollToRow(top);
    emit changed();
}
void Editor::select(qint64 at, bool extend) {
    cursor = std::clamp<qint64>(at, 0, doc.size);
    if (!extend)
        anchor = cursor;
    pending = -1;
    auto row = cursor / columns;
    int visible = std::max(1, viewport()->height() / line() - 1);
    if (row < top)
        scrollToRow(row);
    else if (row >= top + visible)
        scrollToRow(row - visible + 1);
    viewport()->update();
    emit changed();
}
void Editor::resizeEvent(QResizeEvent *e) {
    QAbstractScrollArea::resizeEvent(e);
    refresh();
}
void Editor::wheelEvent(QWheelEvent *e) {
    if (e->pixelDelta().y() || e->angleDelta().y()) {
        wheelRemainder +=
            e->pixelDelta().y() ? e->pixelDelta().y() : e->angleDelta().y() * line() / 40;
        scrollToRow(top - wheelRemainder / line());
        wheelRemainder %= line();
        e->accept();
    } else
        QAbstractScrollArea::wheelEvent(e);
}
void Editor::paintEvent(QPaintEvent *) {
    QPainter p(viewport());
    p.fillRect(viewport()->rect(), QColor("#11151b"));
    p.translate(-horizontalScrollBar()->value(), 0);
    int w = cell(), h = line(), rows = viewport()->height() / h + 1,
        base = fontMetrics().ascent() + 3;
    auto offset = top * columns;
    auto data = doc.read(offset, qint64(rows) * columns);
    p.setPen(QColor("#66cddd"));
    p.drawText(10, base, "OFFSET");
    for (int j = 0; j < columns; j++)
        p.drawText(hexX() + 3 * j * w, base, QString("%1").arg(j, 2, 16, QChar('0')).toUpper());
    p.drawText(asciiX(), base, "ASCII");
    for (int i = 0; i <= data.size(); ++i) {
        if (i > doc.size - offset)
            break;
        auto at = offset + i;
        int j = i % columns, y = (i / columns + 1) * h, x = hexX() + 3 * w * j,
            a = asciiX() + w * j;
        if (!j) {
            p.setPen(QColor("#73869c"));
            p.drawText(
                10, y + base,
                QString("%1").arg(at, doc.size > 0xffffffffLL ? 16 : 8, 16, QChar('0')).toUpper());
        }
        if (at >= first() && at <= std::max(cursor, anchor)) {
            p.fillRect(x - 1, y, 2 * w + 2, h, QColor("#22577a"));
            p.fillRect(a, y, w, h, QColor("#22577a"));
        }
        if (i == data.size()) {
            if (at == doc.size && at == cursor && pending >= 0) {
                p.setPen(QColor("#ffc677"));
                p.drawText(x, y + base, QString::number(pending, 16).toUpper() + "_");
            }
            break;
        }
        auto val = quint8(data[i]);
        auto it = std::upper_bound(differences.begin(), differences.end(), at,
                                   [](qint64 k, const auto &r) { return k < r.first; });
        bool diff = it != differences.begin() && at < (it - 1)->second;
        p.setPen(doc.modified(at) ? QColor("#ffc677")
                 : diff           ? QColor("#ff869e")
                                  : QColor("#d9e4ef"));
        p.drawText(x, y + base,
                   at == cursor && pending >= 0
                       ? QString::number(pending, 16).toUpper() + "_"
                       : QString("%1").arg(val, 2, 16, QChar('0')).toUpper());
        p.drawText(a, y + base, val >= 32 && val < 127 ? QChar(val) : QChar(0xb7));
    }
}
void Editor::hit(QPoint pos, bool extend) {
    int x = pos.x() + horizontalScrollBar()->value(), row = pos.y() / line() - 1;
    if (row < 0 || x < hexX())
        return;
    ascii = x >= asciiX();
    int col = ascii ? (x - asciiX()) / cell() : (x - hexX()) / (3 * cell());
    if (col < columns && top + row <= doc.size / columns)
        select((top + row) * columns + std::min<qint64>(col, doc.size - (top + row) * columns),
               extend);
}
void Editor::mousePressEvent(QMouseEvent *e) { hit(e->pos(), e->modifiers() & Qt::ShiftModifier); }
void Editor::mouseMoveEvent(QMouseEvent *e) {
    if (e->buttons() & Qt::LeftButton)
        hit(e->pos(), true);
}
void Editor::copy() {
    if (length() > 16 * 1024 * 1024) {
        emit notice("Use Extract for selections over 16 MiB");
        return;
    }
    auto bytes = doc.read(first(), length());
    auto mime = new QMimeData;
    mime->setData("application/octet-stream", bytes);
    mime->setText(ascii ? QString::fromLatin1(bytes) : QString::fromLatin1(bytes.toHex(' ')));
    QApplication::clipboard()->setMimeData(mime);
}
void Editor::put(QByteArray b) {
    if (b.isEmpty())
        return;
    auto at = first();
    auto n = cursor != anchor ? length()
             : insertMode     ? 0
                              : std::min<qint64>(b.size(), doc.size - at);
    if (doc.edit(at, n, b))
        select(at + b.size());
}
void Editor::paste() {
    auto m = QApplication::clipboard()->mimeData();
    if (!m)
        return;
    if (m->hasFormat("application/octet-stream"))
        put(m->data("application/octet-stream"));
    else if (ascii)
        put(m->text().toUtf8());
    else {
        auto s = m->text().remove(QRegularExpression("\\s+"));
        if (s.size() % 2 || s.contains(QRegularExpression("[^a-fA-F0-9]")))
            emit notice("Invalid hex bytes");
        else
            put(QByteArray::fromHex(s.toLatin1()));
    }
}
void Editor::erase() {
    auto at = first();
    if (doc.edit(at, length(), {}))
        select(at);
}
void Editor::keyPressEvent(QKeyEvent *e) {
    bool ext = e->modifiers() & Qt::ShiftModifier;
    switch (e->key()) {
    case Qt::Key_Left:
        select(cursor - 1, ext);
        return;
    case Qt::Key_Right:
        select(cursor < doc.size ? cursor + 1 : cursor, ext);
        return;
    case Qt::Key_Up:
        select(cursor - columns, ext);
        return;
    case Qt::Key_Down:
        select(cursor + std::min<qint64>(columns, doc.size - cursor), ext);
        return;
    case Qt::Key_Home:
        select(0, ext);
        return;
    case Qt::Key_End:
        select(doc.size, ext);
        return;
    case Qt::Key_Delete:
        erase();
        return;
    case Qt::Key_Backspace:
        if (cursor == anchor) {
            if (!cursor)
                return;
            if (doc.writable)
                select(cursor - 1);
        }
        erase();
        return;
    default:
        break;
    }
    if (!doc.writable || e->text().isEmpty() ||
        e->modifiers() & (Qt::ControlModifier | Qt::MetaModifier | Qt::AltModifier))
        return;
    if (ascii) {
        if (e->text().at(0).isPrint())
            put(e->text().toUtf8());
        return;
    }
    auto t = e->text().toLower();
    int v = QString("0123456789abcdef").indexOf(t);
    if (t.size() != 1 || v < 0)
        return;
    if (pending < 0) {
        pending = v;
        viewport()->update();
    } else {
        auto b = char(pending * 16 + v);
        pending = -1;
        put(QByteArray(1, b));
    }
}
