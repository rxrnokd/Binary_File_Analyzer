#pragma once
#include "document.h"
#include <array>
#include <atomic>
enum class Operation { Statistics, Strings, Patterns, Search, Compare };
struct Finding {
    qint64 offset, length;
    QString text;
};
struct Report {
    QList<Finding> hits;
    std::array<quint64, 256> frequency{};
    QList<double> regions;
    double entropy = 0;
    QString hash, error;
    qint64 regionSize = 0, start = 0, count = 0;
    bool limited = false;
};
Report analyze(const Snapshot &, qint64 start, qint64 count, Operation, QByteArray query = {},
               Snapshot other = {}, const std::atomic_bool *cancel = nullptr);
