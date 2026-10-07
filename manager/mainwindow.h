#pragma once

#include "configstore.h"
#include "servicecontrol.h"
#include "taskrunner.h"

#include <QByteArray>
#include <QDateTime>
#include <QMainWindow>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSettings>
#include <QSystemTrayIcon>
#include <QUrl>
#include <QHash>
#include <QVector>

#include <functional>

class QCheckBox;
class QComboBox;
class QCloseEvent;
class QDoubleSpinBox;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QSystemTrayIcon;
class QTableWidget;
class QTimer;
class QLabel;

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void showAndActivate();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    using JsonCallback = std::function<void(bool, int, const QJsonObject &, const QString &)>;

    void setupUi();
    void setupTray();
    void setupStyles();
    void setupSettingsPage();
    void loadConfig();
    void loadManagerSettings();
    void saveManagerSettings();

    QWidget *createOverviewPage();
    QWidget *createAccountsPage();
    QWidget *createModelsPage();
    QWidget *createUsagePage();
    QWidget *createTasksPage();
    QWidget *createSettingsPage();
    QWidget *createChatPage();
    QWidget *createLogsPage();
    QWidget *createDiagnosticsPage();

    void refreshAll();
    void fetchStatus();
    void fetchModels();
    void fetchUsage();
    void fetchTaskHistory();
    void refreshTasksPage();
    void requestJson(const QString &method, const QString &path, const QByteArray &body,
                     const JsonCallback &callback);
    QNetworkRequest requestFor(const QUrl &url) const;

    void applyStatus(const QJsonObject &status);
    void applyModels(const QJsonArray &models);
    void applyUsage(const QJsonObject &usage);
    void handleGatewayFailure(const QString &endpoint, int status, const QString &error);
    void setGatewayState(bool online, const QString &message = QString());
    void maybeAutoRecover();

    void startService();
    void stopService();
    void restartService();
    void exitManager();
    void openVerification();
    void showLoginInstructions();
    void openLogsDirectory();
    void openConfigDirectory();
    void openAdvancedConfig();
    void openVerification(const QString &realm);
    void runLocalScript(const QString &script, const QStringList &arguments,
                        const QString &label);

    void enableSelectedAccount();
    void disableSelectedAccount();
    void deleteSelectedAccount();
    void reloginSelectedAccount();
    void importAccountJson();
    void performAccountAction(const QString &action, const QString &uid, const QByteArray &body = {});

    void exportUsageJson();
    void exportUsageCsv();
    void clearUsage();
    void saveConfigFromUi();
    void reloadConfigFromDisk();
    void restartAfterConfig();
    void triggerTask(const QString &task);
    void clearTaskHistory();
    void sendChatTest();
    void stopChatTest();
    void updateChatModels();
    void setSecretVisibility(bool visible);
    void loadAdvancedConfigFields();

    void refreshLogs();
    void runDiagnostics();
    void exportDiagnostics();
    void appendLog(const QString &text);
    void updateLogView();
    void notify(const QString &title, const QString &message,
                QSystemTrayIcon::MessageIcon icon = QSystemTrayIcon::Information);

    QString findProjectRoot() const;
    QString currentAccountUid() const;
    QString maskUid(const QString &uid) const;
    QString selectedTheme() const;
    void applyTheme(const QString &theme);
    void openPage(int index);

    QString root_;
    ConfigStore config_;
    ServiceControl service_;
    QSettings managerSettings_;
    QNetworkAccessManager *network_ = nullptr;
    QTimer *refreshTimer_ = nullptr;
    QTimer *logTimer_ = nullptr;
    QSystemTrayIcon *tray_ = nullptr;
    TaskRunner taskRunner_;
    QStackedWidget *stack_ = nullptr;
    QListWidget *navigation_ = nullptr;

    QUrl baseUrl_;
    QString apiKey_;
    QString configError_;
    QString lastPollError_;
    QString lastGatewayMessage_;
    bool gatewayOnline_ = false;
    bool allowQuit_ = false;
    bool exitAfterServiceStop_ = false;
    bool statusRequestPending_ = false;
    bool modelsRequestPending_ = false;
    bool usageRequestPending_ = false;
    bool recoveryInProgress_ = false;
    bool initialAutoStartAttempted_ = false;
    bool restartAfterStop_ = false;
    bool logsPaused_ = false;
    QList<QDateTime> recoveryTimes_;
    QStringList runtimeLogs_;
    QJsonObject lastUsage_;
    QJsonObject lastStatus_;
    QJsonArray lastModels_;
    QJsonArray lastTaskHistory_;

    QLabel *serviceBadge_ = nullptr;
    QLabel *endpointLabel_ = nullptr;
    QLabel *overviewService_ = nullptr;
    QLabel *overviewAccounts_ = nullptr;
    QLabel *overviewHealthy_ = nullptr;
    QLabel *overviewCooling_ = nullptr;
    QLabel *overviewModels_ = nullptr;
    QLabel *overviewRequests_ = nullptr;
    QLabel *overviewSuccess_ = nullptr;
    QLabel *overviewTokens_ = nullptr;
    QLabel *overviewHint_ = nullptr;

    QTableWidget *accountsTable_ = nullptr;
    QTableWidget *modelsTable_ = nullptr;
    QTableWidget *usageModelsTable_ = nullptr;
    QTableWidget *usageDaysTable_ = nullptr;
    QComboBox *usageRange_ = nullptr;
    QComboBox *modelRealm_ = nullptr;
    QLineEdit *modelSearch_ = nullptr;
    QLabel *usageRequests_ = nullptr;
    QLabel *usageSuccess_ = nullptr;
    QLabel *usageTokens_ = nullptr;
    QLabel *usageCredit_ = nullptr;

    QLineEdit *listenEdit_ = nullptr;
    QLineEdit *apiKeyEdit_ = nullptr;
    QSpinBox *maxInFlightSpin_ = nullptr;
    QSpinBox *maxBodyMbSpin_ = nullptr;
    QLineEdit *softRateEdit_ = nullptr;
    QLineEdit *softRateMaxEdit_ = nullptr;
    QVector<QCheckBox *> scheduleEnabledChecks_;
    QVector<QLineEdit *> scheduleHoursEdits_;
    QSpinBox *activityReportSpin_ = nullptr;
    QCheckBox *globalEnabledCheck_ = nullptr;
    QLineEdit *globalChatBaseEdit_ = nullptr;
    QLineEdit *globalBillingBaseEdit_ = nullptr;
    QSpinBox *timeoutSecondsSpin_ = nullptr;
    QSpinBox *headerTimeoutSecondsSpin_ = nullptr;
    QSpinBox *idleTimeoutSecondsSpin_ = nullptr;
    QLineEdit *userAgentEdit_ = nullptr;
    QLineEdit *clientVersionEdit_ = nullptr;
    QLineEdit *cliVersionEdit_ = nullptr;
    QLineEdit *deviceTokenEdit_ = nullptr;
    QLineEdit *deviceTokenFileEdit_ = nullptr;
    QLineEdit *clientNameEdit_ = nullptr;
    QCheckBox *passthroughIpCheck_ = nullptr;
    QCheckBox *sanitizeFingerprintsCheck_ = nullptr;
    QComboBox *promptModeCombo_ = nullptr;
    QLineEdit *promptFileEdit_ = nullptr;
    QPlainTextEdit *promptTextEdit_ = nullptr;
    QLineEdit *upstashUrlEdit_ = nullptr;
    QLineEdit *upstashTokenEdit_ = nullptr;
    QLabel *redisStatus_ = nullptr;
    QSpinBox *breakerThresholdSpin_ = nullptr;
    QLineEdit *breakerCooldownEdit_ = nullptr;
    QLineEdit *breakerCooldownMaxEdit_ = nullptr;
    QLineEdit *idleWeightPerHourEdit_ = nullptr;
    QLineEdit *idleWeightMaxEdit_ = nullptr;
    QLineEdit *expiringSoonEdit_ = nullptr;
    QCheckBox *sessionStickyCheck_ = nullptr;
    QLineEdit *sessionTtlEdit_ = nullptr;
    QLineEdit *sessionGcEdit_ = nullptr;
    QCheckBox *showSecretsCheck_ = nullptr;
    QLineEdit *proxyEdit_ = nullptr;
    QCheckBox *autoOpenGatewayCheck_ = nullptr;
    QCheckBox *autoRecoveryCheck_ = nullptr;
    QCheckBox *notificationsCheck_ = nullptr;
    QCheckBox *autoStartCheck_ = nullptr;
    QComboBox *themeCombo_ = nullptr;
    QLabel *settingsHint_ = nullptr;

    QTableWidget *tasksTable_ = nullptr;
    QTableWidget *taskHistoryTable_ = nullptr;
    QComboBox *taskHistoryFilter_ = nullptr;
    QPlainTextEdit *taskDetailView_ = nullptr;
    QHash<QString, QPushButton *> taskButtons_;

    QComboBox *chatModelCombo_ = nullptr;
    QPlainTextEdit *chatSystemEdit_ = nullptr;
    QPlainTextEdit *chatUserEdit_ = nullptr;
    QPlainTextEdit *chatResponseEdit_ = nullptr;
    QSpinBox *chatMaxTokensSpin_ = nullptr;
    QDoubleSpinBox *chatTemperatureSpin_ = nullptr;
    QCheckBox *chatStreamCheck_ = nullptr;
    QLabel *chatStatus_ = nullptr;
    QNetworkReply *chatReply_ = nullptr;
    QDateTime chatStartedAt_;
    QByteArray chatStreamBuffer_;
    QString chatAccumulated_;
    bool chatConfirmed_ = false;

    QPlainTextEdit *logView_ = nullptr;
    QLineEdit *logSearch_ = nullptr;
    QCheckBox *logErrorsOnly_ = nullptr;
    QPlainTextEdit *diagnosticsView_ = nullptr;
};
