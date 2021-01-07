// Copyright (c) 2017-2021 The Merit Foundation
// Copyright (c) 2011-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "config/merit-config.h"
#endif

#include "meritgui.h"
#include "pog2/cgs.h"

#include "chainparams.h"
#include "clientmodel.h"
#include "fs.h"
#include "guiconstants.h"
#include "guiutil.h"
#include "intro.h"
#include "faststart.h"
#include "networkstyle.h"
#include "optionsmodel.h"
#include "platformstyle.h"
#include "splashscreen.h"
#include "utilitydialog.h"
#include "winshutdownmonitor.h"

#ifdef ENABLE_WALLET
#include "paymentserver.h"
#include "walletmodel.h"
#endif

#include "init.h"
#include "rpc/server.h"
#include "scheduler.h"
#include "ui_interface.h"
#include "util.h"
#include "warnings.h"

#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif

#include <stdint.h>

#include <boost/thread.hpp>

#include <QApplication>
#include <QDebug>
#include <QLibraryInfo>
#include <QLocale>
#include <QMessageBox>
#include <QSettings>
#include <QThread>
#include <QTimer>
#include <QTranslator>

#if defined(QT_STATICPLUGIN)
#include <QtPlugin>
#if QT_VERSION < 0x050000
Q_IMPORT_PLUGIN(qcncodecs)
Q_IMPORT_PLUGIN(qjpcodecs)
Q_IMPORT_PLUGIN(qtwcodecs)
Q_IMPORT_PLUGIN(qkrcodecs)
Q_IMPORT_PLUGIN(qtaccessiblewidgets)
#else
#if QT_VERSION < 0x050400
Q_IMPORT_PLUGIN(AccessibleFactory)
#endif
#if defined(QT_QPA_PLATFORM_XCB)
Q_IMPORT_PLUGIN(QXcbIntegrationPlugin);
#elif defined(QT_QPA_PLATFORM_WINDOWS)
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin);
#elif defined(QT_QPA_PLATFORM_COCOA)
Q_IMPORT_PLUGIN(QCocoaIntegrationPlugin);
#endif
#endif
#endif

#if QT_VERSION < 0x050000
#include <QTextCodec>
#endif

// Declare meta types used for QMetaObject::invokeMethod
Q_DECLARE_METATYPE(bool*)
Q_DECLARE_METATYPE(CAmount)

static void InitMessage(const std::string &message)
{
    LogPrintf("init message: %s\n", message);
}

/*
   Translate string to current locale using Qt.
 */
static std::string Translate(const char* psz)
{
    return QCoreApplication::translate("merit-core", psz).toStdString();
}

static QString GetLangTerritory()
{
    QSettings settings;
    // Get desired locale (e.g. "de_DE")
    // 1) System default language
    QString lang_territory = QLocale::system().name();
    // 2) Language from QSettings
    QString lang_territory_qsettings = settings.value("language", "").toString();
    if(!lang_territory_qsettings.isEmpty())
        lang_territory = lang_territory_qsettings;
    // 3) -lang command line argument
    lang_territory = QString::fromStdString(gArgs.GetArg("-lang", lang_territory.toStdString()));
    return lang_territory;
}

/** Set up translations */
static void initTranslations(QTranslator &qtTranslatorBase, QTranslator &qtTranslator, QTranslator &translatorBase, QTranslator &translator)
{
    // Remove old translators
    QApplication::removeTranslator(&qtTranslatorBase);
    QApplication::removeTranslator(&qtTranslator);
    QApplication::removeTranslator(&translatorBase);
    QApplication::removeTranslator(&translator);

    // Get desired locale (e.g. "de_DE")
    // 1) System default language
    QString lang_territory = GetLangTerritory();

    // Convert to "de" only by truncating "_DE"
    QString lang = lang_territory;
    lang.truncate(lang_territory.lastIndexOf('_'));

    // Load language files for configured locale:
    // - First load the translator for the base language, without territory
    // - Then load the more specific locale translator

    // Load e.g. qt_de.qm
    if (qtTranslatorBase.load("qt_" + lang, QLibraryInfo::location(QLibraryInfo::TranslationsPath)))
        QApplication::installTranslator(&qtTranslatorBase);

    // Load e.g. qt_de_DE.qm
    if (qtTranslator.load("qt_" + lang_territory, QLibraryInfo::location(QLibraryInfo::TranslationsPath)))
        QApplication::installTranslator(&qtTranslator);

    // Load e.g. merit_de.qm (shortcut "de" needs to be defined in merit.qrc)
    if (translatorBase.load(lang, ":/translations/"))
        QApplication::installTranslator(&translatorBase);

    // Load e.g. merit_de_DE.qm (shortcut "de_DE" needs to be defined in merit.qrc)
    if (translator.load(lang_territory, ":/translations/"))
        QApplication::installTranslator(&translator);
}

/* qDebug() message handler --> debug.log */
#if QT_VERSION < 0x050000
void DebugMessageHandler(QtMsgType type, const char *msg)
{
    if (type == QtDebugMsg) {
        LogPrint(BCLog::QT, "GUI: %s\n", msg);
    } else {
        LogPrintf("GUI: %s\n", msg);
    }
}
#else
void DebugMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString &msg)
{
    Q_UNUSED(context);
    if (type == QtDebugMsg) {
        LogPrint(BCLog::QT, "GUI: %s\n", msg.toStdString());
    } else {
        LogPrintf("GUI: %s\n", msg.toStdString());
    }
}
#endif

/** Class encapsulating Merit Core startup and shutdown.
 * Allows running startup and shutdown in a different thread from the UI thread.
 */
class MeritCore: public QObject
{
    Q_OBJECT
public:
    explicit MeritCore();
    /** Basic initialization, before starting initialization/shutdown thread.
     * Return true on success.
     */
    static bool baseInitialize();

public Q_SLOTS:
    void initialize();
    void shutdown();

Q_SIGNALS:
    void initializeResult(bool success);
    void shutdownResult();
    void runawayException(const QString &message);

private:
    boost::thread_group threadGroup;
    CScheduler scheduler;

    /// Pass fatal exception message to UI thread
    void handleRunawayException(const std::exception *e);
};

/** Main Merit application object */
class MeritApplication: public QApplication
{
    Q_OBJECT
public:
    explicit MeritApplication(int &argc, char **argv);
    ~MeritApplication();

#ifdef ENABLE_WALLET
    /// Create payment server
    void createPaymentServer();
#endif
    /// parameter interaction/setup based on rules
    void parameterSetup();
    /// Create options model
    void createOptionsModel(bool resetSettings);
    /// Create main window
    void createWindow(const NetworkStyle *networkStyle);
    /// Create splash screen
    void createSplashScreen(const NetworkStyle *networkStyle);

    /// Request core initialization
    void requestInitialize();
    /// Request core shutdown
    void requestShutdown();

    /// Get process return value
    int getReturnValue() const { return returnValue; }

    /// Get window identifier of QMainWindow (MeritGUI)
    WId getMainWinId() const;

public Q_SLOTS:
    void initializeResult(bool success);
    void shutdownResult();
    /// Handle runaway exceptions. Shows a message box with the problem and quits the program.
    void handleRunawayException(const QString &message);

Q_SIGNALS:
    void requestedInitialize();
    void requestedShutdown();
    void stopThread();
    void splashFinished(QWidget *window);

private:
    QThread *coreThread;
    OptionsModel *optionsModel;
    ClientModel *clientModel;
    MeritGUI *window;
    QTimer *pollShutdownTimer;
#ifdef ENABLE_WALLET
    PaymentServer* paymentServer;
    WalletModel *walletModel;
#endif
    int returnValue;
    const PlatformStyle *platformStyle;
    std::unique_ptr<QWidget> shutdownWindow;

    void startThread();
};

#include "merit.moc"

MeritCore::MeritCore():
    QObject()
{
}

void MeritCore::handleRunawayException(const std::exception *e)
{
    PrintExceptionContinue(e, "Runaway exception");
    Q_EMIT runawayException(QString::fromStdString(GetWarnings("gui")));
}

bool MeritCore::baseInitialize()
{
    if (!AppInitBasicSetup())
    {
        return false;
    }
    if (!AppInitParameterInteraction())
    {
        return false;
    }
    if (!AppInitSanityChecks())
    {
        return false;
    }
    if (!AppInitLockDataDirectory())
    {
        return false;
    }
    return true;
}

void MeritCore::initialize()
{
    try
    {
        qDebug() << __func__ << ": Running initialization in thread";
        bool rv = AppInitMain(threadGroup, scheduler);
        Q_EMIT initializeResult(rv);
    } catch (const std::exception& e) {
        handleRunawayException(&e);
    } catch (...) {
        handleRunawayException(nullptr);
    }
}

void MeritCore::shutdown()
{
    try
    {
        qDebug() << __func__ << ": Running Shutdown in thread";
        Interrupt(threadGroup);
        threadGroup.join_all();
        Shutdown();
        qDebug() << __func__ << ": Shutdown finished";
        Q_EMIT shutdownResult();
    } catch (const std::exception& e) {
        handleRunawayException(&e);
    } catch (...) {
        handleRunawayException(nullptr);
    }
}

MeritApplication::MeritApplication(int &argc, char **argv):
    QApplication(argc, argv),
    coreThread(0),
    optionsModel(0),
    clientModel(0),
    window(0),
    pollShutdownTimer(0),
#ifdef ENABLE_WALLET
    paymentServer(0),
    walletModel(0),
#endif
    returnValue(0)
{
    setQuitOnLastWindowClosed(false);

    // UI per-platform customization
    // This must be done inside the MeritApplication constructor, or after it, because
    // PlatformStyle::instantiate requires a QApplication
    std::string platformName;
    platformName = gArgs.GetArg("-uiplatform", MeritGUI::DEFAULT_UIPLATFORM);
    platformStyle = PlatformStyle::instantiate(QString::fromStdString(platformName));
    if (!platformStyle) // Fall back to "other" if specified name not found
        platformStyle = PlatformStyle::instantiate("other");
    assert(platformStyle);
}

MeritApplication::~MeritApplication()
{
    if(coreThread)
    {
        qDebug() << __func__ << ": Stopping thread";
        Q_EMIT stopThread();
        coreThread->wait();
        qDebug() << __func__ << ": Stopped thread";
    }

    delete window;
    window = 0;
#ifdef ENABLE_WALLET
    delete paymentServer;
    paymentServer = 0;
#endif
    delete optionsModel;
    optionsModel = 0;
    delete platformStyle;
    platformStyle = 0;
}

#ifdef ENABLE_WALLET
void MeritApplication::createPaymentServer()
{
    paymentServer = new PaymentServer(this);
}
#endif

void MeritApplication::createOptionsModel(bool resetSettings)
{
    optionsModel = new OptionsModel(nullptr, resetSettings);
}

void MeritApplication::createWindow(const NetworkStyle *networkStyle)
{
    window = new MeritGUI(platformStyle, networkStyle, 0);

    pollShutdownTimer = new QTimer(window);
    connect(pollShutdownTimer, SIGNAL(timeout()), window, SLOT(detectShutdown()));
    pollShutdownTimer->start(200);
}

void MeritApplication::createSplashScreen(const NetworkStyle *networkStyle)
{
    SplashScreen *splash = new SplashScreen(0, networkStyle);
    // We don't hold a direct pointer to the splash screen after creation, but the splash
    // screen will take care of deleting itself when slotFinish happens.
    splash->show();
    connect(this, SIGNAL(splashFinished(QWidget*)), splash, SLOT(slotFinish(QWidget*)));
    connect(this, SIGNAL(requestedShutdown()), splash, SLOT(close()));
}

void MeritApplication::startThread()
{
    if(coreThread)
        return;
    coreThread = new QThread(this);
    MeritCore *executor = new MeritCore();
    executor->moveToThread(coreThread);

    /*  communication to and from thread */
    connect(executor, SIGNAL(initializeResult(bool)), this, SLOT(initializeResult(bool)));
    connect(executor, SIGNAL(shutdownResult()), this, SLOT(shutdownResult()));
    connect(executor, SIGNAL(runawayException(QString)), this, SLOT(handleRunawayException(QString)));
    connect(this, SIGNAL(requestedInitialize()), executor, SLOT(initialize()));
    connect(this, SIGNAL(requestedShutdown()), executor, SLOT(shutdown()));
    /*  make sure executor object is deleted in its own thread */
    connect(this, SIGNAL(stopThread()), executor, SLOT(deleteLater()));
    connect(this, SIGNAL(stopThread()), coreThread, SLOT(quit()));

    coreThread->start();
}

void MeritApplication::parameterSetup()
{
    InitLogging();
    InitParameterInteraction();
}

void MeritApplication::requestInitialize()
{
    qDebug() << __func__ << ": Requesting initialize";
    startThread();
    Q_EMIT requestedInitialize();
}

void MeritApplication::requestShutdown()
{
    // Show a simple window indicating shutdown status
    // Do this first as some of the steps may take some time below,
    // for example the RPC console may still be executing a command.
    shutdownWindow.reset(ShutdownWindow::showShutdownWindow(window));

    qDebug() << __func__ << ": Requesting shutdown";
    startThread();
    window->hide();
    window->setClientModel(0);
    pollShutdownTimer->stop();

#ifdef ENABLE_WALLET
    window->removeAllWallets();
    delete walletModel;
    walletModel = 0;
#endif
    delete clientModel;
    clientModel = 0;

    StartShutdown();

    // Request shutdown from core thread
    Q_EMIT requestedShutdown();
}

void MeritApplication::initializeResult(bool success)
{
    qDebug() << __func__ << ": Initialization result: " << success;
    // Set exit result.
    returnValue = success ? EXIT_SUCCESS : EXIT_FAILURE;
    if(success)
    {
        // Log this only after AppInitMain finishes, as then logging setup is guaranteed complete
        qWarning() << "Platform customization:" << platformStyle->getName();
#ifdef ENABLE_WALLET
        PaymentServer::LoadRootCAs();
        paymentServer->setOptionsModel(optionsModel);
#endif

        clientModel = new ClientModel(optionsModel);
        window->setClientModel(clientModel);

#ifdef ENABLE_WALLET
        // TODO: Expose secondary wallets
        if (!vpwallets.empty())
        {
            walletModel = new WalletModel(platformStyle, vpwallets[0], optionsModel);

            window->addWallet(MeritGUI::DEFAULT_WALLET, walletModel);
            window->setCurrentWallet(MeritGUI::DEFAULT_WALLET);

            connect(walletModel, SIGNAL(coinsSent(CWallet*,SendCoinsRecipient,QByteArray)),
                             paymentServer, SLOT(fetchPaymentACK(CWallet*,const SendCoinsRecipient&,QByteArray)));
        }
#endif

        // If -min option passed, start window minimized.
        if(gArgs.GetBoolArg("-min", false))
        {
            window->showMinimized();
        }
        else
        {
            window->show();
        }
        Q_EMIT splashFinished(window);

#ifdef ENABLE_WALLET
        // Now that initialization/startup is done, process any command-line
        // merit: URIs or payment requests:
        connect(paymentServer, SIGNAL(receivedPaymentRequest(SendCoinsRecipient)),
                         window, SLOT(handlePaymentRequest(SendCoinsRecipient)));
        connect(window, SIGNAL(receivedURI(QString)),
                         paymentServer, SLOT(handleURIOrFile(QString)));
        connect(paymentServer, SIGNAL(message(QString,QString,unsigned int)),
                         window, SLOT(message(QString,QString,unsigned int)));
        QTimer::singleShot(100, paymentServer, SLOT(uiReady()));
#endif
    } else {
        quit(); // Exit main loop
    }
}

void MeritApplication::shutdownResult()
{
    quit(); // Exit main loop after shutdown finished
}

void MeritApplication::handleRunawayException(const QString &message)
{
    QMessageBox::critical(0, "Runaway exception", MeritGUI::tr("A fatal error occurred. Merit can no longer continue safely and will quit.") + QString("\n\n") + message);
    ::exit(EXIT_FAILURE);
}

WId MeritApplication::getMainWinId() const
{
    if (!window)
        return 0;

    return window->winId();
}

#ifndef MERIT_QT_TEST
int main(int argc, char *argv[])
{
    {
    //pog2::TestChain();
    //return 0;
    }
    SetupEnvironment();

    /// 1. Parse command-line options. These take precedence over anything else.
    // Command-line options take precedence:
    gArgs.ParseParameters(argc, argv);

    // Do not refer to data directory yet, this can be overridden by Intro::pickDataDirectory

    /// 2. Basic Qt initialization (not dependent on parameters or configuration)
#if QT_VERSION < 0x050000
    // Internal string conversion is all UTF-8
    QTextCodec::setCodecForTr(QTextCodec::codecForName("UTF-8"));
    QTextCodec::setCodecForCStrings(QTextCodec::codecForTr());
#endif

    Q_INIT_RESOURCE(merit);
    Q_INIT_RESOURCE(merit_locale);

    MeritApplication app(argc, argv);
#if QT_VERSION > 0x050100
    // Generate high-dpi pixmaps
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
#if QT_VERSION >= 0x050600
    QGuiApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif
#ifdef Q_OS_MAC
    QApplication::setAttribute(Qt::AA_DontShowIconsInMenus);
#endif

    // Register meta types used for QMetaObject::invokeMethod
    qRegisterMetaType< bool* >();
    //   Need to pass name here as CAmount is a typedef (see http://qt-project.org/doc/qt-5/qmetatype.html#qRegisterMetaType)
    //   IMPORTANT if it is no longer a typedef use the normal variant above
    qRegisterMetaType< CAmount >("CAmount");
    qRegisterMetaType< std::function<void(void)> >("std::function<void(void)>");

    /// 3. Application identification
    // must be set before OptionsModel is initialized or translations are loaded,
    // as it is used to locate QSettings
    QApplication::setOrganizationName(QAPP_ORG_NAME);
    QApplication::setOrganizationDomain(QAPP_ORG_DOMAIN);
    QApplication::setApplicationName(QAPP_APP_NAME_DEFAULT);
    GUIUtil::SubstituteFonts(GetLangTerritory());

    /// 4. Initialization of translations, so that intro dialog is in user's language
    // Now that QSettings are accessible, initialize translations
    QTranslator qtTranslatorBase, qtTranslator, translatorBase, translator;
    initTranslations(qtTranslatorBase, qtTranslator, translatorBase, translator);
    translationInterface.Translate.connect(Translate);

    // Show help message immediately after parsing command-line options (for "-lang") and setting locale,
    // but before showing splash screen.
    if (gArgs.IsArgSet("-?") || gArgs.IsArgSet("-h") || gArgs.IsArgSet("-help") || gArgs.IsArgSet("-version"))
    {
        HelpMessageDialog help(nullptr, gArgs.IsArgSet("-version"));
        help.showOrPrint();
        return EXIT_SUCCESS;
    }

    /// 5. Now that settings and translations are available, ask user for data directory
    // User language is set up: pick a data directory
    if (!Intro::pickDataDirectory())
        return EXIT_SUCCESS;

    /// 6. Determine availability of data directory and parse merit.conf
    /// - Do not call GetDataDir(true) before this step finishes
    if (!fs::is_directory(GetDataDir(false)))
    {
        QMessageBox::critical(0, QObject::tr(PACKAGE_NAME),
                              QObject::tr("Error: Specified data directory \"%1\" does not exist.").arg(QString::fromStdString(gArgs.GetArg("-datadir", ""))));
        return EXIT_FAILURE;
    }
    try {
        gArgs.ReadConfigFile(gArgs.GetArg("-conf", MERIT_CONF_FILENAME));
    } catch (const std::exception& e) {
        QMessageBox::critical(0, QObject::tr(PACKAGE_NAME),
                              QObject::tr("Error: Cannot parse configuration file: %1. Only use key=value syntax.").arg(e.what()));
        return EXIT_FAILURE;
    }

    /// 7. Determine network (and switch to network specific options)
    // - Do not call Params() before this step
    // - Do this after parsing the configuration file, as the network can be switched there
    // - QSettings() will use the new application name after this, resulting in network-specific settings
    // - Needs to be done before createOptionsModel

    // Check for -testnet or -regtest parameter (Params() calls are only valid after this clause)
    try {
        SelectParams(ChainNameFromCommandLine());
    } catch(std::exception &e) {
        QMessageBox::critical(0, QObject::tr(PACKAGE_NAME), QObject::tr("Error: %1").arg(e.what()));
        return EXIT_FAILURE;
    }

#ifdef ENABLE_WALLET
    // Parse URIs on command line -- this can affect Params()
    PaymentServer::ipcParseCommandLine(argc, argv);
#endif

    QScopedPointer<const NetworkStyle> networkStyle(NetworkStyle::instantiate(QString::fromStdString(Params().NetworkIDString())));
    assert(!networkStyle.isNull());
    // Allow for separate UI settings for testnets
    QApplication::setApplicationName(networkStyle->getAppName());
    // Re-initialize translations after changing application name (language in network-specific settings can be different)
    initTranslations(qtTranslatorBase, qtTranslator, translatorBase, translator);

    if (!FastStart::DoDownloadSnapshot())
        return EXIT_SUCCESS;

#ifdef ENABLE_WALLET
    /// 8. URI IPC sending
    // - Do this early as we don't want to bother initializing if we are just calling IPC
    // - Do this *after* setting up the data directory, as the data directory hash is used in the name
    // of the server.
    // - Do this after creating app and setting up translations, so errors are
    // translated properly.
    if (PaymentServer::ipcSendCommandLine())
        exit(EXIT_SUCCESS);

    // Start up the payment server early, too, so impatient users that click on
    // merit: links repeatedly have their payment requests routed to this process:
    app.createPaymentServer();
#endif

    /// 9. Main GUI initialization
    // Install global event filter that makes sure that long tooltips can be word-wrapped
    app.installEventFilter(new GUIUtil::ToolTipToRichTextFilter(TOOLTIP_WRAP_THRESHOLD, &app));
#if QT_VERSION < 0x050000
    // Install qDebug() message handler to route to debug.log
    qInstallMsgHandler(DebugMessageHandler);
#else
#if defined(Q_OS_WIN)
    // Install global event filter for processing Windows session related Windows messages (WM_QUERYENDSESSION and WM_ENDSESSION)
    qApp->installNativeEventFilter(new WinShutdownMonitor());
#endif
    // Install qDebug() message handler to route to debug.log
    qInstallMessageHandler(DebugMessageHandler);
#endif
    // Allow parameter interaction before we create the options model
    app.parameterSetup();
    // Load GUI settings from QSettings
    app.createOptionsModel(gArgs.IsArgSet("-resetguisettings"));

    // Subscribe to global signals from core
    uiInterface.InitMessage.connect(InitMessage);

    if (gArgs.GetBoolArg("-splash", DEFAULT_SPLASHSCREEN) && !gArgs.GetBoolArg("-min", false))
        app.createSplashScreen(networkStyle.data());

    int rv = EXIT_SUCCESS;
    try
    {
        app.createWindow(networkStyle.data());
        // Perform base initialization before spinning up initialization/shutdown thread
        // This is acceptable because this function only contains steps that are quick to execute,
        // so the GUI thread won't be held up.
        if (MeritCore::baseInitialize()) {
            app.requestInitialize();
#if defined(Q_OS_WIN) && QT_VERSION >= 0x050000
            WinShutdownMonitor::registerShutdownBlockReason(QObject::tr("%1 didn't yet exit safely...").arg(QObject::tr(PACKAGE_NAME)), (HWND)app.getMainWinId());
#endif
            app.exec();
            app.requestShutdown();
            app.exec();
            rv = app.getReturnValue();
        } else {
            // A dialog with detailed error will have been shown by InitError()
            rv = EXIT_FAILURE;
        }
    } catch (const std::exception& e) {
        PrintExceptionContinue(&e, "Runaway exception");
        app.handleRunawayException(QString::fromStdString(GetWarnings("gui")));
    } catch (...) {
        PrintExceptionContinue(nullptr, "Runaway exception");
        app.handleRunawayException(QString::fromStdString(GetWarnings("gui")));
    }
    return rv;
}
#endif // MERIT_QT_TEST
