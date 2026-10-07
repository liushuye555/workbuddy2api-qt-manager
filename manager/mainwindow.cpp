#include "mainwindow.h"

#include "logdiagnostics.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollBar>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTableWidget>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QJsonDocument>

#include <algorithm>

namespace {

QString jsonString(const QJsonObject &object, const QString &key)
{
    return object.value(key).toString();
}

qint64 jsonInt64(const QJsonObject &object, const QString &key)
{
    return static_cast<qint64>(object.value(key).toDouble());
}

QString countText(qint64 value)
{
    return QLocale().toString(value);
}

QString optionalCount(const QJsonObject &object, const QString &valueKey, const QString &samplesKey)
{
    return jsonInt64(object, samplesKey) > 0 ? countText(jsonInt64(object, valueKey))
                                             : QStringLiteral("—");
}

QString creditText(const QJsonObject &object)
{
    if (jsonInt64(object, QStringLiteral("credit_samples")) <= 0) {
        return QStringLiteral("—");
    }
    return QString::number(object.value(QStringLiteral("credit")).toDouble(), 'f', 2);
}

QString successRate(const QJsonObject &object)
{
    const qint64 requests = jsonInt64(object, QStringLiteral("requests"));
    if (requests <= 0) {
        return QStringLiteral("—");
    }
    const double successes = static_cast<double>(jsonInt64(object, QStringLiteral("successes")));
    return QStringLiteral("%1%").arg(QString::number(successes * 100.0 / requests, 'f', 1));
}

QString formatTime(const QString &value)
{
    const QDateTime time = QDateTime::fromString(value, Qt::ISODate);
    return time.isValid() ? time.toLocalTime().toString(QStringLiteral("MM-dd HH:mm")) : QString();
}

QString stateForAccount(const QJsonObject &account, int maxInFlight)
{
    if (account.value(QStringLiteral("disabled")).toBool()) {
        return QStringLiteral("已禁用");
    }
    if (account.value(QStringLiteral("cooling")).toBool()) {
        return QStringLiteral("冷却中");
    }
    if (maxInFlight > 0 && account.value(QStringLiteral("in_flight")).toInt() >= maxInFlight) {
        return QStringLiteral("满载");
    }
    return QStringLiteral("可用");
}

QStringList modelCapabilities(const QJsonObject &model)
{
    QStringList capabilities;
    if (model.value(QStringLiteral("supports_images")).toBool()) {
        capabilities << QStringLiteral("图片");
    }
    if (model.value(QStringLiteral("supports_reasoning")).toBool()) {
        capabilities << QStringLiteral("推理");
    }
    if (model.value(QStringLiteral("supports_tool_call")).toBool()) {
        capabilities << QStringLiteral("工具");
    }
    if (model.value(QStringLiteral("is_default")).toBool()) {
        capabilities << QStringLiteral("默认");
    }
    const QJsonArray efforts = model.value(QStringLiteral("reasoning_supported_efforts")).toArray();
    if (!efforts.isEmpty()) {
        capabilities << QStringLiteral("档位 %1").arg(efforts.size());
    }
    return capabilities;
}

void setCell(QTableWidget *table, int row, int column, const QString &text,
             const QString &tooltip = QString())
{
    auto *item = new QTableWidgetItem(text);
    if (!tooltip.isEmpty()) {
        item->setToolTip(tooltip);
    }
    table->setItem(row, column, item);
}

QTableWidget *makeTable(const QStringList &headers)
{
    auto *table = new QTableWidget;
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setAlternatingRowColors(true);
    table->setShowGrid(false);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    return table;
}

QWidget *makeCard(const QString &caption, QLabel **valueOut)
{
    auto *card = new QWidget;
    card->setObjectName(QStringLiteral("statCard"));
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(4);
    auto *captionLabel = new QLabel(caption);
    captionLabel->setObjectName(QStringLiteral("statCaption"));
    auto *valueLabel = new QLabel(QStringLiteral("—"));
    valueLabel->setObjectName(QStringLiteral("statValue"));
    layout->addWidget(captionLabel);
    layout->addWidget(valueLabel);
    if (valueOut) {
        *valueOut = valueLabel;
    }
    return card;
}

QPushButton *button(const QString &text, bool primary = false)
{
    auto *pushButton = new QPushButton(text);
    if (primary) {
        pushButton->setObjectName(QStringLiteral("primaryButton"));
    }
    return pushButton;
}

QJsonObject configSection(const QJsonObject &root, const QString &name)
{
    return root.value(name).toObject();
}

QString nestedString(const QJsonObject &root, const QString &section, const QString &key,
                    const QString &fallback = QString())
{
    const QJsonObject object = configSection(root, section);
    return object.contains(key) ? object.value(key).toString() : fallback;
}

bool nestedBool(const QJsonObject &root, const QString &section, const QString &key, bool fallback)
{
    const QJsonObject object = configSection(root, section);
    return object.contains(key) ? object.value(key).toBool(fallback) : fallback;
}

int nestedInt(const QJsonObject &root, const QString &section, const QString &key, int fallback)
{
    const QJsonObject object = configSection(root, section);
    return object.contains(key) ? object.value(key).toInt(fallback) : fallback;
}

double nestedDouble(const QJsonObject &root, const QString &section, const QString &key, double fallback)
{
    const QJsonObject object = configSection(root, section);
    return object.contains(key) ? object.value(key).toDouble(fallback) : fallback;
}

QString hoursText(const QJsonObject &root, const QString &key, const QString &fallback)
{
    const QJsonArray hours = configSection(root, QStringLiteral("schedule")).value(key).toArray();
    if (hours.isEmpty()) {
        return fallback;
    }
    QStringList values;
    for (const QJsonValue &value : hours) {
        values << QString::number(value.toInt());
    }
    return values.join(QStringLiteral(","));
}

QJsonArray parseHours(const QString &text, bool *ok)
{
    QJsonArray array;
    bool valid = true;
    const QStringList parts = text.split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        valid = false;
    }
    for (const QString &part : parts) {
        bool numberOk = false;
        const int value = part.trimmed().toInt(&numberOk);
        if (!numberOk || value < 0 || value > 23) {
            valid = false;
            break;
        }
        array.append(value);
    }
    if (ok) {
        *ok = valid;
    }
    return valid ? array : QJsonArray();
}

QString taskLabel(const QString &task)
{
    static const QHash<QString, QString> labels = {
        {QStringLiteral("checkin"), QStringLiteral("签到")},
        {QStringLiteral("activity"), QStringLiteral("活跃上报")},
        {QStringLiteral("travel"), QStringLiteral("猫猫旅行")},
        {QStringLiteral("keepalive"), QStringLiteral("Token 保活")},
        {QStringLiteral("school"), QStringLiteral("开学季")},
        {QStringLiteral("cat"), QStringLiteral("夜猫子")}};
    return labels.value(task, task);
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), managerSettings_(QSettings::IniFormat, QSettings::UserScope,
                                            QStringLiteral("workbuddy2api"),
                                            QStringLiteral("manager"))
{
    root_ = findProjectRoot();
    const QIcon appIcon(QDir(root_).filePath(QStringLiteral("assets/workbuddy2api-manager.ico")));
    if (!appIcon.isNull()) {
        setWindowIcon(appIcon);
        QApplication::setWindowIcon(appIcon);
    }
    config_.setRoot(root_);
    service_.setRoot(root_);
    taskRunner_.setRoot(root_);
    network_ = new QNetworkAccessManager(this);
    refreshTimer_ = new QTimer(this);
    logTimer_ = new QTimer(this);

    service_.setLogCallback([this](const QString &line) { appendLog(line); });
    service_.setFinishedCallback([this](const QString &action, bool ok) {
        recoveryInProgress_ = false;
        appendLog(QStringLiteral("服务操作：%1").arg(action));
        if (exitAfterServiceStop_) {
            exitAfterServiceStop_ = false;
            if (action == QStringLiteral("停止服务") && ok) {
                allowQuit_ = true;
                close();
            } else {
                QMessageBox::warning(this, QStringLiteral("退出未完成"),
                                     QStringLiteral("服务未能确认停止，管理器仍保持打开。请查看日志后重试。"));
            }
            return;
        }
        if (!ok) {
            notify(QStringLiteral("服务操作失败"), action, QSystemTrayIcon::Warning);
        }
        if (action == QStringLiteral("停止服务") && restartAfterStop_) {
            restartAfterStop_ = false;
            QTimer::singleShot(700, this, [this] { startService(); });
        } else {
            QTimer::singleShot(900, this, [this] { refreshAll(); });
        }
    });
    taskRunner_.setLogCallback([this](const QString &line) { appendLog(line); });
    taskRunner_.setFinishedCallback([this](const QString &action, bool ok, int exitCode) {
        appendLog(QStringLiteral("本地任务：%1（%2，退出码 %3）")
                      .arg(action, ok ? QStringLiteral("完成") : QStringLiteral("失败"))
                      .arg(exitCode));
        if (action.contains(QStringLiteral("积分日报"))) {
            const QString report = LogDiagnostics::sanitize(taskRunner_.output().trimmed());
            const QString message = report.isEmpty()
                                        ? (ok ? QStringLiteral("积分日报脚本已完成，但没有返回内容。")
                                              : QStringLiteral("积分日报执行失败，请查看日志页。"))
                                        : report;
            if (ok) {
                QMessageBox::information(this, QStringLiteral("积分日报"), message);
            } else {
                QMessageBox::warning(this, QStringLiteral("积分日报失败"), message);
            }
        }
        notify(ok ? QStringLiteral("本地任务完成") : QStringLiteral("本地任务失败"), action,
               ok ? QSystemTrayIcon::Information : QSystemTrayIcon::Warning);
        QTimer::singleShot(800, this, [this] { refreshAll(); });
    });

    loadConfig();
    setupUi();
    loadManagerSettings();
    setupTray();
    setupStyles();

    connect(refreshTimer_, &QTimer::timeout, this, [this] { refreshAll(); });
    connect(logTimer_, &QTimer::timeout, this, [this] { refreshLogs(); });
    refreshTimer_->start(5000);
    logTimer_->start(2500);
    if (!configError_.isEmpty()) {
        appendLog(configError_);
    }
    QTimer::singleShot(0, this, [this] { refreshAll(); });
}

MainWindow::~MainWindow()
{
    service_.terminate();
    taskRunner_.terminate();
    if (chatReply_) {
        chatReply_->abort();
    }
}

QString MainWindow::findProjectRoot() const
{
    QStringList candidates;
    candidates << QCoreApplication::applicationDirPath() << QDir::currentPath();
    for (const QString &candidate : candidates) {
        QDir directory(candidate);
        for (int level = 0; level < 8; ++level) {
            if (directory.exists(QStringLiteral("config.json"))
                && directory.exists(QStringLiteral("start.ps1"))
                && directory.exists(QStringLiteral("stop.ps1"))) {
                return directory.absolutePath();
            }
            if (!directory.cdUp()) {
                break;
            }
        }
    }
    return QDir(QCoreApplication::applicationDirPath()).absolutePath();
}

void MainWindow::loadConfig()
{
    QString error;
    if (!config_.load(&error)) {
        configError_ = error;
        baseUrl_ = QUrl(QStringLiteral("http://127.0.0.1:7863"));
        return;
    }
    baseUrl_ = config_.baseUrl();
    apiKey_ = config_.apiKey();
}

void MainWindow::loadManagerSettings()
{
    if (!autoOpenGatewayCheck_) {
        return;
    }
    autoOpenGatewayCheck_->setChecked(managerSettings_.value(QStringLiteral("autoOpenGateway"), true).toBool());
    autoRecoveryCheck_->setChecked(managerSettings_.value(QStringLiteral("autoRecovery"), false).toBool());
    notificationsCheck_->setChecked(managerSettings_.value(QStringLiteral("notifications"), true).toBool());
    autoStartCheck_->setChecked(managerSettings_.value(QStringLiteral("autoStart"), false).toBool());
    proxyEdit_->setText(managerSettings_.value(QStringLiteral("proxy"), QStringLiteral("127.0.0.1:7897"))
                            .toString());
    const QString theme = managerSettings_.value(QStringLiteral("theme"), QStringLiteral("system")).toString();
    const int index = themeCombo_->findData(theme);
    themeCombo_->setCurrentIndex(index >= 0 ? index : 0);
    applyTheme(themeCombo_->currentData().toString());
}

void MainWindow::saveManagerSettings()
{
    managerSettings_.setValue(QStringLiteral("autoOpenGateway"), autoOpenGatewayCheck_->isChecked());
    managerSettings_.setValue(QStringLiteral("autoRecovery"), autoRecoveryCheck_->isChecked());
    managerSettings_.setValue(QStringLiteral("notifications"), notificationsCheck_->isChecked());
    managerSettings_.setValue(QStringLiteral("autoStart"), autoStartCheck_->isChecked());
    managerSettings_.setValue(QStringLiteral("proxy"), proxyEdit_->text().trimmed());
    managerSettings_.setValue(QStringLiteral("theme"), themeCombo_->currentData().toString());
    managerSettings_.sync();

#ifdef Q_OS_WIN
    QSettings startup(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                     QSettings::NativeFormat);
    const QString appPath = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    if (autoStartCheck_->isChecked()) {
        startup.setValue(QStringLiteral("workbuddy2api-manager"), QStringLiteral("\"%1\"").arg(appPath));
    } else {
        startup.remove(QStringLiteral("workbuddy2api-manager"));
    }
    startup.sync();
#endif
}

void MainWindow::setupUi()
{
    setWindowTitle(QStringLiteral("workbuddy2api 本地管理面板"));
    resize(1220, 820);
    setMinimumSize(960, 640);

    auto *central = new QWidget;
    auto *layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *sidebar = new QWidget;
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setFixedWidth(190);
    auto *sideLayout = new QVBoxLayout(sidebar);
    sideLayout->setContentsMargins(16, 22, 16, 18);
    sideLayout->setSpacing(10);
    auto *brand = new QLabel(QStringLiteral("workbuddy2api\n本地管理器"));
    brand->setObjectName(QStringLiteral("brand"));
    sideLayout->addWidget(brand);
    sideLayout->addSpacing(12);
    navigation_ = new QListWidget;
    navigation_->setObjectName(QStringLiteral("navigation"));
    navigation_->addItems({QStringLiteral("概览"), QStringLiteral("账号"), QStringLiteral("模型"),
                           QStringLiteral("用量"), QStringLiteral("任务中心"), QStringLiteral("聊天测试"),
                           QStringLiteral("设置"), QStringLiteral("日志"), QStringLiteral("诊断")});
    navigation_->setCurrentRow(0);
    sideLayout->addWidget(navigation_, 1);
    auto *sideHint = new QLabel(QStringLiteral("本地运行\n不会上传凭证"));
    sideHint->setObjectName(QStringLiteral("sideHint"));
    sideHint->setWordWrap(true);
    sideLayout->addWidget(sideHint);
    layout->addWidget(sidebar);

    stack_ = new QStackedWidget;
    stack_->addWidget(createOverviewPage());
    stack_->addWidget(createAccountsPage());
    stack_->addWidget(createModelsPage());
    stack_->addWidget(createUsagePage());
    stack_->addWidget(createTasksPage());
    stack_->addWidget(createChatPage());
    stack_->addWidget(createSettingsPage());
    stack_->addWidget(createLogsPage());
    stack_->addWidget(createDiagnosticsPage());
    layout->addWidget(stack_, 1);
    setCentralWidget(central);
    statusBar()->showMessage(QStringLiteral("准备就绪"));

    connect(navigation_, &QListWidget::currentRowChanged, stack_, &QStackedWidget::setCurrentIndex);
}

QWidget *MainWindow::createOverviewPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 24, 28, 22);
    layout->setSpacing(14);

    auto *header = new QHBoxLayout;
    auto *title = new QLabel(QStringLiteral("概览"));
    title->setObjectName(QStringLiteral("pageTitle"));
    endpointLabel_ = new QLabel;
    endpointLabel_->setObjectName(QStringLiteral("endpoint"));
    serviceBadge_ = new QLabel(QStringLiteral("未连接"));
    serviceBadge_->setObjectName(QStringLiteral("serviceBadge"));
    header->addWidget(title);
    header->addStretch(1);
    header->addWidget(endpointLabel_);
    header->addSpacing(14);
    header->addWidget(serviceBadge_);
    layout->addLayout(header);

    auto *actions = new QHBoxLayout;
    auto *start = button(QStringLiteral("启动服务"), true);
    auto *stop = button(QStringLiteral("停止服务"));
    auto *restart = button(QStringLiteral("重启服务"));
    auto *refresh = button(QStringLiteral("刷新状态"));
    auto *verify = button(QStringLiteral("打开验证"));
    auto *logs = button(QStringLiteral("打开日志目录"));
    actions->addWidget(start);
    actions->addWidget(stop);
    actions->addWidget(restart);
    actions->addWidget(refresh);
    actions->addStretch(1);
    actions->addWidget(verify);
    actions->addWidget(logs);
    layout->addLayout(actions);
    connect(start, &QPushButton::clicked, this, [this] { startService(); });
    connect(stop, &QPushButton::clicked, this, [this] { stopService(); });
    connect(restart, &QPushButton::clicked, this, [this] { restartService(); });
    connect(refresh, &QPushButton::clicked, this, [this] { refreshAll(); });
    connect(verify, &QPushButton::clicked, this, [this] { openVerification(); });
    connect(logs, &QPushButton::clicked, this, [this] { openLogsDirectory(); });

    auto *overview = new QGroupBox(QStringLiteral("服务概况"));
    auto *cards = new QGridLayout(overview);
    cards->setContentsMargins(12, 14, 12, 12);
    cards->setSpacing(10);
    cards->addWidget(makeCard(QStringLiteral("网关"), &overviewService_), 0, 0);
    cards->addWidget(makeCard(QStringLiteral("账号总数"), &overviewAccounts_), 0, 1);
    cards->addWidget(makeCard(QStringLiteral("健康账号"), &overviewHealthy_), 0, 2);
    cards->addWidget(makeCard(QStringLiteral("冷却 / 禁用"), &overviewCooling_), 0, 3);
    cards->addWidget(makeCard(QStringLiteral("模型数量"), &overviewModels_), 0, 4);
    cards->addWidget(makeCard(QStringLiteral("今日请求"), &overviewRequests_), 1, 0);
    cards->addWidget(makeCard(QStringLiteral("今日成功率"), &overviewSuccess_), 1, 1);
    cards->addWidget(makeCard(QStringLiteral("今日 Token"), &overviewTokens_), 1, 2);
    layout->addWidget(overview);

    overviewHint_ = new QLabel(QStringLiteral("正在检查本地网关……"));
    overviewHint_->setObjectName(QStringLiteral("hint"));
    overviewHint_->setWordWrap(true);
    layout->addWidget(overviewHint_);

    auto *tip = new QLabel(QStringLiteral("管理器不会发起聊天请求；模型页只读取列表，用量页只读取本地统计。"));
    tip->setObjectName(QStringLiteral("muted"));
    layout->addWidget(tip);
    layout->addStretch(1);
    return page;
}

QWidget *MainWindow::createAccountsPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 24, 28, 22);
    auto *header = new QHBoxLayout;
    auto *title = new QLabel(QStringLiteral("账号管理"));
    title->setObjectName(QStringLiteral("pageTitle"));
    header->addWidget(title);
    header->addStretch(1);
    auto *login = button(QStringLiteral("打开验证"), true);
    auto *import = button(QStringLiteral("导入 JSON"));
    auto *refresh = button(QStringLiteral("刷新"));
    header->addWidget(login);
    header->addWidget(import);
    header->addWidget(refresh);
    layout->addLayout(header);

    auto *toolbar = new QHBoxLayout;
    auto *relogin = button(QStringLiteral("选中账号重新登录"));
    auto *enable = button(QStringLiteral("启用"));
    auto *disable = button(QStringLiteral("禁用"));
    auto *remove = button(QStringLiteral("删除"));
    toolbar->addWidget(relogin);
    toolbar->addWidget(enable);
    toolbar->addWidget(disable);
    toolbar->addWidget(remove);
    toolbar->addStretch(1);
    toolbar->addWidget(new QLabel(QStringLiteral("删除和清空统计会二次确认")));
    layout->addLayout(toolbar);

    accountsTable_ = makeTable({QStringLiteral("UID"), QStringLiteral("昵称"), QStringLiteral("域"),
                                QStringLiteral("积分"), QStringLiteral("状态"), QStringLiteral("在途"),
                                QStringLiteral("成功 / 错误"), QStringLiteral("冷却信息")});
    accountsTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    accountsTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    accountsTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    accountsTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    accountsTable_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    accountsTable_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    accountsTable_->horizontalHeader()->setSectionResizeMode(6, QHeaderView::ResizeToContents);
    layout->addWidget(accountsTable_, 1);

    connect(login, &QPushButton::clicked, this, [this] { openVerification(); });
    connect(import, &QPushButton::clicked, this, [this] { importAccountJson(); });
    connect(refresh, &QPushButton::clicked, this, [this] { refreshAll(); });
    connect(relogin, &QPushButton::clicked, this, [this] { reloginSelectedAccount(); });
    connect(enable, &QPushButton::clicked, this, [this] { enableSelectedAccount(); });
    connect(disable, &QPushButton::clicked, this, [this] { disableSelectedAccount(); });
    connect(remove, &QPushButton::clicked, this, [this] { deleteSelectedAccount(); });
    return page;
}

QWidget *MainWindow::createModelsPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 24, 28, 22);
    auto *header = new QHBoxLayout;
    auto *title = new QLabel(QStringLiteral("模型目录"));
    title->setObjectName(QStringLiteral("pageTitle"));
    header->addWidget(title);
    header->addStretch(1);
    auto *refresh = button(QStringLiteral("刷新模型"));
    auto *copy = button(QStringLiteral("复制模型 ID"));
    header->addWidget(copy);
    header->addWidget(refresh);
    layout->addLayout(header);
    auto *filters = new QHBoxLayout;
    modelSearch_ = new QLineEdit;
    modelSearch_->setPlaceholderText(QStringLiteral("搜索模型 ID、名称或描述"));
    modelRealm_ = new QComboBox;
    modelRealm_->addItem(QStringLiteral("全部域"), QStringLiteral("all"));
    modelRealm_->addItem(QStringLiteral("CN"), QStringLiteral("cn"));
    modelRealm_->addItem(QStringLiteral("Global"), QStringLiteral("global"));
    filters->addWidget(modelSearch_, 1);
    filters->addWidget(modelRealm_);
    layout->addLayout(filters);
    modelsTable_ = makeTable({QStringLiteral("模型 ID"), QStringLiteral("名称"), QStringLiteral("倍率"),
                              QStringLiteral("上下文"), QStringLiteral("输出上限"), QStringLiteral("能力")});
    modelsTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    modelsTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    modelsTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    modelsTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    modelsTable_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    layout->addWidget(modelsTable_, 1);
    connect(refresh, &QPushButton::clicked, this, [this] { fetchModels(); });
    connect(copy, &QPushButton::clicked, this, [this] {
        if (!modelsTable_->currentItem()) {
            return;
        }
        QApplication::clipboard()->setText(modelsTable_->item(modelsTable_->currentRow(), 0)->text());
        statusBar()->showMessage(QStringLiteral("模型 ID 已复制"), 2500);
    });
    connect(modelSearch_, &QLineEdit::textChanged, this, [this] { applyModels(lastModels_); });
    connect(modelRealm_, &QComboBox::currentTextChanged, this, [this] { applyModels(lastModels_); });
    return page;
}

QWidget *MainWindow::createUsagePage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 24, 28, 22);
    auto *header = new QHBoxLayout;
    auto *title = new QLabel(QStringLiteral("用量统计"));
    title->setObjectName(QStringLiteral("pageTitle"));
    header->addWidget(title);
    header->addStretch(1);
    usageRange_ = new QComboBox;
    usageRange_->addItem(QStringLiteral("今天"), QStringLiteral("today"));
    usageRange_->addItem(QStringLiteral("最近 7 天"), QStringLiteral("7d"));
    usageRange_->addItem(QStringLiteral("最近 30 天"), QStringLiteral("30d"));
    usageRange_->addItem(QStringLiteral("全部"), QStringLiteral("all"));
    usageRange_->setCurrentIndex(1);
    auto *refresh = button(QStringLiteral("刷新"));
    auto *json = button(QStringLiteral("导出 JSON"));
    auto *csv = button(QStringLiteral("导出 CSV"));
    auto *clear = button(QStringLiteral("清空统计"));
    header->addWidget(usageRange_);
    header->addWidget(refresh);
    header->addWidget(json);
    header->addWidget(csv);
    header->addWidget(clear);
    layout->addLayout(header);

    auto *cards = new QGridLayout;
    cards->setSpacing(10);
    cards->addWidget(makeCard(QStringLiteral("请求数"), &usageRequests_), 0, 0);
    cards->addWidget(makeCard(QStringLiteral("成功率"), &usageSuccess_), 0, 1);
    cards->addWidget(makeCard(QStringLiteral("总 Token"), &usageTokens_), 0, 2);
    cards->addWidget(makeCard(QStringLiteral("Credit"), &usageCredit_), 0, 3);
    layout->addLayout(cards);

    auto *splitter = new QSplitter(Qt::Vertical);
    usageModelsTable_ = makeTable({QStringLiteral("模型"), QStringLiteral("请求"), QStringLiteral("成功"),
                                   QStringLiteral("失败"), QStringLiteral("输入 Token"),
                                   QStringLiteral("输出 Token"), QStringLiteral("总 Token"),
                                   QStringLiteral("Credit")});
    usageModelsTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    usageDaysTable_ = makeTable({QStringLiteral("日期"), QStringLiteral("请求"), QStringLiteral("成功"),
                                 QStringLiteral("失败"), QStringLiteral("总 Token"), QStringLiteral("Credit")});
    usageDaysTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    splitter->addWidget(usageModelsTable_);
    splitter->addWidget(usageDaysTable_);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    layout->addWidget(splitter, 1);
    connect(usageRange_, &QComboBox::currentIndexChanged, this, [this] { fetchUsage(); });
    connect(refresh, &QPushButton::clicked, this, [this] { fetchUsage(); });
    connect(json, &QPushButton::clicked, this, [this] { exportUsageJson(); });
    connect(csv, &QPushButton::clicked, this, [this] { exportUsageCsv(); });
    connect(clear, &QPushButton::clicked, this, [this] { clearUsage(); });
    return page;
}

QWidget *MainWindow::createTasksPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 24, 28, 22);
    layout->setSpacing(12);

    auto *header = new QHBoxLayout;
    auto *title = new QLabel(QStringLiteral("任务中心"));
    title->setObjectName(QStringLiteral("pageTitle"));
    header->addWidget(title);
    header->addStretch(1);
    auto *realm = new QComboBox;
    realm->addItem(QStringLiteral("国内版 CN"), QStringLiteral("cn"));
    realm->addItem(QStringLiteral("国际版 Global"), QStringLiteral("global"));
    auto *verify = button(QStringLiteral("打开验证"), true);
    auto *signin = button(QStringLiteral("手动签到"));
    auto *credit = button(QStringLiteral("积分日报"));
    auto *trial = button(QStringLiteral("领取 Global Trial"));
    header->addWidget(realm);
    header->addWidget(verify);
    header->addWidget(signin);
    header->addWidget(credit);
    header->addWidget(trial);
    layout->addLayout(header);

    auto *hint = new QLabel(QStringLiteral(
        "手动任务直接复用服务端账号池；签到、活跃、旅行、保活和活动任务可能访问上游并产生副作用。"
        "每项任务可单独关闭，保存后重启网关生效。"));
    hint->setObjectName(QStringLiteral("hint"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    tasksTable_ = makeTable({QStringLiteral("任务"), QStringLiteral("作用"), QStringLiteral("开关"),
                             QStringLiteral("计划时间"), QStringLiteral("上次运行"), QStringLiteral("状态"),
                             QStringLiteral("操作")});
    tasksTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tasksTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    tasksTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    tasksTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    tasksTable_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    tasksTable_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    // Cell widgets do not contribute a reliable size hint to QHeaderView on all
    // Windows font/DPI combinations. Keep the action column stable so the
    // embedded button text cannot be compressed or vertically clipped.
    tasksTable_->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Fixed);
    tasksTable_->setColumnWidth(6, 118);
    tasksTable_->verticalHeader()->setDefaultSectionSize(40);
    const QStringList taskIds = {QStringLiteral("checkin"), QStringLiteral("activity"), QStringLiteral("travel"),
                                 QStringLiteral("keepalive"), QStringLiteral("school"), QStringLiteral("cat")};
    const QStringList taskLabels = {QStringLiteral("手动签到"), QStringLiteral("活跃上报"), QStringLiteral("猫猫旅行"),
                                    QStringLiteral("Token 保活"), QStringLiteral("开学季任务"), QStringLiteral("夜猫子任务")};
    const QStringList taskDescriptions = {
        QStringLiteral("刷新必要凭证、签到并回读余额"),
        QStringLiteral("上报活跃对话并联动领猫/连登奖励"),
        QStringLiteral("领养、派出或领取猫猫旅行奖励"),
        QStringLiteral("刷新所有账号 Token，连续失效才禁用"),
        QStringLiteral("执行开学季任务、领奖和抽奖脚本"),
        QStringLiteral("在 23:00–08:00 窗口执行 black_cat 任务")};
    for (int i = 0; i < taskIds.size(); ++i) {
        const int row = tasksTable_->rowCount();
        tasksTable_->insertRow(row);
        tasksTable_->setVerticalHeaderItem(row, new QTableWidgetItem(taskIds.at(i)));
        setCell(tasksTable_, row, 0, taskLabels.at(i), taskIds.at(i));
        setCell(tasksTable_, row, 1, taskDescriptions.at(i));
        setCell(tasksTable_, row, 2, QStringLiteral("—"));
        setCell(tasksTable_, row, 3, QStringLiteral("—"));
        setCell(tasksTable_, row, 4, QStringLiteral("从未运行"));
        setCell(tasksTable_, row, 5, QStringLiteral("—"));
        auto *run = button(QStringLiteral("立即执行"));
        run->setMinimumSize(100, 30);
        run->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        tasksTable_->setRowHeight(row, 40);
        tasksTable_->setCellWidget(row, 6, run);
        taskButtons_.insert(taskIds.at(i), run);
        const QString taskId = taskIds.at(i);
        connect(run, &QPushButton::clicked, this, [this, taskId] { triggerTask(taskId); });
    }
    layout->addWidget(tasksTable_, 2);

    auto *historyHeader = new QHBoxLayout;
    auto *historyTitle = new QLabel(QStringLiteral("执行历史"));
    historyTitle->setObjectName(QStringLiteral("sectionTitle"));
    historyHeader->addWidget(historyTitle);
    taskHistoryFilter_ = new QComboBox;
    taskHistoryFilter_->addItem(QStringLiteral("全部任务"), QString());
    for (int i = 0; i < taskIds.size(); ++i) {
        taskHistoryFilter_->addItem(taskLabels.at(i), taskIds.at(i));
    }
    auto *refresh = button(QStringLiteral("刷新历史"));
    auto *clear = button(QStringLiteral("清空历史"));
    historyHeader->addStretch(1);
    historyHeader->addWidget(taskHistoryFilter_);
    historyHeader->addWidget(refresh);
    historyHeader->addWidget(clear);
    layout->addLayout(historyHeader);

    taskHistoryTable_ = makeTable({QStringLiteral("任务"), QStringLiteral("开始"), QStringLiteral("结束"),
                                   QStringLiteral("结果"), QStringLiteral("摘要")});
    taskHistoryTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    taskHistoryTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    taskHistoryTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    taskHistoryTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    taskHistoryTable_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    layout->addWidget(taskHistoryTable_, 2);

    taskDetailView_ = new QPlainTextEdit;
    taskDetailView_->setReadOnly(true);
    taskDetailView_->setMaximumBlockCount(200);
    taskDetailView_->setPlaceholderText(QStringLiteral("选择一条历史查看脱敏摘要"));
    taskDetailView_->setFixedHeight(80);
    layout->addWidget(taskDetailView_);

    connect(verify, &QPushButton::clicked, this, [this, realm] {
        openVerification(realm->currentData().toString());
    });
    connect(signin, &QPushButton::clicked, this, [this] {
        runLocalScript(QStringLiteral("signin.sh"), {}, QStringLiteral("手动签到 signin.sh"));
    });
    connect(credit, &QPushButton::clicked, this, [this] {
        runLocalScript(QStringLiteral("credit.sh"), {}, QStringLiteral("积分日报 credit.sh"));
    });
    connect(trial, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, QStringLiteral("确认领取 Global Trial"),
                                  QStringLiteral("该操作会访问国际版计费接口；已领取账号会幂等跳过。继续吗？"),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
            == QMessageBox::Yes) {
            runLocalScript(QStringLiteral("trial.sh"), {}, QStringLiteral("Global Trial trial.sh"));
        }
    });
    connect(refresh, &QPushButton::clicked, this, [this] { fetchTaskHistory(); });
    connect(clear, &QPushButton::clicked, this, [this] { clearTaskHistory(); });
    connect(taskHistoryFilter_, &QComboBox::currentIndexChanged, this, [this] { refreshTasksPage(); });
    connect(taskHistoryTable_, &QTableWidget::itemSelectionChanged, this, [this] {
        const int row = taskHistoryTable_->currentRow();
        if (row >= 0 && taskHistoryTable_->item(row, 0)) {
            taskDetailView_->setPlainText(taskHistoryTable_->item(row, 4)->toolTip());
        }
    });
    return page;
}

QWidget *MainWindow::createChatPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 24, 28, 22);
    layout->setSpacing(12);
    auto *header = new QHBoxLayout;
    auto *title = new QLabel(QStringLiteral("聊天测试"));
    title->setObjectName(QStringLiteral("pageTitle"));
    header->addWidget(title);
    header->addStretch(1);
    chatStatus_ = new QLabel(QStringLiteral("只读模型列表，发送会消耗上游额度"));
    chatStatus_->setObjectName(QStringLiteral("muted"));
    header->addWidget(chatStatus_);
    layout->addLayout(header);

    auto *form = new QFormLayout;
    chatModelCombo_ = new QComboBox;
    chatModelCombo_->setEditable(true);
    chatModelCombo_->addItem(QStringLiteral("glm-5.2"), QStringLiteral("glm-5.2"));
    chatMaxTokensSpin_ = new QSpinBox;
    chatMaxTokensSpin_->setRange(1, 131072);
    chatMaxTokensSpin_->setValue(2048);
    chatTemperatureSpin_ = new QDoubleSpinBox;
    chatTemperatureSpin_->setRange(0.0, 2.0);
    chatTemperatureSpin_->setSingleStep(0.1);
    chatTemperatureSpin_->setValue(0.7);
    chatStreamCheck_ = new QCheckBox(QStringLiteral("流式显示（兼容性模式，默认关闭）"));
    form->addRow(QStringLiteral("模型"), chatModelCombo_);
    form->addRow(QStringLiteral("最大输出 Token"), chatMaxTokensSpin_);
    form->addRow(QStringLiteral("Temperature"), chatTemperatureSpin_);
    form->addRow(QStringLiteral("响应"), chatStreamCheck_);
    layout->addLayout(form);

    auto *splitter = new QSplitter(Qt::Vertical);
    auto *prompts = new QWidget;
    auto *promptLayout = new QHBoxLayout(prompts);
    promptLayout->setContentsMargins(0, 0, 0, 0);
    auto *systemBox = new QGroupBox(QStringLiteral("System（可选）"));
    auto *systemLayout = new QVBoxLayout(systemBox);
    chatSystemEdit_ = new QPlainTextEdit;
    chatSystemEdit_->setPlaceholderText(QStringLiteral("只在需要覆盖系统提示时填写"));
    systemLayout->addWidget(chatSystemEdit_);
    auto *userBox = new QGroupBox(QStringLiteral("User 消息"));
    auto *userLayout = new QVBoxLayout(userBox);
    chatUserEdit_ = new QPlainTextEdit;
    chatUserEdit_->setPlaceholderText(QStringLiteral("输入一条测试消息，例如：你好，请简短介绍你自己。"));
    userLayout->addWidget(chatUserEdit_);
    promptLayout->addWidget(systemBox);
    promptLayout->addWidget(userBox);
    splitter->addWidget(prompts);

    auto *responseBox = new QGroupBox(QStringLiteral("模型响应"));
    auto *responseLayout = new QVBoxLayout(responseBox);
    chatResponseEdit_ = new QPlainTextEdit;
    chatResponseEdit_->setReadOnly(true);
    responseLayout->addWidget(chatResponseEdit_);
    splitter->addWidget(responseBox);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);
    layout->addWidget(splitter, 1);

    auto *actions = new QHBoxLayout;
    auto *send = button(QStringLiteral("发送测试"), true);
    auto *stop = button(QStringLiteral("停止"));
    auto *clear = button(QStringLiteral("清空"));
    actions->addWidget(send);
    actions->addWidget(stop);
    actions->addWidget(clear);
    actions->addStretch(1);
    actions->addWidget(new QLabel(QStringLiteral("请求走本地 /v1/chat/completions，不会绕过网关")));
    layout->addLayout(actions);
    connect(send, &QPushButton::clicked, this, [this] { sendChatTest(); });
    connect(stop, &QPushButton::clicked, this, [this] { stopChatTest(); });
    connect(clear, &QPushButton::clicked, this, [this] {
        chatSystemEdit_->clear();
        chatUserEdit_->clear();
        chatResponseEdit_->clear();
        chatStatus_->setText(QStringLiteral("只读模型列表，发送会消耗上游额度"));
    });
    return page;
}

QWidget *MainWindow::createSettingsPage()
{
    auto *page = new QWidget;
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(28, 24, 28, 22);
    auto *title = new QLabel(QStringLiteral("设置"));
    title->setObjectName(QStringLiteral("pageTitle"));
    layout->addWidget(title);
    auto *group = new QGroupBox(QStringLiteral("网关常用配置"));
    auto *form = new QFormLayout(group);
    form->setContentsMargins(16, 18, 16, 14);
    form->setSpacing(10);
    listenEdit_ = new QLineEdit;
    apiKeyEdit_ = new QLineEdit;
    apiKeyEdit_->setEchoMode(QLineEdit::Password);
    maxInFlightSpin_ = new QSpinBox;
    maxInFlightSpin_->setRange(0, 1000);
    maxInFlightSpin_->setSpecialValueText(QStringLiteral("不限"));
    maxBodyMbSpin_ = new QSpinBox;
    maxBodyMbSpin_->setRange(1, 1024);
    softRateEdit_ = new QLineEdit;
    softRateMaxEdit_ = new QLineEdit;
    proxyEdit_ = new QLineEdit;
    form->addRow(QStringLiteral("监听地址"), listenEdit_);
    form->addRow(QStringLiteral("API Key"), apiKeyEdit_);
    form->addRow(QStringLiteral("单账号并发"), maxInFlightSpin_);
    form->addRow(QStringLiteral("请求体上限（MB）"), maxBodyMbSpin_);
    form->addRow(QStringLiteral("软限流冷却"), softRateEdit_);
    form->addRow(QStringLiteral("软限流封顶"), softRateMaxEdit_);
    form->addRow(QStringLiteral("本地代理"), proxyEdit_);
    layout->addWidget(group);

    auto *schedule = new QGroupBox(QStringLiteral("六类定时任务（独立开关与时间）"));
    auto *scheduleGrid = new QGridLayout(schedule);
    scheduleGrid->addWidget(new QLabel(QStringLiteral("任务")), 0, 0);
    scheduleGrid->addWidget(new QLabel(QStringLiteral("启用")), 0, 1);
    scheduleGrid->addWidget(new QLabel(QStringLiteral("整点列表")), 0, 2);
    scheduleGrid->addWidget(new QLabel(QStringLiteral("说明")), 0, 3);
    const QStringList scheduleIds = {QStringLiteral("checkin"), QStringLiteral("activity"), QStringLiteral("travel"),
                                     QStringLiteral("keepalive"), QStringLiteral("school"), QStringLiteral("cat")};
    const QStringList scheduleLabels = {QStringLiteral("签到"), QStringLiteral("活跃上报"), QStringLiteral("猫猫旅行"),
                                        QStringLiteral("Token 保活"), QStringLiteral("开学季"), QStringLiteral("夜猫子")};
    const QStringList scheduleHints = {QStringLiteral("支持多个时间，例如 9,21"), QStringLiteral("每号上报条数由下方配置"),
                                       QStringLiteral("领养 / 派出 / 领奖巡检"), QStringLiteral("刷新凭证并处理连续失效"),
                                       QStringLiteral("调用开学季脚本"), QStringLiteral("23:00–08:00 窗口内执行")};
    scheduleEnabledChecks_.clear();
    scheduleHoursEdits_.clear();
    for (int i = 0; i < scheduleIds.size(); ++i) {
        auto *enabled = new QCheckBox;
        auto *hours = new QLineEdit;
        hours->setPlaceholderText(QStringLiteral("例如 9,21"));
        scheduleEnabledChecks_.append(enabled);
        scheduleHoursEdits_.append(hours);
        scheduleGrid->addWidget(new QLabel(scheduleLabels.at(i)), i + 1, 0);
        scheduleGrid->addWidget(enabled, i + 1, 1, Qt::AlignCenter);
        scheduleGrid->addWidget(hours, i + 1, 2);
        scheduleGrid->addWidget(new QLabel(scheduleHints.at(i)), i + 1, 3);
    }
    activityReportSpin_ = new QSpinBox;
    activityReportSpin_->setRange(1, 100);
    scheduleGrid->addWidget(new QLabel(QStringLiteral("每号活跃上报条数")), 7, 0, 1, 2);
    scheduleGrid->addWidget(activityReportSpin_, 7, 2);
    layout->addWidget(schedule);

    auto *realmGroup = new QGroupBox(QStringLiteral("Global / CN 双域"));
    auto *realmForm = new QFormLayout(realmGroup);
    globalEnabledCheck_ = new QCheckBox(QStringLiteral("启用 Global 账号与模型"));
    globalChatBaseEdit_ = new QLineEdit;
    globalBillingBaseEdit_ = new QLineEdit;
    globalChatBaseEdit_->setPlaceholderText(QStringLiteral("留空使用内置 workbuddy.ai"));
    globalBillingBaseEdit_->setPlaceholderText(QStringLiteral("留空使用内置 workbuddy.ai"));
    realmForm->addRow(globalEnabledCheck_);
    realmForm->addRow(QStringLiteral("Global Chat Base"), globalChatBaseEdit_);
    realmForm->addRow(QStringLiteral("Global Billing Base"), globalBillingBaseEdit_);
    auto *cnChatBase = new QLabel(QStringLiteral("https://copilot.tencent.com（内置 CN Chat）"));
    auto *cnBillingBase = new QLabel(QStringLiteral("https://www.codebuddy.cn（内置 CN Billing）"));
    cnChatBase->setTextInteractionFlags(Qt::TextSelectableByMouse);
    cnBillingBase->setTextInteractionFlags(Qt::TextSelectableByMouse);
    realmForm->addRow(QStringLiteral("CN Chat Base"), cnChatBase);
    realmForm->addRow(QStringLiteral("CN Billing Base"), cnBillingBase);
    layout->addWidget(realmGroup);

    auto *upstreamGroup = new QGroupBox(QStringLiteral("上游高级配置"));
    auto *upstreamForm = new QFormLayout(upstreamGroup);
    timeoutSecondsSpin_ = new QSpinBox;
    headerTimeoutSecondsSpin_ = new QSpinBox;
    idleTimeoutSecondsSpin_ = new QSpinBox;
    for (QSpinBox *spin : {timeoutSecondsSpin_, headerTimeoutSecondsSpin_, idleTimeoutSecondsSpin_}) {
        spin->setRange(1, 86400);
    }
    userAgentEdit_ = new QLineEdit;
    clientVersionEdit_ = new QLineEdit;
    cliVersionEdit_ = new QLineEdit;
    deviceTokenEdit_ = new QLineEdit;
    deviceTokenEdit_->setEchoMode(QLineEdit::Password);
    deviceTokenFileEdit_ = new QLineEdit;
    clientNameEdit_ = new QLineEdit;
    passthroughIpCheck_ = new QCheckBox(QStringLiteral("透传客户端 IP"));
    sanitizeFingerprintsCheck_ = new QCheckBox(QStringLiteral("脱敏黑名单指纹字段"));
    upstreamForm->addRow(QStringLiteral("短请求超时（秒）"), timeoutSecondsSpin_);
    upstreamForm->addRow(QStringLiteral("聊天首字节超时（秒）"), headerTimeoutSecondsSpin_);
    upstreamForm->addRow(QStringLiteral("流式空闲超时（秒）"), idleTimeoutSecondsSpin_);
    upstreamForm->addRow(QStringLiteral("上游 User-Agent"), userAgentEdit_);
    upstreamForm->addRow(QStringLiteral("Client Version"), clientVersionEdit_);
    upstreamForm->addRow(QStringLiteral("CLI Version"), cliVersionEdit_);
    upstreamForm->addRow(QStringLiteral("Device Token"), deviceTokenEdit_);
    upstreamForm->addRow(QStringLiteral("Device Token 文件"), deviceTokenFileEdit_);
    upstreamForm->addRow(QStringLiteral("Client Name"), clientNameEdit_);
    upstreamForm->addRow(passthroughIpCheck_);
    upstreamForm->addRow(sanitizeFingerprintsCheck_);
    layout->addWidget(upstreamGroup);

    auto *promptGroup = new QGroupBox(QStringLiteral("提示词配置"));
    auto *promptForm = new QFormLayout(promptGroup);
    promptModeCombo_ = new QComboBox;
    promptModeCombo_->addItem(QStringLiteral("透传客户端 system"), QStringLiteral("passthrough"));
    promptModeCombo_->addItem(QStringLiteral("使用自定义提示词"), QStringLiteral("custom"));
    promptFileEdit_ = new QLineEdit;
    promptFileEdit_->setPlaceholderText(QStringLiteral("留空使用内置提示词；custom 模式可填写文件路径"));
    promptTextEdit_ = new QPlainTextEdit;
    promptTextEdit_->setPlaceholderText(QStringLiteral("这里显示/编辑当前自定义提示词文件内容；保存前请确认模式"));
    promptTextEdit_->setMinimumHeight(100);
    promptForm->addRow(QStringLiteral("模式"), promptModeCombo_);
    promptForm->addRow(QStringLiteral("提示词文件"), promptFileEdit_);
    promptForm->addRow(QStringLiteral("提示词内容"), promptTextEdit_);
    layout->addWidget(promptGroup);

    auto *redisGroup = new QGroupBox(QStringLiteral("Upstash Redis 状态镜像"));
    auto *redisForm = new QFormLayout(redisGroup);
    upstashUrlEdit_ = new QLineEdit;
    upstashTokenEdit_ = new QLineEdit;
    upstashTokenEdit_->setEchoMode(QLineEdit::Password);
    upstashUrlEdit_->setPlaceholderText(QStringLiteral("留空 = 本地内存模式；支持 rediss:// 或 Upstash host"));
    upstashTokenEdit_->setPlaceholderText(QStringLiteral("只在 URL 不是完整连接串时填写"));
    redisForm->addRow(QStringLiteral("URL"), upstashUrlEdit_);
    redisForm->addRow(QStringLiteral("Token"), upstashTokenEdit_);
    redisStatus_ = new QLabel(QStringLiteral("状态会在重启后由概览 /status 显示"));
    redisStatus_->setWordWrap(true);
    redisForm->addRow(QStringLiteral("当前状态"), redisStatus_);
    layout->addWidget(redisGroup);

    auto *poolGroup = new QGroupBox(QStringLiteral("账号池、熔断与会话粘性"));
    auto *poolForm = new QFormLayout(poolGroup);
    breakerThresholdSpin_ = new QSpinBox;
    breakerThresholdSpin_->setRange(1, 100);
    breakerCooldownEdit_ = new QLineEdit;
    breakerCooldownMaxEdit_ = new QLineEdit;
    idleWeightPerHourEdit_ = new QLineEdit;
    idleWeightMaxEdit_ = new QLineEdit;
    expiringSoonEdit_ = new QLineEdit;
    sessionStickyCheck_ = new QCheckBox(QStringLiteral("启用会话粘性"));
    sessionTtlEdit_ = new QLineEdit;
    sessionGcEdit_ = new QLineEdit;
    poolForm->addRow(QStringLiteral("熔断失败阈值"), breakerThresholdSpin_);
    poolForm->addRow(QStringLiteral("熔断基础时长"), breakerCooldownEdit_);
    poolForm->addRow(QStringLiteral("熔断封顶时长"), breakerCooldownMaxEdit_);
    poolForm->addRow(QStringLiteral("闲置权重/小时"), idleWeightPerHourEdit_);
    poolForm->addRow(QStringLiteral("闲置权重封顶"), idleWeightMaxEdit_);
    poolForm->addRow(QStringLiteral("快过期积分窗口"), expiringSoonEdit_);
    poolForm->addRow(sessionStickyCheck_);
    poolForm->addRow(QStringLiteral("会话 TTL"), sessionTtlEdit_);
    poolForm->addRow(QStringLiteral("会话 GC 周期"), sessionGcEdit_);
    layout->addWidget(poolGroup);

    auto *behavior = new QGroupBox(QStringLiteral("管理器行为"));
    auto *behaviorLayout = new QVBoxLayout(behavior);
    autoOpenGatewayCheck_ = new QCheckBox(QStringLiteral("打开管理器时自动检查并启动网关"));
    autoRecoveryCheck_ = new QCheckBox(QStringLiteral("网关异常退出时自动恢复（10 分钟最多 3 次）"));
    notificationsCheck_ = new QCheckBox(QStringLiteral("启用 Windows 通知"));
    autoStartCheck_ = new QCheckBox(QStringLiteral("Windows 登录时自动启动管理器（默认关闭）"));
    showSecretsCheck_ = new QCheckBox(QStringLiteral("显示 API Key、Device Token、Upstash Token"));
    behaviorLayout->addWidget(autoOpenGatewayCheck_);
    behaviorLayout->addWidget(autoRecoveryCheck_);
    behaviorLayout->addWidget(notificationsCheck_);
    behaviorLayout->addWidget(autoStartCheck_);
    behaviorLayout->addWidget(showSecretsCheck_);
    layout->addWidget(behavior);

    auto *appearance = new QGroupBox(QStringLiteral("外观"));
    auto *appearanceLayout = new QFormLayout(appearance);
    themeCombo_ = new QComboBox;
    themeCombo_->addItem(QStringLiteral("跟随系统"), QStringLiteral("system"));
    themeCombo_->addItem(QStringLiteral("浅色"), QStringLiteral("light"));
    themeCombo_->addItem(QStringLiteral("深色"), QStringLiteral("dark"));
    appearanceLayout->addRow(QStringLiteral("主题"), themeCombo_);
    layout->addWidget(appearance);

    auto *actions = new QHBoxLayout;
    auto *save = button(QStringLiteral("保存配置"), true);
    auto *reload = button(QStringLiteral("重新读取"));
    auto *advanced = button(QStringLiteral("打开高级配置文件"));
    auto *restart = button(QStringLiteral("保存后重启网关"));
    actions->addWidget(save);
    actions->addWidget(reload);
    actions->addWidget(advanced);
    actions->addWidget(restart);
    actions->addStretch(1);
    layout->addLayout(actions);
    settingsHint_ = new QLabel(QStringLiteral("配置保存不会自动重启网关。"));
    settingsHint_->setObjectName(QStringLiteral("hint"));
    settingsHint_->setWordWrap(true);
    layout->addWidget(settingsHint_);
    layout->addStretch(1);

    listenEdit_->setText(config_.listen());
    apiKeyEdit_->setText(config_.apiKey());
    maxInFlightSpin_->setValue(config_.maxInFlight());
    loadAdvancedConfigFields();
    connect(save, &QPushButton::clicked, this, [this] { saveConfigFromUi(); });
    connect(reload, &QPushButton::clicked, this, [this] { reloadConfigFromDisk(); });
    connect(advanced, &QPushButton::clicked, this, [this] { openAdvancedConfig(); });
    connect(restart, &QPushButton::clicked, this, [this] { restartAfterConfig(); });
    connect(themeCombo_, &QComboBox::currentTextChanged, this,
            [this] { applyTheme(themeCombo_->currentData().toString()); });
    connect(showSecretsCheck_, &QCheckBox::toggled, this, [this](bool checked) { setSecretVisibility(checked); });
    scroll->setWidget(content);
    outer->addWidget(scroll);
    return page;
}

QWidget *MainWindow::createLogsPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 24, 28, 22);
    auto *header = new QHBoxLayout;
    auto *title = new QLabel(QStringLiteral("日志"));
    title->setObjectName(QStringLiteral("pageTitle"));
    header->addWidget(title);
    header->addStretch(1);
    logSearch_ = new QLineEdit;
    logSearch_->setPlaceholderText(QStringLiteral("搜索日志"));
    logErrorsOnly_ = new QCheckBox(QStringLiteral("仅错误"));
    auto *pause = new QCheckBox(QStringLiteral("暂停跟随"));
    auto *copy = button(QStringLiteral("复制脱敏日志"));
    auto *open = button(QStringLiteral("打开日志目录"));
    header->addWidget(logSearch_);
    header->addWidget(logErrorsOnly_);
    header->addWidget(pause);
    header->addWidget(copy);
    header->addWidget(open);
    layout->addLayout(header);
    logView_ = new QPlainTextEdit;
    logView_->setReadOnly(true);
    logView_->setMaximumBlockCount(1000);
    logView_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    layout->addWidget(logView_, 1);
    connect(pause, &QCheckBox::toggled, this, [this](bool checked) { logsPaused_ = checked; });
    connect(logSearch_, &QLineEdit::textChanged, this, [this] { updateLogView(); });
    connect(logErrorsOnly_, &QCheckBox::toggled, this, [this] { updateLogView(); });
    connect(copy, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(logView_->toPlainText());
        statusBar()->showMessage(QStringLiteral("脱敏日志已复制"), 2500);
    });
    connect(open, &QPushButton::clicked, this, [this] { openLogsDirectory(); });
    return page;
}

QWidget *MainWindow::createDiagnosticsPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 24, 28, 22);
    auto *header = new QHBoxLayout;
    auto *title = new QLabel(QStringLiteral("诊断"));
    title->setObjectName(QStringLiteral("pageTitle"));
    header->addWidget(title);
    header->addStretch(1);
    auto *run = button(QStringLiteral("运行诊断"), true);
    auto *exportButton = button(QStringLiteral("导出脱敏报告"));
    header->addWidget(run);
    header->addWidget(exportButton);
    layout->addLayout(header);
    auto *hint = new QLabel(QStringLiteral("诊断只检查本地状态和接口，不发起聊天请求，不读取凭证内容。"));
    hint->setObjectName(QStringLiteral("hint"));
    hint->setWordWrap(true);
    layout->addWidget(hint);
    diagnosticsView_ = new QPlainTextEdit;
    diagnosticsView_->setReadOnly(true);
    diagnosticsView_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    layout->addWidget(diagnosticsView_, 1);
    connect(run, &QPushButton::clicked, this, [this] { runDiagnostics(); });
    connect(exportButton, &QPushButton::clicked, this, [this] { exportDiagnostics(); });
    return page;
}

void MainWindow::setupTray()
{
    tray_ = new QSystemTrayIcon(this);
    const QIcon appIcon = windowIcon().isNull() ? style()->standardIcon(QStyle::SP_ComputerIcon) : windowIcon();
    tray_->setIcon(appIcon);
    tray_->setToolTip(QStringLiteral("workbuddy2api 本地管理器（双击打开）"));
    auto *menu = new QMenu(this);
    auto *open = menu->addAction(QStringLiteral("打开管理面板"));
    menu->addSeparator();
    auto *start = menu->addAction(QStringLiteral("启动服务"));
    auto *stop = menu->addAction(QStringLiteral("停止服务"));
    auto *restart = menu->addAction(QStringLiteral("重启服务"));
    menu->addSeparator();
    auto *logs = menu->addAction(QStringLiteral("打开日志目录"));
    auto *config = menu->addAction(QStringLiteral("打开配置目录"));
    menu->addSeparator();
    auto *quit = menu->addAction(QStringLiteral("退出并停止服务"));
    tray_->setContextMenu(menu);
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        tray_->show();
    }
    connect(open, &QAction::triggered, this, [this] { showAndActivate(); });
    connect(start, &QAction::triggered, this, [this] { startService(); });
    connect(stop, &QAction::triggered, this, [this] { stopService(); });
    connect(restart, &QAction::triggered, this, [this] { restartService(); });
    connect(logs, &QAction::triggered, this, [this] { openLogsDirectory(); });
    connect(config, &QAction::triggered, this, [this] { openConfigDirectory(); });
    connect(quit, &QAction::triggered, this, [this] { exitManager(); });
    connect(tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::DoubleClick) {
            showAndActivate();
        }
    });
}

void MainWindow::setupStyles()
{
    setStyleSheet(QStringLiteral(
        "QMainWindow { background: #f5f7fb; }"
        "QWidget#sidebar { background: #111827; }"
        "QLabel#brand { color: #f8fafc; font-size: 18px; font-weight: 700; padding: 4px; }"
        "QLabel#sideHint { color: #94a3b8; padding: 4px; }"
        "QListWidget#navigation { background: transparent; border: 0; color: #cbd5e1; outline: 0; }"
        "QListWidget#navigation::item { padding: 11px 12px; border-radius: 7px; margin: 2px 0; }"
        "QListWidget#navigation::item:selected { background: #2563eb; color: white; }"
        "QLabel#pageTitle { color: #172033; font-size: 24px; font-weight: 700; }"
        "QLabel#endpoint, QLabel#muted { color: #64748b; }"
        "QLabel#serviceBadge { padding: 6px 12px; border-radius: 12px; background: #e2e8f0; color: #475569; font-weight: 600; }"
        "QGroupBox { border: 1px solid #dbe3ef; border-radius: 10px; margin-top: 8px; padding-top: 12px; color: #334155; font-weight: 600; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 5px; background: #f5f7fb; }"
        "QWidget#statCard { background: #ffffff; border: 1px solid #e5eaf2; border-radius: 8px; }"
        "QLabel#statCaption { color: #64748b; font-size: 12px; font-weight: 500; }"
        "QLabel#statValue { color: #172033; font-size: 20px; font-weight: 700; }"
        "QLabel#hint { color: #475569; background: #eef5ff; border: 1px solid #d5e6ff; border-radius: 8px; padding: 8px 10px; }"
        "QPushButton { background: #ffffff; border: 1px solid #cbd5e1; border-radius: 6px; padding: 7px 14px; color: #1e293b; }"
        "QPushButton:hover { background: #f1f5f9; }"
        "QPushButton:disabled { color: #94a3b8; background: #f8fafc; }"
        "QPushButton#primaryButton { background: #2563eb; border-color: #2563eb; color: #ffffff; }"
        "QPushButton#primaryButton:hover { background: #1d4ed8; }"
        "QTableWidget { background: #ffffff; alternate-background-color: #f8fafc; border: 1px solid #e5eaf2; border-radius: 6px; gridline-color: transparent; }"
        "QTableWidget::item { padding: 5px; }"
        "QTableWidget::item:selected { background: #dbeafe; color: #172033; }"
        "QHeaderView::section { background: #f1f5f9; border: 0; border-bottom: 1px solid #dbe3ef; padding: 7px; color: #475569; font-weight: 600; }"
        "QLineEdit, QComboBox, QSpinBox { background: #ffffff; border: 1px solid #cbd5e1; border-radius: 5px; padding: 6px; }"
        "QPlainTextEdit { background: #0f172a; color: #dbeafe; border: 1px solid #1e293b; border-radius: 6px; }"));
}

void MainWindow::applyTheme(const QString &theme)
{
    if (theme == QStringLiteral("dark")) {
        setStyleSheet(QStringLiteral(
            "QMainWindow { background: #0f172a; color: #e2e8f0; }"
            "QWidget#sidebar { background: #020617; } QLabel#brand { color: #f8fafc; font-size: 18px; font-weight: 700; }"
            "QLabel#sideHint, QLabel#endpoint, QLabel#muted { color: #94a3b8; }"
            "QListWidget#navigation { background: transparent; border: 0; color: #cbd5e1; }"
            "QListWidget#navigation::item { padding: 11px 12px; border-radius: 7px; margin: 2px 0; }"
            "QListWidget#navigation::item:selected { background: #2563eb; color: white; }"
            "QLabel#pageTitle { color: #f8fafc; font-size: 24px; font-weight: 700; }"
            "QLabel#serviceBadge { padding: 6px 12px; border-radius: 12px; background: #334155; color: #e2e8f0; font-weight: 600; }"
            "QGroupBox { border: 1px solid #334155; border-radius: 10px; margin-top: 8px; padding-top: 12px; color: #cbd5e1; font-weight: 600; }"
            "QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 5px; background: #0f172a; }"
            "QWidget#statCard { background: #1e293b; border: 1px solid #334155; border-radius: 8px; }"
            "QLabel#statCaption { color: #94a3b8; font-size: 12px; } QLabel#statValue { color: #f8fafc; font-size: 20px; font-weight: 700; }"
            "QLabel#hint { color: #bfdbfe; background: #172554; border: 1px solid #1d4ed8; border-radius: 8px; padding: 8px 10px; }"
            "QPushButton, QLineEdit, QComboBox, QSpinBox { background: #1e293b; border: 1px solid #475569; border-radius: 6px; padding: 7px 12px; color: #e2e8f0; }"
            "QPushButton:hover { background: #334155; } QTableWidget { background: #1e293b; alternate-background-color: #172033; color: #e2e8f0; border: 1px solid #334155; }"
            "QHeaderView::section { background: #334155; color: #e2e8f0; padding: 7px; border: 0; }"
            "QPlainTextEdit { background: #020617; color: #dbeafe; border: 1px solid #334155; }"));
    } else {
        setupStyles();
    }
}

QNetworkRequest MainWindow::requestFor(const QUrl &url) const
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");
    if (!apiKey_.isEmpty()) {
        request.setRawHeader("Authorization", QByteArray("Bearer ") + apiKey_.toUtf8());
    }
    return request;
}

void MainWindow::requestJson(const QString &method, const QString &path, const QByteArray &body,
                             const JsonCallback &callback)
{
    QUrl url = baseUrl_.resolved(QUrl(path));
    QNetworkRequest request = requestFor(url);
    QNetworkReply *reply = nullptr;
    if (method == QStringLiteral("GET")) {
        reply = network_->get(request);
    } else if (method == QStringLiteral("POST")) {
        reply = network_->post(request, body);
    } else {
        reply = network_->sendCustomRequest(request, method.toUtf8(), body);
    }
    connect(reply, &QNetworkReply::finished, this, [reply, callback] {
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray raw = reply->readAll();
        const bool transportOk = reply->error() == QNetworkReply::NoError;
        QJsonObject object;
        QString error;
        if (!raw.trimmed().isEmpty()) {
            QJsonParseError parseError;
            const QJsonDocument document = QJsonDocument::fromJson(raw, &parseError);
            if (document.isObject() && parseError.error == QJsonParseError::NoError) {
                object = document.object();
                const QJsonObject errorObject = object.value(QStringLiteral("error")).toObject();
                error = errorObject.value(QStringLiteral("message")).toString();
                if (error.isEmpty()) {
                    error = object.value(QStringLiteral("message")).toString();
                }
            } else if (!transportOk) {
                error = QString::fromUtf8(raw).trimmed();
            }
        }
        if (error.isEmpty() && !transportOk) {
            error = reply->errorString();
        }
        const bool ok = transportOk && (httpStatus == 0 || (httpStatus >= 200 && httpStatus < 300));
        callback(ok, httpStatus, object, error);
        reply->deleteLater();
    });
}

void MainWindow::refreshAll()
{
    fetchStatus();
    fetchModels();
    fetchUsage();
    fetchTaskHistory();
}

void MainWindow::fetchStatus()
{
    if (statusRequestPending_) {
        return;
    }
    statusRequestPending_ = true;
    requestJson(QStringLiteral("GET"), QStringLiteral("/status"), {},
                [this](bool ok, int status, const QJsonObject &object, const QString &error) {
                    statusRequestPending_ = false;
                    if (!ok) {
                        handleGatewayFailure(QStringLiteral("状态接口"), status, error);
                        return;
                    }
                    lastPollError_.clear();
                    applyStatus(object);
                    setGatewayState(true);
                });
}

void MainWindow::fetchModels()
{
    if (modelsRequestPending_) {
        return;
    }
    modelsRequestPending_ = true;
    requestJson(QStringLiteral("GET"), QStringLiteral("/v1/models"), {},
                [this](bool ok, int status, const QJsonObject &object, const QString &error) {
                    modelsRequestPending_ = false;
                    if (!ok) {
                        if (status == 401) {
                            appendLog(QStringLiteral("模型接口鉴权失败，请检查 API Key"));
                        } else {
                            appendLog(QStringLiteral("模型接口暂时不可用：%1").arg(error));
                        }
                        return;
                    }
                    applyModels(object.value(QStringLiteral("data")).toArray());
                });
}

void MainWindow::fetchUsage()
{
    if (usageRequestPending_ || !usageRange_) {
        return;
    }
    usageRequestPending_ = true;
    const QString range = usageRange_->currentData().toString();
    requestJson(QStringLiteral("GET"), QStringLiteral("/usage?range=") + range, {},
                [this](bool ok, int status, const QJsonObject &object, const QString &error) {
                    usageRequestPending_ = false;
                    if (!ok) {
                        if (status != 0) {
                            appendLog(QStringLiteral("用量接口暂时不可用：HTTP %1").arg(status));
                        } else {
                            appendLog(QStringLiteral("用量接口暂时不可用：%1").arg(error));
                        }
                        return;
                    }
                    applyUsage(object);
                });
}

void MainWindow::fetchTaskHistory()
{
    if (!taskHistoryTable_) {
        return;
    }
    requestJson(QStringLiteral("GET"), QStringLiteral("/admin/tasks/history"), {},
                [this](bool ok, int status, const QJsonObject &object, const QString &error) {
                    if (!ok) {
                        if (status != 401 && status != 404) {
                            appendLog(QStringLiteral("任务历史暂时不可用：%1").arg(error.isEmpty()
                                                                                  ? QStringLiteral("HTTP %1").arg(status)
                                                                                  : error));
                        }
                        return;
                    }
                    lastTaskHistory_ = object.value(QStringLiteral("runs")).toArray();
                    refreshTasksPage();
                });
}

void MainWindow::refreshTasksPage()
{
    if (!tasksTable_) {
        return;
    }
    const QJsonObject root = config_.object();
    const QStringList taskIds = {QStringLiteral("checkin"), QStringLiteral("activity"), QStringLiteral("travel"),
                                 QStringLiteral("keepalive"), QStringLiteral("school"), QStringLiteral("cat")};
    const QStringList scheduleKeys = {QStringLiteral("checkin_hours"), QStringLiteral("activity_hours"), QStringLiteral("travel_hours"),
                                      QStringLiteral("keepalive_hours"), QStringLiteral("school_hours"), QStringLiteral("cat_hours")};
    const QStringList defaults = {QStringLiteral("9,21"), QStringLiteral("10"), QStringLiteral("9,21"),
                                  QStringLiteral("22"), QStringLiteral("12"), QStringLiteral("1")};
    const QJsonObject schedule = configSection(root, QStringLiteral("schedule"));
    for (int row = 0; row < taskIds.size() && row < tasksTable_->rowCount(); ++row) {
        const QString task = taskIds.at(row);
        const bool enabled = schedule.value(task + QStringLiteral("_enabled")).toBool(true);
        const QString hours = hoursText(root, scheduleKeys.at(row), defaults.at(row));
        setCell(tasksTable_, row, 2, enabled ? QStringLiteral("启用") : QStringLiteral("关闭"));
        setCell(tasksTable_, row, 3, enabled ? hours : QStringLiteral("已关闭 · %1").arg(hours));
        QJsonObject latest;
        for (const QJsonValue &value : lastTaskHistory_) {
            const QJsonObject candidate = value.toObject();
            if (candidate.value(QStringLiteral("task")).toString() == task) {
                latest = candidate;
                break;
            }
        }
        if (latest.isEmpty()) {
            setCell(tasksTable_, row, 4, QStringLiteral("从未运行"));
            setCell(tasksTable_, row, 5, QStringLiteral("—"));
        } else {
            setCell(tasksTable_, row, 4, formatTime(latest.value(QStringLiteral("started_at")).toString()));
            const QString status = latest.value(QStringLiteral("status")).toString();
            setCell(tasksTable_, row, 5, status == QStringLiteral("success") ? QStringLiteral("成功")
                                                                                : status == QStringLiteral("cancelled") ? QStringLiteral("已取消")
                                                                                                                         : QStringLiteral("失败"),
                    latest.value(QStringLiteral("error")).toString());
        }
        if (taskButtons_.contains(task)) {
            taskButtons_.value(task)->setEnabled(gatewayOnline_ && enabled && !taskRunner_.busy());
        }
    }

    if (!taskHistoryTable_) {
        return;
    }
    const QString filter = taskHistoryFilter_ ? taskHistoryFilter_->currentData().toString() : QString();
    taskHistoryTable_->setRowCount(0);
    int historyIndex = 0;
    for (const QJsonValue &value : lastTaskHistory_) {
        const QJsonObject run = value.toObject();
        const QString task = run.value(QStringLiteral("task")).toString();
        const int currentIndex = historyIndex++;
        if (!filter.isEmpty() && filter != task) {
            continue;
        }
        const int row = taskHistoryTable_->rowCount();
        taskHistoryTable_->insertRow(row);
        setCell(taskHistoryTable_, row, 0, taskLabel(task));
        taskHistoryTable_->item(row, 0)->setData(Qt::UserRole, currentIndex);
        setCell(taskHistoryTable_, row, 1, formatTime(run.value(QStringLiteral("started_at")).toString()));
        setCell(taskHistoryTable_, row, 2, formatTime(run.value(QStringLiteral("finished_at")).toString()));
        const QString status = run.value(QStringLiteral("status")).toString();
        setCell(taskHistoryTable_, row, 3, status == QStringLiteral("success") ? QStringLiteral("成功")
                                                                                 : status == QStringLiteral("cancelled") ? QStringLiteral("已取消")
                                                                                                                          : QStringLiteral("失败"));
        const QString summary = run.value(QStringLiteral("summary")).toString();
        const QString error = run.value(QStringLiteral("error")).toString();
        const QString detail = error.isEmpty() ? summary : summary + QStringLiteral("\n错误：") + error;
        setCell(taskHistoryTable_, row, 4, summary, detail);
    }
}

void MainWindow::applyStatus(const QJsonObject &status)
{
    lastStatus_ = status;
    const int total = status.value(QStringLiteral("total")).toInt();
    const int healthy = status.value(QStringLiteral("healthy")).toInt();
    const int cooling = status.value(QStringLiteral("cooling")).toInt();
    const int disabled = status.value(QStringLiteral("disabled")).toInt();
    const int full = status.value(QStringLiteral("in_flight_full")).toInt();
    const int sticky = status.value(QStringLiteral("sticky_sessions")).toInt();
    const QString redisMode = status.value(QStringLiteral("redis_mode")).toString();
    overviewService_->setText(QStringLiteral("运行中"));
    overviewAccounts_->setText(QString::number(total));
    overviewHealthy_->setText(QString::number(healthy));
    overviewCooling_->setText(QStringLiteral("%1 / %2").arg(cooling).arg(disabled));
    overviewHint_->setText(QStringLiteral("已加载 %1 个账号，其中 %2 个健康；冷却 %3 个、禁用 %4 个。"
                                         "在途满载 %5 个，会话粘性 %6 个，Redis 镜像：%7。")
                               .arg(total).arg(healthy).arg(cooling).arg(disabled).arg(full).arg(sticky)
                               .arg(redisMode.isEmpty() ? QStringLiteral("未返回")
                                                        : redisMode == QStringLiteral("upstash") ? QStringLiteral("Upstash")
                                                                                                  : QStringLiteral("本地内存")));
    if (redisStatus_) {
        redisStatus_->setText(redisMode == QStringLiteral("upstash")
                                  ? QStringLiteral("已启用 Upstash Redis 状态镜像")
                                  : QStringLiteral("当前为本地内存模式；填写 URL/Token 后保存并重启即可启用"));
    }
    if (overviewRequests_->text() == QStringLiteral("—")) {
        overviewRequests_->setText(QStringLiteral("0"));
    }
    endpointLabel_->setText(QStringLiteral("本地接口：%1").arg(baseUrl_.toString()));

    accountsTable_->setRowCount(0);
    for (const QJsonValue &value : status.value(QStringLiteral("accounts")).toArray()) {
        const QJsonObject account = value.toObject();
        const int row = accountsTable_->rowCount();
        accountsTable_->insertRow(row);
        const QString uid = jsonString(account, QStringLiteral("uid"));
        const QString realm = jsonString(account, QStringLiteral("realm")).isEmpty()
                                  ? QStringLiteral("cn")
                                  : jsonString(account, QStringLiteral("realm"));
        const bool disabledFlag = account.value(QStringLiteral("disabled")).toBool();
        const bool coolingFlag = account.value(QStringLiteral("cooling")).toBool();
        const QString reason = disabledFlag ? jsonString(account, QStringLiteral("disabled_reason"))
                                            : jsonString(account, QStringLiteral("reason"));
        setCell(accountsTable_, row, 0, maskUid(uid), uid);
        accountsTable_->item(row, 0)->setData(Qt::UserRole, uid);
        setCell(accountsTable_, row, 1, jsonString(account, QStringLiteral("nickname")).isEmpty()
                                           ? QStringLiteral("—")
                                           : jsonString(account, QStringLiteral("nickname")));
        setCell(accountsTable_, row, 2, realm);
        setCell(accountsTable_, row, 3, countText(jsonInt64(account, QStringLiteral("credits"))));
        setCell(accountsTable_, row, 4, stateForAccount(account, config_.maxInFlight()), reason);
        setCell(accountsTable_, row, 5,
                QStringLiteral("%1 / %2")
                    .arg(account.value(QStringLiteral("in_flight")).toInt())
                    .arg(config_.maxInFlight() > 0 ? QString::number(config_.maxInFlight()) : QStringLiteral("∞")));
        setCell(accountsTable_, row, 6,
                QStringLiteral("%1 / %2")
                    .arg(jsonInt64(account, QStringLiteral("success_count")))
                    .arg(jsonInt64(account, QStringLiteral("err_total"))));
        QString cooldown = QStringLiteral("—");
        const qint64 remain = jsonInt64(account, QStringLiteral("cool_remaining_sec"));
        if (remain > 0) {
            cooldown = QStringLiteral("约 %1 分钟").arg((remain + 59) / 60);
        } else if (!formatTime(jsonString(account, QStringLiteral("until"))).isEmpty()) {
            cooldown = formatTime(jsonString(account, QStringLiteral("until")));
        }
        setCell(accountsTable_, row, 7, coolingFlag ? cooldown : (reason.isEmpty() ? QStringLiteral("—") : reason),
                reason);
    }
    statusBar()->showMessage(QStringLiteral("状态已更新 · %1")
                                 .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"))));
}

void MainWindow::applyModels(const QJsonArray &models)
{
    lastModels_ = models;
    updateChatModels();
    if (!modelsTable_) {
        return;
    }
    const QString search = modelSearch_ ? modelSearch_->text().trimmed() : QString();
    const QString realm = modelRealm_ ? modelRealm_->currentData().toString() : QStringLiteral("all");
    modelsTable_->setRowCount(0);
    for (const QJsonValue &value : models) {
        const QJsonObject model = value.toObject();
        const QString id = jsonString(model, QStringLiteral("id"));
        const QString name = jsonString(model, QStringLiteral("name"));
        const QString description = jsonString(model, QStringLiteral("description"));
        const QString haystack = (id + QLatin1Char(' ') + name + QLatin1Char(' ') + description).toLower();
        if (!search.isEmpty() && !haystack.contains(search.toLower())) {
            continue;
        }
        const bool isGlobal = id.startsWith(QStringLiteral("global:"), Qt::CaseInsensitive);
        if (realm == QStringLiteral("global") && !isGlobal) {
            continue;
        }
        if (realm == QStringLiteral("cn") && isGlobal) {
            continue;
        }
        const int row = modelsTable_->rowCount();
        modelsTable_->insertRow(row);
        setCell(modelsTable_, row, 0, id, description);
        setCell(modelsTable_, row, 1, name.isEmpty() ? QStringLiteral("—") : name);
        setCell(modelsTable_, row, 2, jsonString(model, QStringLiteral("credits")).isEmpty()
                                           ? QStringLiteral("—")
                                           : jsonString(model, QStringLiteral("credits")));
        setCell(modelsTable_, row, 3, model.value(QStringLiteral("context_length")).toInt() > 0
                                           ? QString::number(model.value(QStringLiteral("context_length")).toInt())
                                           : QStringLiteral("—"));
        setCell(modelsTable_, row, 4, model.value(QStringLiteral("max_output_tokens")).toInt() > 0
                                           ? QString::number(model.value(QStringLiteral("max_output_tokens")).toInt())
                                           : QStringLiteral("—"));
        const QStringList capabilities = modelCapabilities(model);
        setCell(modelsTable_, row, 5,
                capabilities.isEmpty() ? QStringLiteral("—") : capabilities.join(QStringLiteral("、")),
                description);
    }
    overviewModels_->setText(QString::number(modelsTable_->rowCount()));
}

void MainWindow::updateChatModels()
{
    if (!chatModelCombo_) {
        return;
    }
    const QString current = chatModelCombo_->currentData().toString().isEmpty()
                                ? chatModelCombo_->currentText()
                                : chatModelCombo_->currentData().toString();
    chatModelCombo_->clear();
    QStringList ids;
    for (const QJsonValue &value : lastModels_) {
        const QString id = value.toObject().value(QStringLiteral("id")).toString();
        if (!id.isEmpty() && !ids.contains(id)) {
            ids << id;
            chatModelCombo_->addItem(id, id);
        }
    }
    if (ids.isEmpty()) {
        chatModelCombo_->addItem(current.isEmpty() ? QStringLiteral("glm-5.2") : current,
                                 current.isEmpty() ? QStringLiteral("glm-5.2") : current);
    }
    const int index = chatModelCombo_->findData(current);
    chatModelCombo_->setCurrentIndex(index >= 0 ? index : 0);
}

void MainWindow::sendChatTest()
{
    if (chatReply_) {
        QMessageBox::information(this, QStringLiteral("请求进行中"), QStringLiteral("请等待当前响应完成，或点击停止。"));
        return;
    }
    const QString userText = chatUserEdit_ ? chatUserEdit_->toPlainText().trimmed() : QString();
    if (userText.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("缺少 User 消息"), QStringLiteral("请先输入一条测试消息。"));
        return;
    }
    if (!chatConfirmed_) {
        if (QMessageBox::question(this, QStringLiteral("确认发送聊天测试"),
                                  QStringLiteral("聊天测试会通过本地网关访问上游，并可能消耗账号积分/额度。确认继续吗？"),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
            != QMessageBox::Yes) {
            return;
        }
        chatConfirmed_ = true;
    }
    QString model = chatModelCombo_->currentData().toString();
    if (model.isEmpty()) {
        model = chatModelCombo_->currentText().trimmed();
    }
    if (model.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("缺少模型"), QStringLiteral("请先刷新模型列表或填写模型 ID。"));
        return;
    }
    QJsonArray messages;
    const QString systemText = chatSystemEdit_->toPlainText().trimmed();
    if (!systemText.isEmpty()) {
        messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                                    {QStringLiteral("content"), systemText}});
    }
    messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                {QStringLiteral("content"), userText}});
    const bool stream = chatStreamCheck_->isChecked();
    const QJsonObject body{{QStringLiteral("model"), model},
                           {QStringLiteral("messages"), messages},
                           {QStringLiteral("max_tokens"), chatMaxTokensSpin_->value()},
                           {QStringLiteral("temperature"), chatTemperatureSpin_->value()},
                           {QStringLiteral("stream"), stream}};
    QNetworkRequest request = requestFor(baseUrl_.resolved(QUrl(QStringLiteral("/v1/chat/completions"))));
    request.setRawHeader("Accept", stream ? QByteArray("text/event-stream") : QByteArray("application/json"));
    chatReply_ = network_->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    chatStartedAt_ = QDateTime::currentDateTime();
    chatStreamBuffer_.clear();
    chatAccumulated_.clear();
    chatResponseEdit_->clear();
    chatStatus_->setText(QStringLiteral("请求中……"));
    QNetworkReply *reply = chatReply_;

    auto consumeSseLine = [this](const QByteArray &line) {
        const QByteArray trimmed = line.trimmed();
        if (!trimmed.startsWith("data:")) {
            return;
        }
        const QByteArray data = trimmed.mid(5).trimmed();
        if (data.isEmpty() || data == "[DONE]") {
            return;
        }
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            return;
        }
        const QJsonObject object = document.object();
        const QJsonArray choices = object.value(QStringLiteral("choices")).toArray();
        if (!choices.isEmpty()) {
            const QJsonObject choice = choices.first().toObject();
            const QJsonObject delta = choice.value(QStringLiteral("delta")).toObject();
            QString text = delta.value(QStringLiteral("content")).toString();
            if (text.isEmpty()) {
                text = delta.value(QStringLiteral("reasoning_content")).toString();
            }
            if (!text.isEmpty()) {
                chatAccumulated_ += text;
                chatResponseEdit_->setPlainText(chatAccumulated_);
                chatResponseEdit_->verticalScrollBar()->setValue(chatResponseEdit_->verticalScrollBar()->maximum());
            }
        }
    };

    connect(reply, &QNetworkReply::readyRead, this, [this, reply, stream, consumeSseLine] {
        if (!stream) {
            return;
        }
        chatStreamBuffer_ += reply->readAll();
        int newline = -1;
        while ((newline = chatStreamBuffer_.indexOf('\n')) >= 0) {
            const QByteArray line = chatStreamBuffer_.left(newline);
            chatStreamBuffer_.remove(0, newline + 1);
            consumeSseLine(line);
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, stream, consumeSseLine] {
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray raw = reply->readAll();
        if (stream) {
            chatStreamBuffer_ += raw;
            if (!chatStreamBuffer_.isEmpty()) {
                const QList<QByteArray> lines = chatStreamBuffer_.split('\n');
                for (const QByteArray &line : lines) {
                    consumeSseLine(line);
                }
            }
        } else if (!raw.trimmed().isEmpty()) {
            QJsonParseError parseError;
            const QJsonDocument document = QJsonDocument::fromJson(raw, &parseError);
            if (document.isObject() && parseError.error == QJsonParseError::NoError) {
                const QJsonObject object = document.object();
                if (httpStatus >= 400 || reply->error() != QNetworkReply::NoError) {
                    const QJsonObject errorObject = object.value(QStringLiteral("error")).toObject();
                    const QString message = errorObject.value(QStringLiteral("message")).toString().isEmpty()
                                                 ? QStringLiteral("HTTP %1").arg(httpStatus)
                                                 : errorObject.value(QStringLiteral("message")).toString();
                    chatResponseEdit_->setPlainText(LogDiagnostics::sanitize(message.left(800)));
                } else {
                    const QJsonArray choices = object.value(QStringLiteral("choices")).toArray();
                    if (!choices.isEmpty()) {
                        const QJsonObject choice = choices.first().toObject();
                        const QJsonObject message = choice.value(QStringLiteral("message")).toObject();
                        chatResponseEdit_->setPlainText(message.value(QStringLiteral("content")).toString());
                    } else {
                        chatResponseEdit_->setPlainText(QString::fromUtf8(raw.left(4000)));
                    }
                    const QJsonObject usage = object.value(QStringLiteral("usage")).toObject();
                    if (!usage.isEmpty()) {
                        chatAccumulated_ += QStringLiteral("\n\n[usage] prompt=%1 completion=%2 total=%3 credit=%4")
                                                .arg(usage.value(QStringLiteral("prompt_tokens")).toInt())
                                                .arg(usage.value(QStringLiteral("completion_tokens")).toInt())
                                                .arg(usage.value(QStringLiteral("total_tokens")).toInt())
                                                .arg(usage.contains(QStringLiteral("credit"))
                                                         ? QString::number(usage.value(QStringLiteral("credit")).toDouble(), 'f', 2)
                                                         : QStringLiteral("—"));
                    }
                }
            } else {
                chatResponseEdit_->setPlainText(QStringLiteral("响应不是有效 JSON（HTTP %1）").arg(httpStatus));
            }
        }
        const bool ok = reply->error() == QNetworkReply::NoError && (httpStatus == 0 || httpStatus < 400);
        const qint64 elapsed = chatStartedAt_.msecsTo(QDateTime::currentDateTime());
        chatStatus_->setText(ok ? QStringLiteral("完成 · %1 ms").arg(elapsed)
                                : QStringLiteral("失败 · HTTP %1 · %2 ms").arg(httpStatus).arg(elapsed));
        if (!ok && chatResponseEdit_->toPlainText().isEmpty()) {
            chatResponseEdit_->setPlainText(LogDiagnostics::sanitize(reply->errorString()));
        }
        chatReply_ = nullptr;
        reply->deleteLater();
        fetchUsage();
    });
}

void MainWindow::stopChatTest()
{
    if (!chatReply_) {
        return;
    }
    chatStatus_->setText(QStringLiteral("正在停止……"));
    chatReply_->abort();
}

void MainWindow::applyUsage(const QJsonObject &usage)
{
    lastUsage_ = usage;
    const QJsonObject summary = usage.value(QStringLiteral("summary")).toObject();
    usageRequests_->setText(countText(jsonInt64(summary, QStringLiteral("requests"))));
    usageSuccess_->setText(successRate(summary));
    usageTokens_->setText(optionalCount(summary, QStringLiteral("total_tokens"), QStringLiteral("token_samples")));
    usageCredit_->setText(creditText(summary));
    overviewRequests_->setText(countText(jsonInt64(summary, QStringLiteral("requests"))));
    overviewSuccess_->setText(successRate(summary));
    overviewTokens_->setText(optionalCount(summary, QStringLiteral("total_tokens"), QStringLiteral("token_samples")));

    usageModelsTable_->setRowCount(0);
    for (const QJsonValue &value : usage.value(QStringLiteral("models")).toArray()) {
        const QJsonObject model = value.toObject();
        const int row = usageModelsTable_->rowCount();
        usageModelsTable_->insertRow(row);
        setCell(usageModelsTable_, row, 0, jsonString(model, QStringLiteral("model")));
        setCell(usageModelsTable_, row, 1, countText(jsonInt64(model, QStringLiteral("requests"))));
        setCell(usageModelsTable_, row, 2, countText(jsonInt64(model, QStringLiteral("successes"))));
        setCell(usageModelsTable_, row, 3, countText(jsonInt64(model, QStringLiteral("failures"))));
        setCell(usageModelsTable_, row, 4, optionalCount(model, QStringLiteral("prompt_tokens"), QStringLiteral("token_samples")));
        setCell(usageModelsTable_, row, 5, optionalCount(model, QStringLiteral("completion_tokens"), QStringLiteral("token_samples")));
        setCell(usageModelsTable_, row, 6, optionalCount(model, QStringLiteral("total_tokens"), QStringLiteral("token_samples")));
        setCell(usageModelsTable_, row, 7, creditText(model));
    }
    usageDaysTable_->setRowCount(0);
    for (const QJsonValue &value : usage.value(QStringLiteral("days")).toArray()) {
        const QJsonObject day = value.toObject();
        const int row = usageDaysTable_->rowCount();
        usageDaysTable_->insertRow(row);
        setCell(usageDaysTable_, row, 0, jsonString(day, QStringLiteral("date")));
        setCell(usageDaysTable_, row, 1, countText(jsonInt64(day, QStringLiteral("requests"))));
        setCell(usageDaysTable_, row, 2, countText(jsonInt64(day, QStringLiteral("successes"))));
        setCell(usageDaysTable_, row, 3, countText(jsonInt64(day, QStringLiteral("failures"))));
        setCell(usageDaysTable_, row, 4, optionalCount(day, QStringLiteral("total_tokens"), QStringLiteral("token_samples")));
        setCell(usageDaysTable_, row, 5, creditText(day));
    }
}

void MainWindow::handleGatewayFailure(const QString &endpoint, int status, const QString &error)
{
    QString reason;
    if (status == 401) {
        reason = QStringLiteral("API Key 校验失败");
    } else if (status > 0) {
        reason = QStringLiteral("HTTP %1").arg(status);
    } else {
        reason = error.isEmpty() ? QStringLiteral("服务未启动或连接被拒绝") : error;
    }
    setGatewayState(false, reason);
    if (lastPollError_ != reason) {
        appendLog(QStringLiteral("%1：%2").arg(endpoint, reason));
        lastPollError_ = reason;
    }
    maybeAutoRecover();
}

void MainWindow::setGatewayState(bool online, const QString &message)
{
    if (gatewayOnline_ != online) {
        if (online) {
            notify(QStringLiteral("网关已连接"), QStringLiteral("本地服务状态正常"));
        } else {
            notify(QStringLiteral("网关不可用"), message.isEmpty() ? QStringLiteral("请检查服务状态") : message,
                   QSystemTrayIcon::Warning);
        }
    }
    gatewayOnline_ = online;
    serviceBadge_->setText(online ? QStringLiteral("运行中") : QStringLiteral("未连接"));
    serviceBadge_->setStyleSheet(online ? QStringLiteral("background:#dcfce7;color:#166534;padding:6px 12px;border-radius:12px;font-weight:600;")
                                        : QStringLiteral("background:#fee2e2;color:#991b1b;padding:6px 12px;border-radius:12px;font-weight:600;"));
    if (online) {
        recoveryInProgress_ = false;
        lastGatewayMessage_.clear();
    }
    refreshTasksPage();
}

void MainWindow::maybeAutoRecover()
{
    if (managerSettings_.value(QStringLiteral("manualStop"), false).toBool() || recoveryInProgress_) {
        return;
    }
    if (!initialAutoStartAttempted_ && managerSettings_.value(QStringLiteral("autoOpenGateway"), true).toBool()) {
        initialAutoStartAttempted_ = true;
        appendLog(QStringLiteral("网关未运行，按设置自动启动"));
        recoveryInProgress_ = service_.start();
        return;
    }
    if (!managerSettings_.value(QStringLiteral("autoRecovery"), false).toBool()) {
        return;
    }
    const QDateTime cutoff = QDateTime::currentDateTime().addSecs(-600);
    recoveryTimes_.erase(std::remove_if(recoveryTimes_.begin(), recoveryTimes_.end(),
                                        [&cutoff](const QDateTime &value) { return value < cutoff; }),
                         recoveryTimes_.end());
    if (recoveryTimes_.size() >= 3) {
        if (lastGatewayMessage_ != QStringLiteral("recovery-limit")) {
            notify(QStringLiteral("自动恢复已暂停"), QStringLiteral("10 分钟内已达到 3 次上限，请检查日志"),
                   QSystemTrayIcon::Warning);
            lastGatewayMessage_ = QStringLiteral("recovery-limit");
        }
        return;
    }
    recoveryTimes_.append(QDateTime::currentDateTime());
    recoveryInProgress_ = service_.start();
    notify(QStringLiteral("正在自动恢复网关"), QStringLiteral("第 %1/3 次").arg(recoveryTimes_.size()));
}

void MainWindow::startService()
{
    managerSettings_.setValue(QStringLiteral("manualStop"), false);
    managerSettings_.sync();
    recoveryInProgress_ = service_.start();
    if (recoveryInProgress_) {
        appendLog(QStringLiteral("正在启动服务……"));
    }
}

void MainWindow::stopService()
{
    managerSettings_.setValue(QStringLiteral("manualStop"), true);
    managerSettings_.sync();
    restartAfterStop_ = false;
    if (service_.stop()) {
        appendLog(QStringLiteral("正在停止服务……"));
    }
}

void MainWindow::restartService()
{
    managerSettings_.setValue(QStringLiteral("manualStop"), false);
    managerSettings_.sync();
    restartAfterStop_ = false;
    recoveryInProgress_ = service_.restart();
    if (recoveryInProgress_) {
        appendLog(QStringLiteral("正在重启服务……"));
    }
}

void MainWindow::exitManager()
{
    if (exitAfterServiceStop_ || allowQuit_) {
        return;
    }
    if (service_.busy()) {
        QMessageBox::information(this, QStringLiteral("请稍候"),
                                 QStringLiteral("当前服务操作尚未完成，请完成后再退出管理器。"));
        return;
    }

    managerSettings_.setValue(QStringLiteral("manualStop"), true);
    managerSettings_.sync();
    exitAfterServiceStop_ = true;
    appendLog(QStringLiteral("正在停止服务，完成后退出管理器……"));
    statusBar()->showMessage(QStringLiteral("正在停止服务，完成后退出管理器……"));
    if (!service_.stop()) {
        exitAfterServiceStop_ = false;
        QMessageBox::warning(this, QStringLiteral("退出未完成"),
                             QStringLiteral("无法启动停止服务脚本，管理器仍保持打开。请检查日志。"));
    }
}

void MainWindow::openVerification()
{
    openVerification(QStringLiteral("cn"));
}

void MainWindow::openVerification(const QString &realm)
{
    const QString selectedRealm = realm == QStringLiteral("global") ? QStringLiteral("global") : QStringLiteral("cn");
    if (!taskRunner_.openInteractive(QStringLiteral("login.sh"),
                                     {QStringLiteral("--realm=%1").arg(selectedRealm)},
                                     QStringLiteral("workbuddy2api %1 登录验证")
                                         .arg(selectedRealm == QStringLiteral("global") ? QStringLiteral("Global")
                                                                                         : QStringLiteral("CN")),
                                     proxyEdit_ ? proxyEdit_->text() : QString())) {
        QMessageBox::warning(this, QStringLiteral("无法打开验证"),
                             QStringLiteral("请确认 Git Bash 已安装，并可在项目目录执行 ./login.sh --realm=%1")
                                 .arg(selectedRealm));
        appendLog(QStringLiteral("%1 登录窗口未打开").arg(selectedRealm));
        return;
    }
    appendLog(QStringLiteral("%1 登录验证窗口已打开；授权完成后会自动刷新账号")
                  .arg(selectedRealm == QStringLiteral("global") ? QStringLiteral("Global") : QStringLiteral("CN")));
    statusBar()->showMessage(QStringLiteral("请在浏览器完成 %1 授权，稍后返回查看账号状态")
                                 .arg(selectedRealm == QStringLiteral("global") ? QStringLiteral("Global") : QStringLiteral("CN")));
    notify(QStringLiteral("登录验证窗口已打开"), QStringLiteral("完成授权后返回管理器查看账号"));
    QTimer::singleShot(5000, this, [this] { refreshAll(); });
    QTimer::singleShot(12000, this, [this] { refreshAll(); });
}

void MainWindow::showLoginInstructions()
{
    QMessageBox::information(this, QStringLiteral("登录说明"),
                             QStringLiteral("点击“打开验证”后，浏览器会进入 OAuth 页面。完成后回到管理器，账号页会自动刷新。\n\n"
                                             "凭证只保存在项目目录 auths/，管理器不会显示或上传 Token。"));
}

void MainWindow::openLogsDirectory()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(root_));
}

void MainWindow::openConfigDirectory()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(QDir(root_).absolutePath()));
}

void MainWindow::openAdvancedConfig()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(config_.configPath()));
}

QString MainWindow::currentAccountUid() const
{
    if (!accountsTable_ || accountsTable_->currentRow() < 0 || !accountsTable_->item(accountsTable_->currentRow(), 0)) {
        return QString();
    }
    return accountsTable_->item(accountsTable_->currentRow(), 0)->data(Qt::UserRole).toString();
}

void MainWindow::enableSelectedAccount()
{
    const QString uid = currentAccountUid();
    if (uid.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("启用账号"), QStringLiteral("请先选择一个账号。"));
        return;
    }
    performAccountAction(QStringLiteral("enable"), uid);
}

void MainWindow::disableSelectedAccount()
{
    const QString uid = currentAccountUid();
    if (uid.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("禁用账号"), QStringLiteral("请先选择一个账号。"));
        return;
    }
    const QString reason = QStringLiteral("管理器手动禁用");
    performAccountAction(QStringLiteral("disable"), uid,
                         QJsonDocument(QJsonObject{{QStringLiteral("reason"), reason}}).toJson(QJsonDocument::Compact));
}

void MainWindow::deleteSelectedAccount()
{
    const QString uid = currentAccountUid();
    if (uid.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("删除账号"), QStringLiteral("请先选择一个账号。"));
        return;
    }
    if (QMessageBox::warning(this, QStringLiteral("确认删除账号"),
                             QStringLiteral("将删除该账号的本地凭证文件并从账号池移除。此操作不可撤销，确定继续吗？"),
                             QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
        != QMessageBox::Yes) {
        return;
    }
    performAccountAction(QStringLiteral("delete"), uid);
}

void MainWindow::reloginSelectedAccount()
{
    const QString uid = currentAccountUid();
    if (uid.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("重新登录"), QStringLiteral("请先选择一个账号。"));
        return;
    }
    QMessageBox::information(this, QStringLiteral("重新登录"),
                             QStringLiteral("即将打开验证流程。完成授权后，现有账号会按 UID 刷新；当前选中 UID：%1")
                                 .arg(maskUid(uid)));
    openVerification();
}

void MainWindow::importAccountJson()
{
    const QString sourcePath = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择 WorkBuddy 账号凭证"), QDir(root_).absolutePath(),
        QStringLiteral("JSON 文件 (*.json);;所有文件 (*)"));
    if (sourcePath.isEmpty()) {
        return;
    }

    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, QStringLiteral("导入失败"),
                             QStringLiteral("无法读取所选文件：%1").arg(source.errorString()));
        return;
    }
    const QByteArray raw = source.readAll();
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        QMessageBox::warning(this, QStringLiteral("导入失败"),
                             QStringLiteral("文件不是有效的 JSON 对象：%1").arg(parseError.errorString()));
        return;
    }

    const QJsonObject root = document.object();
    const bool nested = root.contains(QStringLiteral("auth"));
    if (nested && !root.value(QStringLiteral("auth")).isObject()) {
        QMessageBox::warning(this, QStringLiteral("导入失败"),
                             QStringLiteral("凭证格式错误：auth 必须是对象。"));
        return;
    }
    const QJsonObject auth = nested ? root.value(QStringLiteral("auth")).toObject() : root;
    const QJsonObject account = nested ? root.value(QStringLiteral("account")).toObject() : root;
    const QString accessToken = auth.value(QStringLiteral("accessToken")).toString().trimmed();
    const QString uid = account.value(QStringLiteral("uid")).toString().trimmed();
    if (accessToken.isEmpty() || uid.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("导入失败"),
                             QStringLiteral("凭证必须包含 accessToken 和账号 UID。"));
        return;
    }

    const QStringList authStringFields = {QStringLiteral("accessToken"), QStringLiteral("refreshToken"),
                                          QStringLiteral("domain"), QStringLiteral("realm")};
    for (const QString &field : authStringFields) {
        if (auth.contains(field) && !auth.value(field).isString()) {
            QMessageBox::warning(this, QStringLiteral("导入失败"),
                                 QStringLiteral("凭证字段 %1 必须是文本。\n文件未导入。")
                                     .arg(field));
            return;
        }
    }
    const QStringList accountStringFields = {QStringLiteral("uid"), QStringLiteral("enterpriseId"),
                                             QStringLiteral("nickname")};
    for (const QString &field : accountStringFields) {
        if (account.contains(field) && !account.value(field).isString()) {
            QMessageBox::warning(this, QStringLiteral("导入失败"),
                                 QStringLiteral("账号字段 %1 必须是文本。\n文件未导入。")
                                     .arg(field));
            return;
        }
    }
    if (auth.contains(QStringLiteral("expiresAt")) && !auth.value(QStringLiteral("expiresAt")).isDouble()) {
        QMessageBox::warning(this, QStringLiteral("导入失败"),
                             QStringLiteral("expiresAt 必须是 Unix 时间戳数字。\n文件未导入。"));
        return;
    }
    if (root.contains(QStringLiteral("device_token")) && !root.value(QStringLiteral("device_token")).isString()) {
        QMessageBox::warning(this, QStringLiteral("导入失败"),
                             QStringLiteral("device_token 必须是文本。\n文件未导入。"));
        return;
    }

    QString authDir = config_.object().value(QStringLiteral("auth_dir")).toString(QStringLiteral("./auths")).trimmed();
    if (authDir.isEmpty()) {
        authDir = QStringLiteral("./auths");
    }
    const QFileInfo authDirInfo(authDir);
    const QString authDirPath = authDirInfo.isAbsolute()
                                    ? QDir::cleanPath(authDir)
                                    : QDir::cleanPath(QDir(root_).absoluteFilePath(authDir));
    if (QFileInfo::exists(authDirPath) && !QFileInfo(authDirPath).isDir()) {
        QMessageBox::warning(this, QStringLiteral("导入失败"),
                             QStringLiteral("auth_dir 指向的路径不是文件夹：%1").arg(authDirPath));
        return;
    }

    const auto uidInDocument = [](const QJsonObject &object) {
        if (object.value(QStringLiteral("auth")).isObject()) {
            return object.value(QStringLiteral("account")).toObject().value(QStringLiteral("uid")).toString().trimmed();
        }
        return object.value(QStringLiteral("uid")).toString().trimmed();
    };
    const QDir authDirectory(authDirPath);
    const QStringList existingFiles = authDirectory.entryList({QStringLiteral("workbuddy*.json")}, QDir::Files,
                                                               QDir::Name);
    for (const QString &name : existingFiles) {
        QFile existing(authDirectory.filePath(name));
        if (!existing.open(QIODevice::ReadOnly)) {
            continue;
        }
        QJsonParseError existingError;
        const QJsonDocument existingDocument = QJsonDocument::fromJson(existing.readAll(), &existingError);
        if (existingError.error == QJsonParseError::NoError && existingDocument.isObject()
            && uidInDocument(existingDocument.object()) == uid) {
            QMessageBox::information(this, QStringLiteral("账号已存在"),
                                     QStringLiteral("UID %1 已在凭证目录中，无需重复导入。")
                                         .arg(maskUid(uid)));
            return;
        }
    }

    if (!QDir().mkpath(authDirPath)) {
        QMessageBox::warning(this, QStringLiteral("导入失败"),
                             QStringLiteral("无法创建凭证目录：%1").arg(authDirPath));
        return;
    }
    const QByteArray uidHash = QCryptographicHash::hash(uid.toUtf8(), QCryptographicHash::Sha256).toHex().left(16);
    const QString destinationPath = QDir(authDirPath).filePath(
        QStringLiteral("workbuddy-import-%1.json").arg(QString::fromLatin1(uidHash)));
    if (QFileInfo::exists(destinationPath)) {
        QMessageBox::warning(this, QStringLiteral("导入失败"),
                             QStringLiteral("目标凭证文件已存在，为避免覆盖已停止导入。"));
        return;
    }

    QSaveFile destination(destinationPath);
    destination.setDirectWriteFallback(false);
    if (!destination.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, QStringLiteral("导入失败"), destination.errorString());
        return;
    }
    destination.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (destination.write(raw) != raw.size() || !destination.commit()) {
        QMessageBox::warning(this, QStringLiteral("导入失败"), destination.errorString());
        return;
    }

    appendLog(QStringLiteral("已导入账号凭证（UID：%1），令牌未写入日志。文件：%2")
                  .arg(maskUid(uid), QFileInfo(destinationPath).fileName()));
    const auto restart = QMessageBox::question(
        this, QStringLiteral("导入成功"),
        QStringLiteral("账号 %1 已导入。需要重启网关后才会加入账号池。\n\n现在重启网关吗？重启会短暂中断正在进行的请求。")
            .arg(maskUid(uid)),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (restart == QMessageBox::Yes) {
        restartService();
    } else {
        statusBar()->showMessage(QStringLiteral("账号凭证已导入；重启网关后生效"), 5000);
    }
}

void MainWindow::performAccountAction(const QString &action, const QString &uid, const QByteArray &body)
{
    QString method = QStringLiteral("POST");
    QString path;
    const QString encoded = QString::fromUtf8(QUrl::toPercentEncoding(uid));
    if (action == QStringLiteral("delete")) {
        method = QStringLiteral("DELETE");
        path = QStringLiteral("/admin/accounts/") + encoded;
    } else {
        path = QStringLiteral("/admin/accounts/") + encoded + QLatin1Char('/') + action;
    }
    requestJson(method, path, body, [this, action](bool ok, int status, const QJsonObject &, const QString &error) {
        if (!ok) {
            QMessageBox::warning(this, QStringLiteral("账号操作失败"),
                                 QStringLiteral("%1（HTTP %2）").arg(error.isEmpty() ? QStringLiteral("请求失败") : error)
                                     .arg(status));
            return;
        }
        appendLog(QStringLiteral("账号操作完成：%1").arg(action));
        notify(QStringLiteral("账号操作完成"), action == QStringLiteral("delete") ? QStringLiteral("账号已删除")
                                                                            : QStringLiteral("账号状态已更新"));
        refreshAll();
    });
}

void MainWindow::exportUsageJson()
{
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出用量 JSON"),
                                                      QDir(root_).filePath(QStringLiteral("usage-export.json")),
                                                      QStringLiteral("JSON 文件 (*.json)"));
    if (path.isEmpty()) {
        return;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(lastUsage_).toJson(QJsonDocument::Indented)) < 0
        || !file.commit()) {
        QMessageBox::warning(this, QStringLiteral("导出失败"), file.errorString());
        return;
    }
    statusBar()->showMessage(QStringLiteral("用量 JSON 已导出"), 3000);
}

void MainWindow::exportUsageCsv()
{
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出用量 CSV"),
                                                      QDir(root_).filePath(QStringLiteral("usage-export.csv")),
                                                      QStringLiteral("CSV 文件 (*.csv)"));
    if (path.isEmpty()) {
        return;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, QStringLiteral("导出失败"), file.errorString());
        return;
    }
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << "type,model_or_date,requests,successes,failures,prompt_tokens,completion_tokens,total_tokens,credit\n";
    for (const QJsonValue &value : lastUsage_.value(QStringLiteral("models")).toArray()) {
        const QJsonObject row = value.toObject();
        stream << "model," << row.value(QStringLiteral("model")).toString() << ','
               << jsonInt64(row, QStringLiteral("requests")) << ',' << jsonInt64(row, QStringLiteral("successes")) << ','
               << jsonInt64(row, QStringLiteral("failures")) << ',' << jsonInt64(row, QStringLiteral("prompt_tokens")) << ','
               << jsonInt64(row, QStringLiteral("completion_tokens")) << ',' << jsonInt64(row, QStringLiteral("total_tokens")) << ','
               << row.value(QStringLiteral("credit")).toDouble() << '\n';
    }
    for (const QJsonValue &value : lastUsage_.value(QStringLiteral("days")).toArray()) {
        const QJsonObject row = value.toObject();
        stream << "day," << row.value(QStringLiteral("date")).toString() << ','
               << jsonInt64(row, QStringLiteral("requests")) << ',' << jsonInt64(row, QStringLiteral("successes")) << ','
               << jsonInt64(row, QStringLiteral("failures")) << ',' << jsonInt64(row, QStringLiteral("prompt_tokens")) << ','
               << jsonInt64(row, QStringLiteral("completion_tokens")) << ',' << jsonInt64(row, QStringLiteral("total_tokens")) << ','
               << row.value(QStringLiteral("credit")).toDouble() << '\n';
    }
    if (!file.commit()) {
        QMessageBox::warning(this, QStringLiteral("导出失败"), file.errorString());
        return;
    }
    statusBar()->showMessage(QStringLiteral("用量 CSV 已导出"), 3000);
}

void MainWindow::clearUsage()
{
    if (QMessageBox::warning(this, QStringLiteral("确认清空用量"),
                             QStringLiteral("会清空本地所有历史请求统计，无法恢复。确定继续吗？"),
                             QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
        != QMessageBox::Yes) {
        return;
    }
    requestJson(QStringLiteral("POST"), QStringLiteral("/admin/usage/clear"), {},
                [this](bool ok, int status, const QJsonObject &, const QString &error) {
                    if (!ok) {
                        QMessageBox::warning(this, QStringLiteral("清空失败"),
                                             QStringLiteral("%1（HTTP %2）").arg(error).arg(status));
                        return;
                    }
                    appendLog(QStringLiteral("用量统计已清空"));
                    notify(QStringLiteral("用量已清空"), QStringLiteral("本地统计已重置"));
                    fetchUsage();
                });
}

void MainWindow::loadAdvancedConfigFields()
{
    const QJsonObject root = config_.object();
    maxBodyMbSpin_->setValue(nestedInt(root, QStringLiteral("server"), QStringLiteral("max_body_mb"), 8));
    softRateEdit_->setText(nestedString(root, QStringLiteral("cooldown"), QStringLiteral("soft_rate"), QStringLiteral("600s")));
    softRateMaxEdit_->setText(nestedString(root, QStringLiteral("cooldown"), QStringLiteral("soft_rate_max"), QStringLiteral("2h")));

    const QStringList scheduleIds = {QStringLiteral("checkin"), QStringLiteral("activity"), QStringLiteral("travel"),
                                     QStringLiteral("keepalive"), QStringLiteral("school"), QStringLiteral("cat")};
    const QStringList scheduleKeys = {QStringLiteral("checkin_hours"), QStringLiteral("activity_hours"), QStringLiteral("travel_hours"),
                                      QStringLiteral("keepalive_hours"), QStringLiteral("school_hours"), QStringLiteral("cat_hours")};
    const QStringList scheduleDefaults = {QStringLiteral("9,21"), QStringLiteral("10"), QStringLiteral("9,21"),
                                          QStringLiteral("22"), QStringLiteral("12"), QStringLiteral("1")};
    for (int i = 0; i < scheduleIds.size() && i < scheduleEnabledChecks_.size(); ++i) {
        scheduleEnabledChecks_.at(i)->setChecked(nestedBool(root, QStringLiteral("schedule"),
                                                            scheduleIds.at(i) + QStringLiteral("_enabled"), true));
        scheduleHoursEdits_.at(i)->setText(hoursText(root, scheduleKeys.at(i), scheduleDefaults.at(i)));
    }
    activityReportSpin_->setValue(nestedInt(root, QStringLiteral("schedule"), QStringLiteral("activity_report_count"), 5));

    globalEnabledCheck_->setChecked(nestedBool(root, QStringLiteral("global"), QStringLiteral("enabled"), true));
    globalChatBaseEdit_->setText(nestedString(root, QStringLiteral("global"), QStringLiteral("chat_base")));
    globalBillingBaseEdit_->setText(nestedString(root, QStringLiteral("global"), QStringLiteral("billing_base")));

    timeoutSecondsSpin_->setValue(nestedInt(root, QStringLiteral("upstream"), QStringLiteral("timeout_seconds"), 120));
    headerTimeoutSecondsSpin_->setValue(nestedInt(root, QStringLiteral("upstream"), QStringLiteral("header_timeout_seconds"), 120));
    idleTimeoutSecondsSpin_->setValue(nestedInt(root, QStringLiteral("upstream"), QStringLiteral("idle_timeout_seconds"), 300));
    userAgentEdit_->setText(nestedString(root, QStringLiteral("upstream"), QStringLiteral("user_agent")));
    clientVersionEdit_->setText(nestedString(root, QStringLiteral("upstream"), QStringLiteral("client_version")));
    cliVersionEdit_->setText(nestedString(root, QStringLiteral("upstream"), QStringLiteral("cli_version")));
    deviceTokenEdit_->setText(nestedString(root, QStringLiteral("upstream"), QStringLiteral("device_token")));
    deviceTokenFileEdit_->setText(nestedString(root, QStringLiteral("upstream"), QStringLiteral("device_token_file")));
    clientNameEdit_->setText(nestedString(root, QStringLiteral("upstream"), QStringLiteral("client_name"), QStringLiteral("WorkBuddy")));
    passthroughIpCheck_->setChecked(nestedBool(root, QStringLiteral("upstream"), QStringLiteral("passthrough_ip"), false));
    sanitizeFingerprintsCheck_->setChecked(nestedBool(root, QStringLiteral("features"),
                                                       QStringLiteral("sanitize_blacklist_fingerprints"), true));

    const QString promptMode = nestedString(root, QStringLiteral("prompt"), QStringLiteral("mode"), QStringLiteral("passthrough"));
    const int promptIndex = promptModeCombo_->findData(promptMode.toLower());
    promptModeCombo_->setCurrentIndex(promptIndex >= 0 ? promptIndex : 0);
    promptFileEdit_->setText(nestedString(root, QStringLiteral("prompt"), QStringLiteral("file")));
    promptTextEdit_->clear();
    const QString promptFile = promptFileEdit_->text().trimmed();
    if (!promptFile.isEmpty()) {
        QFile file(QDir(root_).filePath(promptFile));
        if (file.open(QIODevice::ReadOnly)) {
            promptTextEdit_->setPlainText(QString::fromUtf8(file.readAll()));
        }
    }

    upstashUrlEdit_->setText(nestedString(root, QStringLiteral("upstash"), QStringLiteral("url")));
    upstashTokenEdit_->setText(nestedString(root, QStringLiteral("upstash"), QStringLiteral("token")));
    breakerThresholdSpin_->setValue(nestedInt(root, QStringLiteral("pool"), QStringLiteral("breaker_threshold"), 3));
    breakerCooldownEdit_->setText(nestedString(root, QStringLiteral("pool"), QStringLiteral("breaker_cooldown"), QStringLiteral("30m")));
    breakerCooldownMaxEdit_->setText(nestedString(root, QStringLiteral("pool"), QStringLiteral("breaker_cooldown_max"), QStringLiteral("6h")));
    idleWeightPerHourEdit_->setText(QString::number(nestedDouble(root, QStringLiteral("pool"), QStringLiteral("idle_weight_per_hour"), 0.5)));
    idleWeightMaxEdit_->setText(QString::number(nestedDouble(root, QStringLiteral("pool"), QStringLiteral("idle_weight_max"), 5.0)));
    expiringSoonEdit_->setText(nestedString(root, QStringLiteral("pool"), QStringLiteral("expiring_soon"), QStringLiteral("168h")));
    sessionStickyCheck_->setChecked(nestedBool(root, QStringLiteral("session_sticky"), QStringLiteral("enabled"), true));
    sessionTtlEdit_->setText(nestedString(root, QStringLiteral("session_sticky"), QStringLiteral("ttl"), QStringLiteral("30m")));
    sessionGcEdit_->setText(nestedString(root, QStringLiteral("session_sticky"), QStringLiteral("gc_interval"), QStringLiteral("5m")));
}

void MainWindow::setSecretVisibility(bool visible)
{
    const auto mode = visible ? QLineEdit::Normal : QLineEdit::Password;
    if (apiKeyEdit_) {
        apiKeyEdit_->setEchoMode(mode);
    }
    if (deviceTokenEdit_) {
        deviceTokenEdit_->setEchoMode(mode);
    }
    if (upstashTokenEdit_) {
        upstashTokenEdit_->setEchoMode(mode);
    }
}

void MainWindow::saveConfigFromUi()
{
    const QString listen = listenEdit_->text().trimmed();
    const QString apiKey = apiKeyEdit_->text();
    if (listen.isEmpty() || !listen.contains(QLatin1Char(':'))) {
        QMessageBox::warning(this, QStringLiteral("保存失败"), QStringLiteral("监听地址必须包含端口，例如 127.0.0.1:7863"));
        return;
    }
    if (apiKey.trimmed().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("保存失败"), QStringLiteral("API Key 不能为空"));
        return;
    }
    QJsonObject &root = config_.object();
    root.insert(QStringLiteral("listen"), listen);
    root.insert(QStringLiteral("api_key"), apiKey);
    QJsonObject pool = root.value(QStringLiteral("pool")).toObject();
    pool.insert(QStringLiteral("max_in_flight"), maxInFlightSpin_->value());
    pool.insert(QStringLiteral("breaker_threshold"), breakerThresholdSpin_->value());
    pool.insert(QStringLiteral("breaker_cooldown"), breakerCooldownEdit_->text().trimmed());
    pool.insert(QStringLiteral("breaker_cooldown_max"), breakerCooldownMaxEdit_->text().trimmed());
    bool doubleOk = false;
    const double idleWeightPerHour = idleWeightPerHourEdit_->text().trimmed().toDouble(&doubleOk);
    if (!doubleOk || idleWeightPerHour < 0) {
        QMessageBox::warning(this, QStringLiteral("保存失败"), QStringLiteral("闲置权重/小时必须是非负数字"));
        return;
    }
    pool.insert(QStringLiteral("idle_weight_per_hour"), idleWeightPerHour);
    const double idleWeightMax = idleWeightMaxEdit_->text().trimmed().toDouble(&doubleOk);
    if (!doubleOk || idleWeightMax < 0) {
        QMessageBox::warning(this, QStringLiteral("保存失败"), QStringLiteral("闲置权重封顶必须是非负数字"));
        return;
    }
    pool.insert(QStringLiteral("idle_weight_max"), idleWeightMax);
    pool.insert(QStringLiteral("expiring_soon"), expiringSoonEdit_->text().trimmed());
    root.insert(QStringLiteral("pool"), pool);

    QJsonObject server = root.value(QStringLiteral("server")).toObject();
    server.insert(QStringLiteral("max_body_mb"), maxBodyMbSpin_->value());
    root.insert(QStringLiteral("server"), server);
    QJsonObject cooldown = root.value(QStringLiteral("cooldown")).toObject();
    cooldown.insert(QStringLiteral("soft_rate"), softRateEdit_->text().trimmed());
    cooldown.insert(QStringLiteral("soft_rate_max"), softRateMaxEdit_->text().trimmed());
    root.insert(QStringLiteral("cooldown"), cooldown);

    const QStringList scheduleIds = {QStringLiteral("checkin"), QStringLiteral("activity"), QStringLiteral("travel"),
                                     QStringLiteral("keepalive"), QStringLiteral("school"), QStringLiteral("cat")};
    const QStringList scheduleKeys = {QStringLiteral("checkin_hours"), QStringLiteral("activity_hours"), QStringLiteral("travel_hours"),
                                      QStringLiteral("keepalive_hours"), QStringLiteral("school_hours"), QStringLiteral("cat_hours")};
    QJsonObject schedule = root.value(QStringLiteral("schedule")).toObject();
    for (int i = 0; i < scheduleIds.size(); ++i) {
        bool hoursOk = false;
        const QJsonArray hours = parseHours(scheduleHoursEdits_.at(i)->text(), &hoursOk);
        if (!hoursOk) {
            QMessageBox::warning(this, QStringLiteral("保存失败"),
                                 QStringLiteral("%1 的整点列表必须是 0-23 的逗号分隔数字").arg(taskLabel(scheduleIds.at(i))));
            return;
        }
        schedule.insert(scheduleIds.at(i) + QStringLiteral("_enabled"), scheduleEnabledChecks_.at(i)->isChecked());
        schedule.insert(scheduleKeys.at(i), hours);
    }
    schedule.insert(QStringLiteral("activity_report_count"), activityReportSpin_->value());
    root.insert(QStringLiteral("schedule"), schedule);

    QJsonObject global = root.value(QStringLiteral("global")).toObject();
    global.insert(QStringLiteral("enabled"), globalEnabledCheck_->isChecked());
    global.insert(QStringLiteral("chat_base"), globalChatBaseEdit_->text().trimmed());
    global.insert(QStringLiteral("billing_base"), globalBillingBaseEdit_->text().trimmed());
    root.insert(QStringLiteral("global"), global);

    QJsonObject upstream = root.value(QStringLiteral("upstream")).toObject();
    upstream.insert(QStringLiteral("timeout_seconds"), timeoutSecondsSpin_->value());
    upstream.insert(QStringLiteral("header_timeout_seconds"), headerTimeoutSecondsSpin_->value());
    upstream.insert(QStringLiteral("idle_timeout_seconds"), idleTimeoutSecondsSpin_->value());
    upstream.insert(QStringLiteral("user_agent"), userAgentEdit_->text());
    upstream.insert(QStringLiteral("client_version"), clientVersionEdit_->text().trimmed());
    upstream.insert(QStringLiteral("cli_version"), cliVersionEdit_->text().trimmed());
    upstream.insert(QStringLiteral("device_token"), deviceTokenEdit_->text());
    upstream.insert(QStringLiteral("device_token_file"), deviceTokenFileEdit_->text().trimmed());
    upstream.insert(QStringLiteral("client_name"), clientNameEdit_->text().trimmed());
    upstream.insert(QStringLiteral("passthrough_ip"), passthroughIpCheck_->isChecked());
    root.insert(QStringLiteral("upstream"), upstream);
    QJsonObject features = root.value(QStringLiteral("features")).toObject();
    features.insert(QStringLiteral("sanitize_blacklist_fingerprints"), sanitizeFingerprintsCheck_->isChecked());
    root.insert(QStringLiteral("features"), features);

    QJsonObject prompt = root.value(QStringLiteral("prompt")).toObject();
    prompt.insert(QStringLiteral("mode"), promptModeCombo_->currentData().toString());
    QString promptFile = promptFileEdit_->text().trimmed();
    const QString promptText = promptTextEdit_->toPlainText();
    if (!promptText.trimmed().isEmpty() && promptFile.isEmpty()) {
        promptFile = QStringLiteral("data/manager-prompt.md");
        promptFileEdit_->setText(promptFile);
    }
    if (!promptText.trimmed().isEmpty() && !promptFile.isEmpty()) {
        QSaveFile promptSave(QDir(root_).filePath(promptFile));
        if (!promptSave.open(QIODevice::WriteOnly) || promptSave.write(promptText.toUtf8()) < 0 || !promptSave.commit()) {
            QMessageBox::warning(this, QStringLiteral("保存失败"), QStringLiteral("无法保存提示词文件：%1").arg(promptFile));
            return;
        }
    }
    prompt.insert(QStringLiteral("file"), promptFile);
    root.insert(QStringLiteral("prompt"), prompt);

    QJsonObject upstash = root.value(QStringLiteral("upstash")).toObject();
    upstash.insert(QStringLiteral("url"), upstashUrlEdit_->text().trimmed());
    upstash.insert(QStringLiteral("token"), upstashTokenEdit_->text());
    root.insert(QStringLiteral("upstash"), upstash);

    QJsonObject sticky = root.value(QStringLiteral("session_sticky")).toObject();
    sticky.insert(QStringLiteral("enabled"), sessionStickyCheck_->isChecked());
    sticky.insert(QStringLiteral("ttl"), sessionTtlEdit_->text().trimmed());
    sticky.insert(QStringLiteral("gc_interval"), sessionGcEdit_->text().trimmed());
    root.insert(QStringLiteral("session_sticky"), sticky);

    QString saveError;
    if (!config_.save(&saveError)) {
        const QString error = saveError.isEmpty() ? QStringLiteral("配置原子保存失败，请检查文件权限") : saveError;
        QMessageBox::warning(this, QStringLiteral("保存失败"), error);
        return;
    }
    baseUrl_ = config_.baseUrl();
    apiKey_ = config_.apiKey();
    saveManagerSettings();
    settingsHint_->setText(QStringLiteral("完整配置已保存并生成 config.json.bak；服务、任务、提示词、Global/CN 和上游字段需要重启网关后生效。"));
    statusBar()->showMessage(QStringLiteral("配置已保存，不会自动重启"), 4000);
    appendLog(QStringLiteral("配置已保存，备份文件：config.json.bak"));
    refreshTasksPage();
}

void MainWindow::reloadConfigFromDisk()
{
    QString error;
    if (!config_.load(&error)) {
        QMessageBox::warning(this, QStringLiteral("读取失败"), error);
        return;
    }
    baseUrl_ = config_.baseUrl();
    apiKey_ = config_.apiKey();
    listenEdit_->setText(config_.listen());
    apiKeyEdit_->setText(config_.apiKey());
    maxInFlightSpin_->setValue(config_.maxInFlight());
    loadAdvancedConfigFields();
    settingsHint_->setText(QStringLiteral("已从磁盘重新读取，尚未修改文件。"));
    refreshTasksPage();
}

void MainWindow::restartAfterConfig()
{
    if (QMessageBox::question(this, QStringLiteral("确认重启网关"),
                              QStringLiteral("重启会中断正在处理的请求，确定继续吗？"),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
        == QMessageBox::Yes) {
        restartService();
    }
}

void MainWindow::triggerTask(const QString &task)
{
    if (!gatewayOnline_) {
        QMessageBox::warning(this, QStringLiteral("无法执行任务"), QStringLiteral("本地网关尚未连接，请先启动服务。"));
        return;
    }
    if (!taskButtons_.contains(task) || !taskButtons_.value(task)->isEnabled()) {
        return;
    }
    const QString warning = QStringLiteral("“%1”会调用上游并可能产生签到、上报、领奖或 Token 刷新等写操作。确定立即执行吗？")
                                .arg(taskLabel(task));
    if (QMessageBox::question(this, QStringLiteral("确认执行任务"), warning,
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
        != QMessageBox::Yes) {
        return;
    }
    taskButtons_.value(task)->setEnabled(false);
    appendLog(QStringLiteral("开始手动执行任务：%1").arg(taskLabel(task)));
    requestJson(QStringLiteral("POST"), QStringLiteral("/admin/tasks/") + task, {},
                [this, task](bool ok, int status, const QJsonObject &object, const QString &error) {
                    if (taskButtons_.contains(task)) {
                        taskButtons_.value(task)->setEnabled(true);
                    }
                    if (!ok) {
                        const QString message = error.isEmpty() ? QStringLiteral("HTTP %1").arg(status) : error;
                        appendLog(QStringLiteral("任务 %1 失败：%2").arg(taskLabel(task), message));
                        QMessageBox::warning(this, QStringLiteral("任务执行失败"), message);
                        return;
                    }
                    appendLog(QStringLiteral("任务 %1 完成：%2")
                                  .arg(taskLabel(task), object.value(QStringLiteral("summary")).toString()));
                    notify(QStringLiteral("任务执行完成"), taskLabel(task));
                    fetchTaskHistory();
                    refreshAll();
                });
}

void MainWindow::clearTaskHistory()
{
    if (QMessageBox::question(this, QStringLiteral("清空任务历史"),
                              QStringLiteral("只会清理管理器可见的任务运行记录，不会撤销已经执行的上游操作。继续吗？"),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
        != QMessageBox::Yes) {
        return;
    }
    requestJson(QStringLiteral("DELETE"), QStringLiteral("/admin/tasks/history"), {},
                [this](bool ok, int status, const QJsonObject &, const QString &error) {
                    if (!ok) {
                        QMessageBox::warning(this, QStringLiteral("清理失败"),
                                             error.isEmpty() ? QStringLiteral("HTTP %1").arg(status) : error);
                        return;
                    }
                    lastTaskHistory_ = {};
                    refreshTasksPage();
                    appendLog(QStringLiteral("任务执行历史已清空"));
                });
}

void MainWindow::runLocalScript(const QString &script, const QStringList &arguments,
                                const QString &label)
{
    if (taskRunner_.busy()) {
        QMessageBox::information(this, QStringLiteral("已有任务运行"), QStringLiteral("请等待当前脚本完成后再运行。"));
        return;
    }
    if (!taskRunner_.runScript(script, arguments, label, proxyEdit_ ? proxyEdit_->text() : QString())) {
        QMessageBox::warning(this, QStringLiteral("无法启动脚本"),
                             QStringLiteral("请确认 Git Bash、Go/Python 依赖和脚本文件存在。"));
        return;
    }
    statusBar()->showMessage(QStringLiteral("已启动 %1，输出会进入日志页").arg(label), 4000);
}

void MainWindow::refreshLogs()
{
    if (logsPaused_) {
        return;
    }
    updateLogView();
}

void MainWindow::updateLogView()
{
    if (!logView_) {
        return;
    }
    QStringList lines = runtimeLogs_;
    const QString output = LogDiagnostics::readTail(QDir(root_).filePath(QStringLiteral("server.log")))
                           + QStringLiteral("\n")
                           + LogDiagnostics::readTail(QDir(root_).filePath(QStringLiteral("server.error.log")));
    if (!output.trimmed().isEmpty()) {
        lines << QStringLiteral("--- 服务日志（脱敏） ---") << output.trimmed();
    }
    const QString search = logSearch_ ? logSearch_->text().trimmed() : QString();
    QStringList filtered;
    for (const QString &line : lines) {
        if (logErrorsOnly_ && logErrorsOnly_->isChecked()
            && !line.contains(QStringLiteral("error"), Qt::CaseInsensitive)
            && !line.contains(QStringLiteral("失败"))
            && !line.contains(QStringLiteral("WARN"), Qt::CaseInsensitive)) {
            continue;
        }
        if (!search.isEmpty() && !line.contains(search, Qt::CaseInsensitive)) {
            continue;
        }
        filtered << LogDiagnostics::sanitize(line);
    }
    if (filtered.isEmpty()) {
        filtered << QStringLiteral("暂无匹配日志");
    }
    logView_->setPlainText(filtered.join(QStringLiteral("\n")));
    logView_->verticalScrollBar()->setValue(logView_->verticalScrollBar()->maximum());
}

void MainWindow::runDiagnostics()
{
    QString report = LogDiagnostics::localReport(root_, config_.object());
    report += QStringLiteral("\n\n接口检查：\n");
    diagnosticsView_->setPlainText(report + QStringLiteral("正在检查 /healthz ……"));
    requestJson(QStringLiteral("GET"), QStringLiteral("/healthz"), {},
                [this, report](bool ok, int status, const QJsonObject &object, const QString &error) {
                    QString result = report + QStringLiteral("\n");
                    if (ok) {
                        result += QStringLiteral("[通过] /healthz：HTTP %1，healthy=%2\n")
                                      .arg(status)
                                      .arg(object.value(QStringLiteral("healthy")).toInt());
                    } else {
                        result += QStringLiteral("[失败] /healthz：%1（HTTP %2）\n").arg(error).arg(status);
                    }
                    result += QStringLiteral("[提示] /status、/v1/models、/usage 使用 API Key；诊断不会发起聊天请求。\n");
                    diagnosticsView_->setPlainText(LogDiagnostics::sanitize(result));
                });
}

void MainWindow::exportDiagnostics()
{
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出诊断报告"),
                                                      QDir(root_).filePath(QStringLiteral("diagnostics.txt")),
                                                      QStringLiteral("文本文件 (*.txt)"));
    if (path.isEmpty()) {
        return;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, QStringLiteral("导出失败"), file.errorString());
        return;
    }
    const QByteArray data = LogDiagnostics::sanitize(diagnosticsView_->toPlainText()).toUtf8();
    file.write(data);
    if (!file.commit()) {
        QMessageBox::warning(this, QStringLiteral("导出失败"), file.errorString());
        return;
    }
    statusBar()->showMessage(QStringLiteral("诊断报告已导出"), 3000);
}

void MainWindow::appendLog(const QString &text)
{
    const QString clean = LogDiagnostics::sanitize(text.trimmed());
    if (clean.isEmpty()) {
        return;
    }
    runtimeLogs_.append(QStringLiteral("[%1] %2")
                            .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), clean));
    while (runtimeLogs_.size() > 120) {
        runtimeLogs_.removeFirst();
    }
    updateLogView();
}

void MainWindow::notify(const QString &title, const QString &message, QSystemTrayIcon::MessageIcon icon)
{
    if (notificationsCheck_ && !notificationsCheck_->isChecked()) {
        return;
    }
    if (tray_ && tray_->isVisible()) {
        tray_->showMessage(title, message, icon, 5000);
    }
}

QString MainWindow::maskUid(const QString &uid) const
{
    if (uid.size() <= 8) {
        return uid.left(2) + QStringLiteral("***");
    }
    return uid.left(4) + QStringLiteral("…") + uid.right(4);
}

QString MainWindow::selectedTheme() const
{
    return themeCombo_ ? themeCombo_->currentData().toString() : QStringLiteral("system");
}

void MainWindow::openPage(int index)
{
    if (navigation_ && index >= 0 && index < navigation_->count()) {
        navigation_->setCurrentRow(index);
    }
}

void MainWindow::showAndActivate()
{
    showNormal();
    raise();
    activateWindow();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!allowQuit_ && managerSettings_.value(QStringLiteral("closeToTray"), true).toBool()
        && tray_ && tray_->isVisible()) {
        hide();
        tray_->showMessage(QStringLiteral("workbuddy2api 仍在运行"), QStringLiteral("已隐藏到系统托盘"),
                           QSystemTrayIcon::Information, 2500);
        event->ignore();
        return;
    }
    if (!allowQuit_) {
        event->ignore();
        exitManager();
        return;
    }
    allowQuit_ = true;
    if (tray_) {
        tray_->hide();
    }
    event->accept();
    QTimer::singleShot(0, qApp, [] { QApplication::quit(); });
}
