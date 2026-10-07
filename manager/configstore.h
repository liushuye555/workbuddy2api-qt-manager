#pragma once

#include <QJsonObject>
#include <QString>
#include <QUrl>

class ConfigStore final {
public:
    ConfigStore() = default;
    explicit ConfigStore(const QString &root) : root_(root) {}

    void setRoot(const QString &root) { root_ = root; }
    QString root() const { return root_; }
    QString configPath() const;
    QString backupPath() const;

    bool load(QString *error = nullptr);
    bool save(QString *error = nullptr);
    bool saveCommon(const QString &listen, const QString &apiKey, int maxInFlight,
                    QString *error = nullptr);

    const QJsonObject &object() const { return object_; }
    QJsonObject &object() { return object_; }
    QString listen() const;
    QString apiKey() const;
    int maxInFlight() const;
    QUrl baseUrl() const;

private:
    QString root_;
    QJsonObject object_;
};
