#include "configstore.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>

namespace {

QString errorText(const QString &prefix, const QString &detail)
{
    return detail.isEmpty() ? prefix : prefix + QStringLiteral("：") + detail;
}

} // namespace

QString ConfigStore::configPath() const
{
    return QDir(root_).filePath(QStringLiteral("config.json"));
}

QString ConfigStore::backupPath() const
{
    return configPath() + QStringLiteral(".bak");
}

bool ConfigStore::load(QString *error)
{
    QFile file(configPath());
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = errorText(QStringLiteral("无法读取 config.json"), file.errorString());
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) {
            *error = errorText(QStringLiteral("config.json 格式错误"), parseError.errorString());
        }
        return false;
    }
    object_ = document.object();
    return true;
}

bool ConfigStore::save(QString *error)
{
    const QString path = configPath();
    const QString backup = backupPath();
    if (QFileInfo::exists(path)) {
        QFile::remove(backup);
        if (!QFile::copy(path, backup)) {
            if (error) {
                *error = QStringLiteral("无法创建 config.json.bak");
            }
            return false;
        }
    }

    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = errorText(QStringLiteral("无法写入配置临时文件"), file.errorString());
        }
        return false;
    }
    const QByteArray data = QJsonDocument(object_).toJson(QJsonDocument::Indented);
    if (file.write(data) != data.size() || !file.commit()) {
        if (error) {
            *error = errorText(QStringLiteral("配置原子替换失败"), file.errorString());
        }
        return false;
    }
    return true;
}

bool ConfigStore::saveCommon(const QString &listenValue, const QString &apiKeyValue,
                             int maxInFlightValue, QString *error)
{
    const QString listenValueTrimmed = listenValue.trimmed();
    if (listenValueTrimmed.isEmpty() || !listenValueTrimmed.contains(QLatin1Char(':'))) {
        if (error) {
            *error = QStringLiteral("监听地址必须包含端口，例如 127.0.0.1:7863");
        }
        return false;
    }
    if (apiKeyValue.trimmed().isEmpty()) {
        if (error) {
            *error = QStringLiteral("API Key 不能为空");
        }
        return false;
    }
    if (maxInFlightValue < 0 || maxInFlightValue > 1000) {
        if (error) {
            *error = QStringLiteral("单账号并发上限必须在 0 到 1000 之间");
        }
        return false;
    }

    object_.insert(QStringLiteral("listen"), listenValueTrimmed);
    object_.insert(QStringLiteral("api_key"), apiKeyValue);
    QJsonObject pool = object_.value(QStringLiteral("pool")).toObject();
    pool.insert(QStringLiteral("max_in_flight"), maxInFlightValue);
    object_.insert(QStringLiteral("pool"), pool);
    return save(error);
}

QString ConfigStore::listen() const
{
    return object_.value(QStringLiteral("listen")).toString(QStringLiteral("127.0.0.1:7863"));
}

QString ConfigStore::apiKey() const
{
    return object_.value(QStringLiteral("api_key")).toString();
}

int ConfigStore::maxInFlight() const
{
    return object_.value(QStringLiteral("pool")).toObject()
        .value(QStringLiteral("max_in_flight")).toInt(3);
}

QUrl ConfigStore::baseUrl() const
{
    const QString listenValue = listen();
    const int separator = listenValue.lastIndexOf(QLatin1Char(':'));
    const QString port = separator >= 0 ? listenValue.mid(separator + 1) : QStringLiteral("7863");
    return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(port));
}
