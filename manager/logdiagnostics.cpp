#include "logdiagnostics.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QRegularExpression>

QString LogDiagnostics::sanitize(const QString &input)
{
    QString output = input;
    output.replace(QRegularExpression(QStringLiteral("(?i)(bearer\\s+)[^\\s\\r\\n]+")),
                   QStringLiteral("\\1<hidden>"));
    output.replace(QRegularExpression(QStringLiteral(
                       "(?i)(\\\"?(?:access[_-]?token|refresh[_-]?token|device[_-]?token|api[_-]?key)\\\"?\\s*[:=]\\s*\\\"?)[^\\\"\\s,}]+")),
                   QStringLiteral("\\1<hidden>"));
    output.replace(QRegularExpression(QStringLiteral("(?i)(password\\s*[=:]\\s*)[^\\s]+")),
                   QStringLiteral("\\1<hidden>"));
    return output;
}

QString LogDiagnostics::readTail(const QString &path, qint64 maxBytes)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    QByteArray data = file.readAll();
    if (data.size() > maxBytes) {
        data = data.right(maxBytes);
    }
    return sanitize(QString::fromUtf8(data));
}

QString LogDiagnostics::localReport(const QString &root, const QJsonObject &config)
{
    const QString authDir = config.value(QStringLiteral("auth_dir")).toString(QStringLiteral("./auths"));
    const QString stateFile = config.value(QStringLiteral("state_file")).toString(QStringLiteral("./data/state.json"));
    const QString usageFile = QDir(root).filePath(QStringLiteral("data/usage.json"));
    QStringList lines;
    lines << QStringLiteral("workbuddy2api 脱敏诊断报告")
          << QStringLiteral("生成时间：%1").arg(QDateTime::currentDateTime().toString(Qt::ISODate))
          << QStringLiteral("项目目录：%1").arg(root)
          << QStringLiteral("配置文件：%1").arg(QFileInfo(QDir(root).filePath(QStringLiteral("config.json"))).exists()
                                                    ? QStringLiteral("存在")
                                                    : QStringLiteral("缺失"))
          << QStringLiteral("账号目录：%1").arg(QFileInfo(QDir(root).filePath(authDir)).isDir()
                                                    ? QStringLiteral("存在")
                                                    : QStringLiteral("缺失"))
          << QStringLiteral("状态文件：%1").arg(QFileInfo(QDir(root).filePath(stateFile)).exists()
                                                    ? QStringLiteral("存在")
                                                    : QStringLiteral("缺失"))
          << QStringLiteral("用量文件：%1").arg(QFileInfo(usageFile).exists() ? QStringLiteral("存在")
                                                                            : QStringLiteral("尚未生成"))
          << QStringLiteral("代理：127.0.0.1:7897（由启动脚本注入）")
          << QStringLiteral("API Key：已隐藏")
          << QStringLiteral("凭证内容：未读取");
    return sanitize(lines.join(QStringLiteral("\n")));
}
