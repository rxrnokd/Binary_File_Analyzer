#pragma once
#include "document.h"
#include <QAbstractScrollArea>
class Editor final : public QAbstractScrollArea {
    Q_OBJECT
  public:
    Document doc;
    QByteArray structureTemplate;
    int columns = 16;
    bool insertMode = false, ascii = false;
    qint64 cursor = 0, anchor = 0;
    QList<QPair<qint64, qint64>> differences;
    explicit Editor(QWidget *parent = nullptr);
    qint64 first() const { return std::min(anchor, cursor); }
    qint64 length() const {
        return doc.size ? std::min(doc.size - 1, std::max(anchor, cursor)) - first() + 1 : 0;
    }
    void select(qint64 at, bool extend = false);
    void refresh();
    void copy();
    void paste();
    void erase();
  signals:
    void changed();
    void notice(QString);

  protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;

  private:
    int pending = -1, wheelRemainder = 0;
    qint64 top = 0;
    int cell() const;
    int line() const;
    int hexX() const;
    int asciiX() const;
    qint64 lastRow() const;
    void scrollToRow(qint64);
    void hit(QPoint, bool);
    void put(QByteArray);
};
