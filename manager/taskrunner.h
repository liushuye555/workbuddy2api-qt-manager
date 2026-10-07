#pragma once

#include <QProcess>
#include <QProcessEnvironment>
#include <QString>

#include <functional>

// 管理器中的本地脚本执行器：日报/Trial 使用可收集输出的 Git Bash 进程，
// OAuth 使用独立 mintty 窗口，避免把交互式授权卡在 Qt 主窗口里。
class TaskRunner final {
public:
    using LogCallback = std::function<void(const QString &)>;
    using FinishedCallback = std::function<void(const QString &, bool, int)>;

    TaskRunner() = default;
    ~TaskRunner();

    void setRoot(const QString &root) { root_ = root; }
    QString root() const { return root_; }
    QString output() const { return output_; }
    bool busy() const { return process_ && process_->state() != QProcess::NotRunning; }
    void setLogCallback(LogCallback callback) { logCallback_ = std::move(callback); }
    void setFinishedCallback(FinishedCallback callback) { finishedCallback_ = std::move(callback); }

    bool runScript(const QString &script, const QStringList &arguments,
                   const QString &label, const QString &proxy = QString());
    bool openInteractive(const QString &script, const QStringList &arguments,
                         const QString &title, const QString &proxy = QString());
    void terminate();

    static QString findBash();
    static QString findMintty();

private:
    void ensureProcess();
    void log(const QString &line) const;

    QString root_;
    QProcess *process_ = nullptr;
    QString action_;
    QString output_;
    LogCallback logCallback_;
    FinishedCallback finishedCallback_;
};
