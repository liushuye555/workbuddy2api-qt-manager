#pragma once

#include <QJsonObject>
#include <QString>

class LogDiagnostics final {
public:
    static QString sanitize(const QString &input);
    static QString readTail(const QString &path, qint64 maxBytes = 16000);
    static QString localReport(const QString &root, const QJsonObject &config);
};
