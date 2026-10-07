#include "taskrunner.h"

#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QStandardPaths>

namespace {

QString shellQuote(const QString &value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QStringLiteral("'%1'").arg(escaped);
}

QString bashPathValue(const QString &root)
{
    QString path = QDir::fromNativeSeparators(QDir::cleanPath(root));
    if (path.size() >= 3 && path.at(1) == QLatin1Char(':') && path.at(2) == QLatin1Char('/')) {
        path = QStringLiteral("/") + path.at(0).toLower() + path.mid(2);
    }
    return path;
}

} // namespace

TaskRunner::~TaskRunner()
{
    terminate();
}

void TaskRunner::ensureProcess()
{
    if (process_) {
        return;
    }
    process_ = new QProcess;
    process_->setProcessChannelMode(QProcess::MergedChannels);
    QObject::connect(process_, &QProcess::readyRead, [this] {
        const QByteArray bytes = process_->readAll();
        // Git Bash/Go tools emit UTF-8 even on Windows. Decoding this stream
        // with the Windows local code page turns Chinese output into mojibake.
        const QString decoded = QString::fromUtf8(bytes);
        output_ += decoded;
        const QString output = decoded.trimmed();
        if (!output.isEmpty()) {
            log(output);
        }
    });
    QObject::connect(process_, &QProcess::errorOccurred, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            log(QStringLiteral("脚本进程无法启动，请确认 Git Bash 与脚本依赖已安装"));
        }
    });
    QObject::connect(process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                     [this](int exitCode, QProcess::ExitStatus status) {
                         const QByteArray tail = process_->readAll();
                         const QString decodedTail = QString::fromUtf8(tail);
                         output_ += decodedTail;
                         const QString tailText = decodedTail.trimmed();
                         if (!tailText.isEmpty()) {
                             log(tailText);
                         }
                         const bool ok = status == QProcess::NormalExit && exitCode == 0;
                         const QString action = action_;
                         log(QStringLiteral("%1 %2（退出码 %3）")
                                 .arg(action, ok ? QStringLiteral("完成") : QStringLiteral("失败"))
                                 .arg(exitCode));
                         if (finishedCallback_) {
                             finishedCallback_(action, ok, exitCode);
                         }
                         process_->deleteLater();
                         process_ = nullptr;
                     });
}

bool TaskRunner::runScript(const QString &script, const QStringList &arguments,
                           const QString &label, const QString &proxy)
{
    if (busy()) {
        log(QStringLiteral("已有脚本任务正在执行，请稍候"));
        return false;
    }
    const QString bash = findBash();
    const QString scriptPath = QDir(root_).filePath(script);
    if (bash.isEmpty() || !QFileInfo::exists(scriptPath)) {
        log(QStringLiteral("找不到 Git Bash 或脚本：%1").arg(scriptPath));
        return false;
    }
    ensureProcess();
    action_ = label;
    output_.clear();
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    const QString proxyValue = proxy.trimmed().isEmpty() ? QStringLiteral("127.0.0.1:7897") : proxy.trimmed();
    const QString proxyUrl = proxyValue.startsWith(QStringLiteral("http://"))
                                 ? proxyValue
                                 : QStringLiteral("http://%1").arg(proxyValue);
    environment.insert(QStringLiteral("HTTP_PROXY"), proxyUrl);
    environment.insert(QStringLiteral("HTTPS_PROXY"), proxyUrl);
    environment.insert(QStringLiteral("ALL_PROXY"), proxyUrl);
    environment.insert(QStringLiteral("NO_PROXY"), QStringLiteral("127.0.0.1,localhost"));
    // 本地部署的 Go 工具链若不在用户 PATH 中，给脚本一个可覆盖的入口。
    const QStringList bundledGoCandidates = {
        QStringLiteral("C:/Users/eryis/AppData/Local/Temp/workbuddy2api-go/go-full/go/bin"),
        QStringLiteral("C:/Users/eryis/AppData/Local/Temp/workbuddy2api-go/go/bin")};
    for (const QString &bundledGo : bundledGoCandidates) {
        const QString goExecutable = QDir(bundledGo).filePath(QStringLiteral("go.exe"));
        if (QFileInfo::exists(goExecutable)) {
            environment.insert(QStringLiteral("PATH"), bundledGo + QDir::listSeparator()
                                                 + environment.value(QStringLiteral("PATH")));
            environment.insert(QStringLiteral("WB2A_GO"), goExecutable);
            break;
        }
    }
    const QString goOnPath = QStandardPaths::findExecutable(QStringLiteral("go.exe"));
    if (!environment.contains(QStringLiteral("WB2A_GO")) && !goOnPath.isEmpty()) {
        environment.insert(QStringLiteral("WB2A_GO"), goOnPath);
    }
    const QString python = QStandardPaths::findExecutable(QStringLiteral("python.exe"));
    if (!python.isEmpty() && QFileInfo::exists(python)) {
        environment.insert(QStringLiteral("WB2A_PYTHON"), python);
    }
    process_->setProcessEnvironment(environment);
    process_->setWorkingDirectory(root_);
    QStringList bashArguments{QStringLiteral("--noprofile"), QStringLiteral("--norc"),
                              QStringLiteral("./%1").arg(script)};
    bashArguments += arguments;
    process_->start(bash, bashArguments);
    return true;
}

bool TaskRunner::openInteractive(const QString &script, const QStringList &arguments,
                                 const QString &title, const QString &proxy)
{
    const QString mintty = findMintty();
    if (mintty.isEmpty() || !QFileInfo::exists(QDir(root_).filePath(script))) {
        log(QStringLiteral("无法打开交互式验证：找不到 Git Bash 或 %1").arg(script));
        return false;
    }
    const QString rootPath = shellQuote(bashPathValue(root_));
    QStringList commandParts;
    commandParts << QStringLiteral("cd %1").arg(rootPath);
    const QString proxyValue = proxy.trimmed().isEmpty() ? QStringLiteral("127.0.0.1:7897") : proxy.trimmed();
    const QString proxyUrl = proxyValue.startsWith(QStringLiteral("http://"))
                                 ? proxyValue
                                 : QStringLiteral("http://%1").arg(proxyValue);
    commandParts << QStringLiteral("export HTTP_PROXY=%1 HTTPS_PROXY=%1 ALL_PROXY=%1 NO_PROXY=127.0.0.1,localhost WB2A_OPEN_BROWSER=1")
                        .arg(shellQuote(proxyUrl));
    QString command = QStringLiteral("./%1").arg(script);
    for (const QString &argument : arguments) {
        command += QLatin1Char(' ') + shellQuote(argument);
    }
    commandParts << command;
    commandParts << QStringLiteral("rc=$?");
    commandParts << QStringLiteral("echo");
    commandParts << QStringLiteral("if [ $rc -eq 0 ]; then echo '流程已完成，请回到管理器刷新状态。'; else echo '流程未完成，请根据上方错误重试。'; fi");
    commandParts << QStringLiteral("read -r -p '按回车关闭此窗口...' _");
    const QString shellCommand = commandParts.join(QStringLiteral("; "));
    const QStringList minttyArguments = {QStringLiteral("--title=%1").arg(title),
                                         QStringLiteral("--dir=%1").arg(bashPathValue(root_)),
                                         QStringLiteral("/usr/bin/bash"), QStringLiteral("-li"),
                                         QStringLiteral("-c"), shellCommand};
    qint64 pid = 0;
    if (!QProcess::startDetached(mintty, minttyArguments, root_, &pid)) {
        log(QStringLiteral("Git Bash 交互窗口启动失败"));
        return false;
    }
    log(QStringLiteral("%1 已打开（PID=%2）").arg(title).arg(pid));
    return true;
}

void TaskRunner::terminate()
{
    if (process_ && process_->state() != QProcess::NotRunning) {
        process_->kill();
    }
}

QString TaskRunner::findBash()
{
    const QStringList candidates = {
        QStringLiteral("C:/Program Files/Git/bin/bash.exe"),
        QStringLiteral("C:/Program Files (x86)/Git/bin/bash.exe"),
        QStandardPaths::findExecutable(QStringLiteral("bash.exe"))};
    for (const QString &candidate : candidates) {
        if (!candidate.isEmpty() && QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return QString();
}

QString TaskRunner::findMintty()
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

void TaskRunner::log(const QString &line) const
{
    if (logCallback_ && !line.trimmed().isEmpty()) {
        logCallback_(line.trimmed());
    }
}
