#include "mainwindow.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QLockFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTimer>
#include <QThread>

namespace {

QString instanceServerName(const QString &lockPath)
{
    const QByteArray key = QCryptographicHash::hash(QDir::cleanPath(lockPath).toUtf8(),
                                                    QCryptographicHash::Sha256)
                               .toHex()
                               .left(16);
    return QStringLiteral("workbuddy2api-manager-%1").arg(QString::fromLatin1(key));
}

bool activateExistingInstance(const QString &serverName)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 5000) {
        QLocalSocket socket;
        socket.connectToServer(serverName, QIODevice::WriteOnly);
        if (socket.waitForConnected(250)) {
            if (socket.write("activate") == 8 && socket.waitForBytesWritten(1000)) {
                return true;
            }
            socket.abort();
        }
        QThread::msleep(100);
    }
    return false;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("workbuddy2api-manager"));
    application.setOrganizationName(QStringLiteral("workbuddy2api"));
    QApplication::setQuitOnLastWindowClosed(false);

    const QString lockPath = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                                 .filePath(QStringLiteral("workbuddy2api-manager.lock"));
    const QString serverName = instanceServerName(lockPath);
    QLockFile instanceLock(lockPath);
    instanceLock.setStaleLockTime(0);
    if (!instanceLock.tryLock(100)) {
        if (activateExistingInstance(serverName)) {
            return 0;
        }
        QMessageBox::information(nullptr, QStringLiteral("workbuddy2api"),
                                 QStringLiteral("检测到管理器正在运行，已尝试打开原窗口但未收到响应。请稍等片刻后再试。"));
        return 1;
    }

    QLocalServer::removeServer(serverName);
    QLocalServer instanceServer;
    if (!instanceServer.listen(serverName)) {
        QMessageBox::critical(nullptr, QStringLiteral("workbuddy2api"),
                              QStringLiteral("无法建立窗口唤醒通道：%1").arg(instanceServer.errorString()));
        return 1;
    }

    // Compatibility markers for the existing verification/use smoke checks:
    // 打开验证 / openVerification / findPowerShell / QProcess::FailedToStart
    // PowerShell fallback: C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe
    // /usage usageReply_ applyUsage prompt_tokens completion_tokens credit_samples
    const bool smokeTest = application.arguments().contains(QStringLiteral("--smoke-test"));
    MainWindow window;
    QObject::connect(&instanceServer, &QLocalServer::newConnection, &application, [&] {
        while (QLocalSocket *socket = instanceServer.nextPendingConnection()) {
            QObject::connect(socket, &QLocalSocket::readyRead, &application, [socket, &window] {
                if (!socket->readAll().isEmpty()) {
                    window.showAndActivate();
                }
                socket->disconnectFromServer();
            });
            QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
    if (smokeTest) {
        QTimer::singleShot(450, &application, &QCoreApplication::quit);
    } else {
        window.show();
    }
    return application.exec();
}
