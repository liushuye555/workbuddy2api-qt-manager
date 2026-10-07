#include "servicecontrol.h"

#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QStandardPaths>

namespace {

QString quotePowerShell(const QString &value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('\''), QStringLiteral("''"));
    return QStringLiteral("'%1'").arg(escaped);
}

} // namespace

void ServiceControl::ensureProcess()
{
    if (process_) {
        return;
    }
    process_ = new QProcess;
    process_->setProcessChannelMode(QProcess::MergedChannels);
    QObject::connect(process_, &QProcess::readyRead, [this] {
        const QString output = QString::fromLocal8Bit(process_->readAll()).trimmed();
        if (!output.isEmpty()) {
            log(output);
        }
    });
    QObject::connect(process_, &QProcess::errorOccurred, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            log(QStringLiteral("无法启动 PowerShell 服务控制脚本"));
        }
    });
    QObject::connect(process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                     [this](int exitCode, QProcess::ExitStatus status) {
                         const bool ok = status == QProcess::NormalExit && exitCode == 0;
                         const QString action = action_;
                         log(QStringLiteral("%1 %2（退出码 %3）")
                                 .arg(action, ok ? QStringLiteral("完成") : QStringLiteral("失败"))
                                 .arg(exitCode));
                         if (finishedCallback_) {
                             finishedCallback_(action, ok);
                         }
                         process_->deleteLater();
                         process_ = nullptr;
                     });
}

bool ServiceControl::runScript(const QString &scriptName, const QString &action)
{
    if (busy()) {
        log(QStringLiteral("已有服务操作正在执行，请稍候"));
        return false;
    }
    const QString powershell = findPowerShell();
    const QString script = QDir(root_).filePath(scriptName);
    if (powershell.isEmpty() || !QFileInfo::exists(script)) {
        log(QStringLiteral("找不到 PowerShell 或脚本：%1").arg(script));
        return false;
    }
    ensureProcess();
    action_ = action;
    process_->setWorkingDirectory(root_);
    process_->start(powershell, {QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"),
                                 QStringLiteral("Bypass"), QStringLiteral("-File"), script});
    return true;
}

bool ServiceControl::start()
{
    return runScript(QStringLiteral("start.ps1"), QStringLiteral("启动服务"));
}

bool ServiceControl::stop()
{
    return runScript(QStringLiteral("stop.ps1"), QStringLiteral("停止服务"));
}

bool ServiceControl::restart()
{
    if (busy()) {
        log(QStringLiteral("已有服务操作正在执行，请稍候"));
        return false;
    }
    const QString powershell = findPowerShell();
    const QString stopScript = QDir(root_).filePath(QStringLiteral("stop.ps1"));
    const QString startScript = QDir(root_).filePath(QStringLiteral("start.ps1"));
    if (powershell.isEmpty() || !QFileInfo::exists(stopScript) || !QFileInfo::exists(startScript)) {
        log(QStringLiteral("找不到重启所需脚本"));
        return false;
    }
    ensureProcess();
    action_ = QStringLiteral("重启服务");
    process_->setWorkingDirectory(root_);
    const QString command = QStringLiteral("& %1; & %2")
                                .arg(quotePowerShell(stopScript), quotePowerShell(startScript));
    process_->start(powershell, {QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"),
                                 QStringLiteral("Bypass"), QStringLiteral("-Command"), command});
    return true;
}

void ServiceControl::terminate()
{
    if (process_ && process_->state() != QProcess::NotRunning) {
        process_->kill();
    }
}

QString ServiceControl::findPowerShell()
{
    const QStringList candidates = {
        QStandardPaths::findExecutable(QStringLiteral("powershell.exe")),
        QStringLiteral("C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe"),
        QStringLiteral("C:/Windows/SysWOW64/WindowsPowerShell/v1.0/powershell.exe")};
    for (const QString &candidate : candidates) {
        if (!candidate.isEmpty() && QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return QString();
}

QString ServiceControl::findMintty()
{
    const QStringList candidates = {
        QStringLiteral("C:/Program Files/Git/usr/bin/mintty.exe"),
        QStringLiteral("C:/Program Files (x86)/Git/usr/bin/mintty.exe"),
        QStandardPaths::findExecutable(QStringLiteral("mintty.exe"))};
    for (const QString &candidate : candidates) {
        if (!candidate.isEmpty() && QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return QString();
}

void ServiceControl::log(const QString &line) const
{
    if (logCallback_ && !line.trimmed().isEmpty()) {
        logCallback_(line.trimmed());
    }
}
