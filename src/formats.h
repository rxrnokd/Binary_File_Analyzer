#pragma once
#include "document.h"
struct Field {
    QString label;
    qint64 offset, length;
    QList<Field> children;
};
Field structure(const Snapshot &s);
quint64 integer(QByteArrayView bytes, bool little);
QStringList inspect(QByteArrayView bytes, bool little, int base);
// A passive template consists only of named, bounded byte ranges.
Field applyTemplate(const Snapshot &s, QByteArrayView json, QString &error);
