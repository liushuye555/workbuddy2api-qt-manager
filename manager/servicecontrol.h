#pragma once

#include <QProcess>
#include <QString>

#include <functional>

class ServiceControl final {
public:
    ServiceControl() = default;
    explicit ServiceControl(const QString &root) : root_(root) {}

    void setRoot(const QString &root) { root_ = root; }
    QString root() const { return root_; }
    bool busy() const { return process_ && process_->state() != QProcess::NotRunning; }

    void setLogCallback(std::function<void(const QString &)> callback) { logCallback_ = std::move(callback); }
    void setFinishedCallback(std::function<void(const QString &, bool)> callback)
    {
        finishedCallback_ = std::move(callback);
    }

    bool start();
    bool stop();
    bool restart();
    void terminate();

    static QString findPowerShell();
    static QString findMintty();

private:
    bool runScript(const QString &scriptName, const QString &action);
    void ensureProcess();
    void log(const QString &line) const;

    QString root_;
    QProcess *process_ = nullptr;
    QString action_;
    std::function<void(const QString &)> logCallback_;
    std::function<void(const QString &, bool)> finishedCallback_;
};
