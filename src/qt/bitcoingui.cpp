/*
 * Qt4 bitcoin GUI.
 *
 * W.J. van der Laan 2011-2012
 * The Bitcoin Developers 2011-2012
 */
#include "bitcoingui.h"
#include "transactiontablemodel.h"
#include "addressbookpage.h"
#include "sendcoinsdialog.h"
#include "signverifymessagedialog.h"
#include "optionsdialog.h"
#include "aboutdialog.h"
#include "clientmodel.h"
#include "walletmodel.h"
#include "editaddressdialog.h"
#include "optionsmodel.h"
#include "transactiondescdialog.h"
#include "addresstablemodel.h"
#include "transactionview.h"
#include "overviewpage.h"
#include "bitcoinunits.h"
#include "guiconstants.h"
#include "askpassphrasedialog.h"
#include "notificator.h"
#include "guiutil.h"
#include "rpcconsole.h"
#include "wallet.h"
#include "ui_interface.h"
#include "net.h"

#ifdef Q_OS_MAC
#include "macdockiconhandler.h"
#endif

#include <QApplication>
#include <QMainWindow>
#include <QMenuBar>
#include <QMenu>
#include <QIcon>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QToolBar>
#include <QStatusBar>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QLocale>
#include <QMessageBox>
#include <QProgressBar>
#include <QStackedWidget>
#include <QDateTime>
#include <QMovie>
#include <QFileDialog>
#include <QDesktopServices>
#if QT_VERSION < 0x050000
#include <QDesktopServices>
#else
#include <QStandardPaths>
#endif
#include <QTimer>
#include <QDragEnterEvent>
#if QT_VERSION < 0x050000
#include <QUrl>
#endif
#include <QMimeData>
#include <QStyle>

#include <iostream>

extern CWallet* pwalletMain;
extern int64 nLastCoinStakeSearchInterval;

BitcoinGUI::BitcoinGUI(QWidget *parent):
    QMainWindow(parent),
    clientModel(0),
    walletModel(0),
    encryptWalletAction(0),
    changePassphraseAction(0),
    unlockWalletAction(0),
    lockWalletAction(0),
    aboutQtAction(0),
    trayIcon(0),
    notificator(0),
    rpcConsole(0),
    prevBlocks(0)
{

  	QFile style(":/text/res/text/style.qss");
	style.open(QFile::ReadOnly);
	setStyleSheet(QString::fromUtf8(style.readAll()));

    resize(850, 550);
    setWindowTitle(tr("Philosopherstone") + " - " + tr("Wallet"));
#ifndef Q_OS_MAC
    qApp->setWindowIcon(QIcon(":icons/bitcoin"));
    setWindowIcon(QIcon(":icons/bitcoin"));
#else
    setUnifiedTitleAndToolBarOnMac(true);
    QApplication::setAttribute(Qt::AA_DontShowIconsInMenus);
#endif
    // Accept D&D of URIs
    setAcceptDrops(true);

    // Create actions for the toolbar, menu bar and tray/dock icon
    createActions();

    // Create application menu bar
    createMenuBar();

    // Create the toolbars
    createToolBars();

    // Create system tray icon and notification
    createTrayIcon();

    // Create tabs
    overviewPage = new OverviewPage();

    transactionsPage = new QWidget(this);
    QVBoxLayout *vbox = new QVBoxLayout();
    transactionView = new TransactionView(this);
    vbox->addWidget(transactionView);
    transactionsPage->setLayout(vbox);

    addressBookPage = new AddressBookPage(AddressBookPage::ForEditing, AddressBookPage::SendingTab);

    receiveCoinsPage = new AddressBookPage(AddressBookPage::ForEditing, AddressBookPage::ReceivingTab);

    sendCoinsPage = new SendCoinsDialog(this);

    signVerifyMessageDialog = new SignVerifyMessageDialog(this);

    centralWidget = new QStackedWidget(this);
    centralWidget->addWidget(overviewPage);
		overviewPage->setStyleSheet("background: transparent");
    centralWidget->addWidget(transactionsPage);
		sendCoinsPage->setStyleSheet("background-color: transparent;");
		transactionsPage->setStyleSheet("background: #1b2f2f;");
    centralWidget->addWidget(addressBookPage);	
		sendCoinsPage->setStyleSheet("background-color: transparent;");
		addressBookPage->setStyleSheet("background: #1b2f2f;");
    centralWidget->addWidget(receiveCoinsPage);
		sendCoinsPage->setStyleSheet("background-color: #1b2f2f;");
		receiveCoinsPage->setStyleSheet("background: #1b2f2f;");
    centralWidget->addWidget(sendCoinsPage);	
		sendCoinsPage->setStyleSheet("background-color: #1b2f2f;");
		sendCoinsPage->setStyleSheet("background: #1b2f2f;");
	
    setCentralWidget(centralWidget);

    // Create status bar
    statusBar();

    // Status bar notification icons
    QFrame *frameBlocks = new QFrame();
    frameBlocks->setContentsMargins(0,0,0,0);
    frameBlocks->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    QHBoxLayout *frameBlocksLayout = new QHBoxLayout(frameBlocks);
    frameBlocksLayout->setContentsMargins(3,0,3,0);
    frameBlocksLayout->setSpacing(3);
    labelEncryptionIcon = new GUIUtil::ClickableLabel();

    labelConnectionsIcon = new GUIUtil::ClickableLabel();
    connect(labelConnectionsIcon, SIGNAL(clicked()),this,SLOT(connectionIconClicked()));

    labelBlocksIcon = new GUIUtil::ClickableLabel();
    connect(labelBlocksIcon, SIGNAL(clicked()),this,SLOT(blocksIconClicked()));

    labelStakingIcon = new GUIUtil::ClickableLabel();
    connect(labelStakingIcon, SIGNAL(clicked()), this, SLOT(blocksIconClicked()));


    frameBlocksLayout->addStretch();
    frameBlocksLayout->addWidget(labelEncryptionIcon);
    frameBlocksLayout->addStretch();
    frameBlocksLayout->addWidget(labelStakingIcon);
    frameBlocksLayout->addStretch();
    frameBlocksLayout->addWidget(labelConnectionsIcon);
    frameBlocksLayout->addStretch();
    frameBlocksLayout->addWidget(labelBlocksIcon);
    frameBlocksLayout->addStretch();

    QTimer *timerStakingIcon = new QTimer(labelStakingIcon);
    connect(timerStakingIcon, SIGNAL(timeout()), this, SLOT(updateStakingIcon()));
    timerStakingIcon->start(30 * 1000);
     updateStakingIcon();

	 // Progress bar and label for blocks download
    progressBarLabel = new QLabel();
    progressBarLabel->setVisible(false);
    progressBar = new QProgressBar();
    progressBar->setAlignment(Qt::AlignCenter);
    progressBar->setVisible(false);

    // Override style sheet for progress bar for styles that have a segmented progress bar,
    // as they make the text unreadable (workaround for issue #1071)
    // See https://qt-project.org/doc/qt-4.8/gallery.html
    QString curStyle = qApp->style()->metaObject()->className();
    if(curStyle == "QWindowsStyle" || curStyle == "QWindowsXPStyle")
    {
        progressBar->setStyleSheet("QProgressBar { background-color: #e8e8e8; border: 1px solid grey; border-radius: 7px; padding: 1px; text-align: center; } QProgressBar::chunk { background: QLinearGradient(x1: 0, y1: 0, x2: 1, y2: 0, stop: 0 #FF8000, stop: 1 orange); border-radius: 7px; margin: 0px; }");
    }

    statusBar()->addWidget(progressBarLabel);
    statusBar()->addWidget(progressBar);
    statusBar()->addPermanentWidget(frameBlocks);

    syncIconMovie = new QMovie(":/movies/update_spinner", "mng", this);

    // Clicking on a transaction on the overview page simply sends you to transaction history page
    connect(overviewPage, SIGNAL(transactionClicked(QModelIndex)), this, SLOT(gotoHistoryPage()));
    connect(overviewPage, SIGNAL(transactionClicked(QModelIndex)), transactionView, SLOT(focusTransaction(QModelIndex)));

    // Double-clicking on a transaction on the transaction history page shows details
    connect(transactionView, SIGNAL(doubleClicked(QModelIndex)), transactionView, SLOT(showDetails()));

    rpcConsole = new RPCConsole(this);
    connect(openRPCConsoleAction, SIGNAL(triggered()), rpcConsole, SLOT(show()));

    // Clicking on "Verify Message" in the address book sends you to the verify message tab
    connect(addressBookPage, SIGNAL(verifyMessage(QString)), this, SLOT(gotoVerifyMessageTab(QString)));
    // Clicking on "Sign Message" in the receive coins page sends you to the sign message tab
    connect(receiveCoinsPage, SIGNAL(signMessage(QString)), this, SLOT(gotoSignMessageTab(QString)));
	
    // Install event filter to be able to catch status tip events (QEvent::StatusTip)
    this->installEventFilter(this);

    gotoOverviewPage();
}

BitcoinGUI::~BitcoinGUI()
{
    if(trayIcon) // Hide tray icon, as deleting will let it linger until quit (on Ubuntu)
        trayIcon->hide();
#ifdef Q_OS_MAC
    delete appMenuBar;
#endif
}

void BitcoinGUI::createActions()
{
    QActionGroup *tabGroup = new QActionGroup(this);
		
    actionCommunity = new QAction(QIcon(":/icons/tsu"), tr(""), this);
    actionCommunity->setStatusTip(tr("PHS Community Channel"));
    actionCommunity->setToolTip(actionCommunity->statusTip());
	
    actionHomepage = new QAction(QIcon(":/icons/stones"), tr(""), this);
    actionHomepage->setStatusTip(tr("PHS Homepage"));
    actionHomepage->setToolTip(actionHomepage->statusTip());
	
    actionExchanger = new QAction(QIcon(":/icons/cryptsy"), tr(""), this);
    actionExchanger->setStatusTip(tr("Buy and Sell PHS"));
    actionExchanger->setToolTip(actionExchanger->statusTip());
	
    actionExplorer = new QAction(QIcon(":/icons/abe"), tr(""), this);
    actionExplorer->setStatusTip(tr("PHS Block Explorer"));
    actionExplorer->setToolTip(actionExplorer->statusTip());
	
    overviewAction = new QAction(QIcon(":/icons/overview"), tr("&Home"), this);
    overviewAction->setStatusTip(tr("Wallet Overview"));
    overviewAction->setToolTip(overviewAction->statusTip());
    overviewAction->setCheckable(true);
    overviewAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_1));
    tabGroup->addAction(overviewAction);

    sendCoinsAction = new QAction(QIcon(":/icons/send"), tr("&Send"), this);
    sendCoinsAction->setStatusTip(tr("Send coins to a Philosopherstone address"));
    sendCoinsAction->setToolTip(sendCoinsAction->statusTip());
    sendCoinsAction->setCheckable(true);
    sendCoinsAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_2));
    tabGroup->addAction(sendCoinsAction);

    receiveCoinsAction = new QAction(QIcon(":/icons/receiving_addresses"), tr("&Receive"), this);
    receiveCoinsAction->setStatusTip(tr("Show the list of addresses for receiving payments"));
    receiveCoinsAction->setToolTip(receiveCoinsAction->statusTip());
    receiveCoinsAction->setCheckable(true);
    receiveCoinsAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_3));
    tabGroup->addAction(receiveCoinsAction);

    historyAction = new QAction(QIcon(":/icons/history"), tr("&History"), this);
    historyAction->setStatusTip(tr("Browse transaction history"));
    historyAction->setToolTip(historyAction->statusTip());
    historyAction->setCheckable(true);
    historyAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_4));
    tabGroup->addAction(historyAction);

    addressBookAction = new QAction(QIcon(":/icons/address-book"), tr("&Address Book"), this);
    addressBookAction->setStatusTip(tr("Edit the list of stored addresses and labels"));
    addressBookAction->setToolTip(addressBookAction->statusTip());
    addressBookAction->setCheckable(true);
    addressBookAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_5));
    tabGroup->addAction(addressBookAction);

    connect(overviewAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(overviewAction, SIGNAL(triggered()), this, SLOT(gotoOverviewPage()));
    connect(sendCoinsAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(sendCoinsAction, SIGNAL(triggered()), this, SLOT(gotoSendCoinsPage()));
    connect(receiveCoinsAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(receiveCoinsAction, SIGNAL(triggered()), this, SLOT(gotoReceiveCoinsPage()));
    connect(historyAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(historyAction, SIGNAL(triggered()), this, SLOT(gotoHistoryPage()));
    connect(addressBookAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(addressBookAction, SIGNAL(triggered()), this, SLOT(gotoAddressBookPage()));

    quitAction = new QAction(QIcon(":/icons/quit"), tr("E&xit"), this);
    quitAction->setStatusTip(tr("Quit application"));
    quitAction->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_Q));
    quitAction->setMenuRole(QAction::QuitRole);
    aboutAction = new QAction(QIcon(":/icons/tx_mined"), tr("&About Philosopherstone"), this);
    aboutAction->setStatusTip(tr("Show information about Philosopherstone"));
    aboutAction->setMenuRole(QAction::AboutRole);
    aboutQtAction = new QAction(QIcon(":/icons/qtlogo"), tr("About &Qt"), this);
    aboutQtAction->setStatusTip(tr("Show information about Qt"));
    aboutQtAction->setMenuRole(QAction::AboutQtRole);
    optionsAction = new QAction(QIcon(":/icons/options"), tr("&Options..."), this);
    optionsAction->setStatusTip(tr("Modify configuration options for Philosopherstone"));
    optionsAction->setMenuRole(QAction::PreferencesRole);
    toggleHideAction = new QAction(QIcon(":/icons/bitcoin"), tr("&Show / Hide"), this);
    encryptWalletAction = new QAction(QIcon(":/icons/lock_closed"), tr("&Encrypt Wallet..."), this);
    encryptWalletAction->setStatusTip(tr("Encrypt the private keys that belong to your wallet"));
    encryptWalletAction->setCheckable(true);
    backupWalletAction = new QAction(QIcon(":/icons/filesave"), tr("&Backup Wallet..."), this);
    backupWalletAction->setStatusTip(tr("Backup wallet to another location"));
    dumpWalletAction = new QAction(QIcon(":/icons/export2"), tr("&Export Wallet..."), this);
    dumpWalletAction->setStatusTip(tr("Export wallet's keys to a text file"));
    importWalletAction = new QAction(QIcon(":/icons/import"), tr("&Import Wallet..."), this);
    importWalletAction->setStatusTip(tr("Import a file's keys into a wallet"));
    changePassphraseAction = new QAction(QIcon(":/icons/key"), tr("&Change Passphrase..."), this);
    changePassphraseAction->setStatusTip(tr("Change the passphrase used for wallet encryption"));
	
    unlockWalletAction = new QAction(QIcon(":/icons/lock_open"), tr("&Unlock Wallet..."), this);
    unlockWalletAction->setStatusTip(tr("Unlock wallet for Minting"));
    lockWalletAction = new QAction(QIcon(":/icons/lock_closed"), tr("&Lock Wallet..."), this);
    lockWalletAction->setStatusTip(tr("Lock the wallet"));
    lockWalletAction->setCheckable(true);
    checkWalletAction = new QAction(QIcon(":/icons/inspect"), tr("&Check Wallet..."), this);
    checkWalletAction->setStatusTip(tr("Check wallet integrity and report findings"));
    repairWalletAction = new QAction(QIcon(":/icons/repair"), tr("&Repair Wallet..."), this);
    repairWalletAction->setStatusTip(tr("Fix wallet integrity and remove orphans"));
	
    signMessageAction = new QAction(QIcon(":/icons/edit"), tr("Sign &message..."), this);
    signMessageAction->setStatusTip(tr("Sign messages with your Philosopherstone addresses to prove you own them"));
    verifyMessageAction = new QAction(QIcon(":/icons/transaction_0"), tr("&Verify message..."), this);
    verifyMessageAction->setStatusTip(tr("Verify messages to ensure they were signed with specified Philosopherstone addresses"));

    exportAction = new QAction(QIcon(":/icons/export"), tr("&Export..."), this);
    exportAction->setStatusTip(tr("Export the data in the current tab to a file"));
    exportAction->setToolTip(exportAction->statusTip());
	
    openRPCConsoleAction = new QAction(QIcon(":/icons/debugwindow"), tr("&Debug window"), this);
    openRPCConsoleAction->setStatusTip(tr("Open debugging and diagnostic console"));
    openRPCConsoleAction->setToolTip(openRPCConsoleAction->statusTip());

    connect(quitAction, SIGNAL(triggered()), qApp, SLOT(quit()));
    connect(aboutAction, SIGNAL(triggered()), this, SLOT(aboutClicked()));
    connect(aboutQtAction, SIGNAL(triggered()), qApp, SLOT(aboutQt()));
    connect(optionsAction, SIGNAL(triggered()), this, SLOT(optionsClicked()));
    connect(toggleHideAction, SIGNAL(triggered()), this, SLOT(toggleHidden()));
    connect(encryptWalletAction, SIGNAL(triggered(bool)), this, SLOT(encryptWallet(bool)));
    connect(checkWalletAction, SIGNAL(triggered()), this, SLOT(checkWallet()));
    connect(repairWalletAction, SIGNAL(triggered()), this, SLOT(repairWallet()));
    connect(backupWalletAction, SIGNAL(triggered()), this, SLOT(backupWallet()));
    connect(dumpWalletAction, SIGNAL(triggered()), this, SLOT(dumpWallet()));
    connect(importWalletAction, SIGNAL(triggered()), this, SLOT(importWallet()));
    connect(changePassphraseAction, SIGNAL(triggered()), this, SLOT(changePassphrase()));
    connect(unlockWalletAction, SIGNAL(triggered()), this, SLOT(unlockWallet()));
    connect(signMessageAction, SIGNAL(triggered()), this, SLOT(gotoSignMessageTab()));
    connect(verifyMessageAction, SIGNAL(triggered()), this, SLOT(gotoVerifyMessageTab()));
    connect(lockWalletAction, SIGNAL(triggered()), this, SLOT(lockWallet()));
}

void BitcoinGUI::createMenuBar()
{
#ifdef Q_OS_MAC
    // Create a decoupled menu bar on Mac which stays even if the window is closed
    appMenuBar = new QMenuBar();
#else
    // Get the main window's menu bar on other platforms
    appMenuBar = menuBar();
#endif

    // Configure the menus
    QMenu *file = appMenuBar->addMenu(tr("&File"));
    file->addAction(backupWalletAction);
    file->addSeparator();
    file->addAction(dumpWalletAction);
    file->addAction(importWalletAction);
    file->addSeparator();
    file->addAction(exportAction);
    file->addSeparator();
    file->addAction(quitAction);

    QMenu *settings = appMenuBar->addMenu(tr("&Settings"));
    settings->addAction(optionsAction);

    QMenu *wallet = appMenuBar->addMenu(tr("&Wallet"));
    wallet->addAction(encryptWalletAction);
    wallet->addAction(changePassphraseAction);
    wallet->addAction(unlockWalletAction);
    wallet->addAction(lockWalletAction);
    wallet->addSeparator();
    wallet->addAction(checkWalletAction);
    wallet->addAction(repairWalletAction);
    wallet->addSeparator();
    wallet->addAction(signMessageAction);
    wallet->addAction(verifyMessageAction);

    QMenu *help = appMenuBar->addMenu(tr("&Help"));
    help->addAction(openRPCConsoleAction);
    help->addSeparator();
    help->addAction(aboutAction);
    help->addAction(aboutQtAction);
}

void BitcoinGUI::createToolBars()
{
    QToolBar *toolbar = addToolBar(tr("Tabs toolbar"));
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolbar->addAction(overviewAction);
    toolbar->addAction(sendCoinsAction);
    toolbar->addAction(receiveCoinsAction);
    toolbar->addAction(historyAction);
    toolbar->addAction(addressBookAction);

    QToolBar *toolbar2 = addToolBar(tr("Actions toolbar"));
    toolbar2->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolbar2->addAction(exportAction);
    toolbar2->addAction(actionCommunity);
    toolbar2->addAction(actionExchanger);
    toolbar2->addAction(actionHomepage);
    toolbar2->addAction(actionExplorer);

    connect(actionCommunity, SIGNAL(triggered()), this, SLOT(openCommunity()));
    connect(actionExchanger, SIGNAL(triggered()), this, SLOT(openExchanger()));
    connect(actionHomepage, SIGNAL(triggered()), this, SLOT(openHomepage()));
    connect(actionExplorer, SIGNAL(triggered()), this, SLOT(openExplorer()));
}

void BitcoinGUI::setClientModel(ClientModel *clientModel)
{
    this->clientModel = clientModel;
    if(clientModel)
    {
        // Replace some strings and icons, when using the testnet
        if(clientModel->isTestNet())
        {
            setWindowTitle(windowTitle() + QString(" ") + tr("[testnet]"));
#ifndef Q_OS_MAC
            qApp->setWindowIcon(QIcon(":icons/bitcoin_testnet"));
            setWindowIcon(QIcon(":icons/bitcoin_testnet"));
#else
            MacDockIconHandler::instance()->setIcon(QIcon(":icons/bitcoin_testnet"));
#endif
            if(trayIcon)
            {
                trayIcon->setToolTip(tr("Philosopherstone client") + QString(" ") + tr("[testnet]"));
                trayIcon->setIcon(QIcon(":/icons/toolbar_testnet"));
                toggleHideAction->setIcon(QIcon(":/icons/toolbar_testnet"));
            }

            aboutAction->setIcon(QIcon(":/icons/toolbar_testnet"));
        }

    // Create system tray menu (or setup the dock menu) that late to prevent users from calling actions,
    // while the client has not yet fully loaded

    if(trayIcon)
        createTrayIconMenu();

        // Keep up to date with client
        setNumConnections(clientModel->getNumConnections());
        connect(clientModel, SIGNAL(numConnectionsChanged(int)), this, SLOT(setNumConnections(int)));

        setNumBlocks(clientModel->getNumBlocks(), clientModel->getNumBlocksOfPeers());
        connect(clientModel, SIGNAL(numBlocksChanged(int,int)), this, SLOT(setNumBlocks(int,int)));
        connect(clientModel, SIGNAL(numBlocksChanged(int,int)), this, SLOT(updateStakingIcon()));

        // Report errors from network/worker thread
        connect(clientModel, SIGNAL(message(QString,QString,unsigned int)), this, SLOT(message(QString,QString,unsigned int)));

        rpcConsole->setClientModel(clientModel);
        overviewPage->setClientModel(clientModel);
        addressBookPage->setOptionsModel(clientModel->getOptionsModel());
        receiveCoinsPage->setOptionsModel(clientModel->getOptionsModel());
    }
}

void BitcoinGUI::setWalletModel(WalletModel *walletModel)
{
    this->walletModel = walletModel;
    if(walletModel)
    {
        // Report errors from wallet thread
        connect(walletModel, SIGNAL(message(QString,QString,unsigned int)), this, SLOT(message(QString,QString,unsigned int)));

        // Put transaction list in tabs
        transactionView->setModel(walletModel);

        overviewPage->setWalletModel(walletModel);
        addressBookPage->setModel(walletModel->getAddressTableModel());
        receiveCoinsPage->setModel(walletModel->getAddressTableModel());
        sendCoinsPage->setModel(walletModel);
        signVerifyMessageDialog->setModel(walletModel);

        setEncryptionStatus(walletModel->getEncryptionStatus());
        connect(walletModel, SIGNAL(encryptionStatusChanged(int)), this, SLOT(setEncryptionStatus(int)));

        // Balloon pop-up for new transaction
        connect(walletModel->getTransactionTableModel(), SIGNAL(rowsInserted(QModelIndex,int,int)),
                this, SLOT(incomingTransaction(QModelIndex,int,int)));

        // Ask for passphrase if needed
        connect(walletModel, SIGNAL(requireUnlock()), this, SLOT(unlockWallet()));
    }
}

void BitcoinGUI::createTrayIcon()
{

#ifndef Q_OS_MAC
    trayIcon = new QSystemTrayIcon(this);

    trayIcon->setToolTip(tr("Philosopherstone client"));
    trayIcon->setIcon(QIcon(":/icons/toolbar"));
    trayIcon->show();
#endif

    notificator = new Notificator(qApp->applicationName(), trayIcon);
}


void BitcoinGUI::createTrayIconMenu()
{
    QMenu *trayIconMenu;
#ifndef Q_OS_MAC
    trayIconMenu = new QMenu(this);
    trayIcon->setContextMenu(trayIconMenu);
    connect(trayIcon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)),
            this, SLOT(trayIconActivated(QSystemTrayIcon::ActivationReason)));
#else
    // Note: On Mac, the dock icon is used to provide the tray's functionality.
    MacDockIconHandler *dockIconHandler = MacDockIconHandler::instance();
    trayIconMenu = dockIconHandler->dockMenu();
    dockIconHandler->setMainWindow((QMainWindow*)this);
#endif

    // Configuration of the tray icon (or dock icon) icon menu
#ifndef Q_OS_MAC // This is built-in on Mac
    trayIconMenu->addAction(toggleHideAction);
    trayIconMenu->addSeparator();
#endif
    trayIconMenu->addAction(sendCoinsAction);
    trayIconMenu->addAction(receiveCoinsAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(signMessageAction);
    trayIconMenu->addAction(verifyMessageAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(optionsAction);
    trayIconMenu->addAction(openRPCConsoleAction);
#ifndef Q_OS_MAC // This is built-in on Mac
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(quitAction);
#endif
}

#ifndef Q_OS_MAC
void BitcoinGUI::trayIconActivated(QSystemTrayIcon::ActivationReason reason)
{
    if(reason == QSystemTrayIcon::Trigger)
    {
        // Click on system tray icon triggers show/hide of the main window
        toggleHideAction->trigger();
    }
}
#endif

void BitcoinGUI::optionsClicked()
{
    if(!clientModel || !clientModel->getOptionsModel())
        return;
    OptionsDialog dlg;
    dlg.setModel(clientModel->getOptionsModel());
    dlg.exec();
}

void BitcoinGUI::aboutClicked()
{
    AboutDialog dlg;
    dlg.setModel(clientModel);
    dlg.exec();
}

void BitcoinGUI::blocksIconClicked()
{
   TRY_LOCK(cs_main, lockMain);
   if(!lockMain)
       return;
	   
   uint64 nMinWeight = 0, nMaxWeight = 0, nWeight = 0, nNetweightk = 0;
   walletModel->getStakeWeight(nMinWeight,nMaxWeight,nWeight);
   nNetweightk=(clientModel->getPosKernalPS()/1000);
		
   int unit = clientModel->getOptionsModel()->getDisplayUnit();

   message(tr("Extended Block Chain Information"),
       tr("Client Version: %1\n"
         "Protocol Version: %2\n"
         "Wallet Version: %3\n\n"
         "Last Block Number: %4\n"
         "Last Block Time: %5\n\n"
         "Split Threshold: %6\n"
         "Combine Threshold: %7\n\n"
         "Current Difficulty: %8 \n\n"      
         "Network Weight: %9 K\n"
         "Your Weight: %10\n\n"
         "Money Supply: %11\n")

         .arg(clientModel->formatFullVersion())
         .arg(clientModel->getProtocolVersion())
         .arg(walletModel->getWalletVersion())
         .arg(clientModel->getNumBlocks())
         .arg(clientModel->getLastBlockDate().toString())
         .arg(BitcoinUnits::formatWithUnit(unit, nSplitThreshold, false))
         .arg(BitcoinUnits::formatWithUnit(unit, nCombineThreshold, false))
         .arg(clientModel->GetDifficulty())
         .arg(nNetweightk)
         .arg(nWeight)
         .arg(BitcoinUnits::formatWithUnit(unit, clientModel->getMoneySupply(), false))
       ,CClientUIInterface::MODAL);
}

void BitcoinGUI::lockIconClicked()
{
    if(!walletModel)
        return;

    if(walletModel->getEncryptionStatus() == WalletModel::Locked)
        unlockWallet();
}

void BitcoinGUI::connectionIconClicked()
{

   QString strAllPeer;
   QVector<CNodeStats> qvNodeStats = clientModel->getPeerStats();
   uint64 nTotSendBytes = 0, nTotRecvBytes = 0 ,nTotBlocksRequested = 0;

   BOOST_FOREACH(const CNodeStats& stats, qvNodeStats) {
      QString strPeer;
      nTotSendBytes+=stats.nSendBytes;
      nTotRecvBytes+=stats.nRecvBytes;
      nTotBlocksRequested+=stats.nBlocksRequested;

      strPeer=tr("Peer IP: %1\n") .arg(stats.addrName.c_str());
      strPeer=strPeer+tr("Time Connected: %1\n") .arg(QDateTime::fromTime_t(QDateTime::currentDateTimeUtc().toTime_t() - stats.nTimeConnected).toUTC().toString("hh:mm:ss"));
      strPeer=strPeer+tr("Time of Last Send: %1\n") .arg(QDateTime::fromTime_t(stats.nLastSend).toString());
      strPeer=strPeer+tr("Time of Last Recv: %1\n") .arg(QDateTime::fromTime_t(stats.nLastRecv).toString());
      strPeer=strPeer+tr("KBytes Sent: %1\n") .arg(stats.nSendBytes/1000);
      strPeer=strPeer+tr("KBytes Recv: %1\n") .arg(stats.nRecvBytes/1000);
      strPeer=strPeer+tr("Blocks Requested: %1\n") .arg(stats.nBlocksRequested);
      strPeer=strPeer+tr("Version: %1\n") .arg(stats.nVersion);
      strPeer=strPeer+tr("SubVersion: %1\n") .arg(stats.strSubVer.c_str());
      strPeer=strPeer+tr("Inbound?: %1\n") .arg(stats.fInbound ? "N": "Y");
      strPeer=strPeer+tr("Starting Block: %1\n") .arg(stats.nStartingHeight);
      strPeer=strPeer+tr("Ban Score(100 max): %1\n\n") .arg(stats.nMisbehavior);

      strAllPeer=strAllPeer+strPeer;
   }

  message(tr("Extended Peer Information"),
          tr("\tNumber of Connections: %1\n"
             "\tTotal KBytes Recv: %2\n"
             "\tTotal KBytes Sent: %3\n"
             "\tTotal Blocks Requested: %4\n\n"
             "\tPlease click \"Show Details\" for more information.\n")
          .arg(clientModel->getNumConnections())
          .arg(nTotRecvBytes/1000)
          .arg(nTotSendBytes/1000)
          .arg(nTotBlocksRequested),
          CClientUIInterface::MODAL,
          tr("%1")
          .arg(strAllPeer));
}

void BitcoinGUI::setNumConnections(int count)
{
    QString icon;
    switch(count)
    {
    case 0: icon = ":/icons/connect_0"; break;
    case 1: case 2: case 3: icon = ":/icons/connect_1"; break;
    case 4: case 5: case 6: icon = ":/icons/connect_2"; break;
    case 7: case 8: case 9: icon = ":/icons/connect_3"; break;
    default: icon = ":/icons/connect_4"; break;
    }
    labelConnectionsIcon->setPixmap(QIcon(icon).pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));
    labelConnectionsIcon->setToolTip(tr("%n active connection(s) to Philosopherstone network", "", count));
}

void BitcoinGUI::setNumBlocks(int count, int nTotalBlocks)
{
    // don't show / hide progress bar and its label if we have no connection to the network
    if (!clientModel || clientModel->getNumConnections() == 0)
    {
        statusBar()->setVisible(false);

        return;
    }

    bool fShowStatusBar = true;
    QString tooltip;
    QDateTime lastBlockDate = clientModel->getLastBlockDate();
    QDateTime currentDate = QDateTime::currentDateTime();
    int totalSecs = GetTime() - 1374911200 ;
    int secs = lastBlockDate.secsTo(currentDate);

    if(count < nTotalBlocks)
    {
        tooltip = tr("Processed %1 of %2 (estimated) blocks of transaction history.").arg(count).arg(nTotalBlocks);
    }
    else
    {
        tooltip = tr("Processed %1 blocks of transaction history.").arg(count);
    }

    // Set icon state: spinning if catching up, tick otherwise
    if(secs < 90*60 && count >= nTotalBlocks)
    {
        tooltip = tr("Up to date") + QString(".<br>") + tooltip;
        labelBlocksIcon->setPixmap(QIcon(":/icons/synced").pixmap(STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE));

        overviewPage->showOutOfSyncWarning(false);

        progressBarLabel->setVisible(false);
        progressBar->setVisible(false);
    }
    else
    {
        // Represent time from last generated block in human readable text
        QString timeBehindText;
        const int HOUR_IN_SECONDS = 60*60;
        const int DAY_IN_SECONDS = 24*60*60;
        const int WEEK_IN_SECONDS = 7*24*60*60;
        const int YEAR_IN_SECONDS = 31556952; // Average length of year in Gregorian calendar
        if(secs < 2*DAY_IN_SECONDS)
        {
            timeBehindText = tr("%n hour(s)","",secs/HOUR_IN_SECONDS);
        }
        else if(secs < 2*WEEK_IN_SECONDS)
        {
            timeBehindText = tr("%n day(s)","",secs/DAY_IN_SECONDS);
        }
        else if(secs < YEAR_IN_SECONDS)
        {
            timeBehindText = tr("%n week(s)","",secs/WEEK_IN_SECONDS);
        }
        else
        {
            int years = secs / YEAR_IN_SECONDS;
            int remainder = secs % YEAR_IN_SECONDS;
            timeBehindText = tr("%1 and %2").arg(tr("%n year(s)", "", years)).arg(tr("%n week(s)","", remainder/WEEK_IN_SECONDS));
        }

        progressBarLabel->setText(tr("Synchronizing with network..."));
        progressBarLabel->setVisible(true);
        progressBar->setFormat(tr("%1 behind").arg(timeBehindText));
        progressBar->setMaximum(totalSecs);
        progressBar->setValue(totalSecs - secs);
        progressBar->setVisible(true);
        fShowStatusBar = true;

        tooltip = tr("Catching up...") + QString("<br>") + tooltip;
        labelBlocksIcon->setMovie(syncIconMovie);
        if(count != prevBlocks)
            syncIconMovie->jumpToNextFrame();
        prevBlocks = count;

        overviewPage->showOutOfSyncWarning(true);

        tooltip += QString("<br>");
        tooltip += tr("Last received block was generated %1 ago.").arg(timeBehindText);
        tooltip += QString("<br>");
        tooltip += tr("Transactions after this will not yet be visible.");
    }

    // Don't word-wrap this (fixed-width) tooltip
    tooltip = QString("<nobr>") + tooltip + QString("</nobr>");

    labelBlocksIcon->setToolTip(tooltip);
    progressBarLabel->setToolTip(tooltip);
    progressBar->setToolTip(tooltip);
	if(GetBoolArg("-chart", true) && count > 0 && nTotalBlocks > 0)
    {
        overviewPage->updatePlot(count);
    }
    statusBar()->setVisible(fShowStatusBar);
}

void BitcoinGUI::message(const QString &title, const QString &message, unsigned int style, const QString &detail)
{
  QString strTitle = tr("Philosopherstone") + " - ";
  // Default to information icon
  int nMBoxIcon = QMessageBox::Information;
  int nNotifyIcon = Notificator::Information;

  // Check for usage of predefined title
  switch (style) {
  case CClientUIInterface::MSG_ERROR:
      strTitle += tr("Error");
      break;
  case CClientUIInterface::MSG_WARNING:
      strTitle += tr("Warning");
      break;
  case CClientUIInterface::MSG_INFORMATION:
      strTitle += tr("Information");
      break;
  default:
      strTitle += title; // Use supplied title
  }

  // Check for error/warning icon
  if (style & CClientUIInterface::ICON_ERROR) {
      nMBoxIcon = QMessageBox::Critical;
      nNotifyIcon = Notificator::Critical;
  }
  else if (style & CClientUIInterface::ICON_WARNING) {
      nMBoxIcon = QMessageBox::Warning;
      nNotifyIcon = Notificator::Warning;
  }

  // Display message
  if (style & CClientUIInterface::MODAL) {
      // Check for buttons, use OK as default, if none was supplied
      QMessageBox::StandardButton buttons;
      if (!(buttons = (QMessageBox::StandardButton)(style & CClientUIInterface::BTN_MASK)))
          buttons = QMessageBox::Ok;

      QMessageBox mBox((QMessageBox::Icon)nMBoxIcon, strTitle, message, buttons);

      if(!detail.isEmpty()) { mBox.setDetailedText(detail); }

      mBox.exec();
  }
  else
      notificator->notify((Notificator::Class)nNotifyIcon, strTitle, message);
}
void BitcoinGUI::changeEvent(QEvent *e)
{
    QMainWindow::changeEvent(e);
#ifndef Q_OS_MAC // Ignored on Mac
    if(e->type() == QEvent::WindowStateChange)
    {
        if(clientModel && clientModel->getOptionsModel()->getMinimizeToTray())
        {
            QWindowStateChangeEvent *wsevt = static_cast<QWindowStateChangeEvent*>(e);
            if(!(wsevt->oldState() & Qt::WindowMinimized) && isMinimized())
            {
                QTimer::singleShot(0, this, SLOT(hide()));
                e->ignore();
            }
        }
    }
#endif
}

void BitcoinGUI::closeEvent(QCloseEvent *event)
{
    if(clientModel)
    {
#ifndef Q_OS_MAC // Ignored on Mac
        if(!clientModel->getOptionsModel()->getMinimizeToTray() &&
           !clientModel->getOptionsModel()->getMinimizeOnClose())
        {
            qApp->quit();
        }
#endif
    }
    QMainWindow::closeEvent(event);
}

void BitcoinGUI::askFee(qint64 nFeeRequired, bool *payFee)
{
    QString strMessage =
        tr("This transaction requires a fee.  You can still send it for a fee of %1, "
          "This fee will be destroyed and removed from circulation "
          "Do you want to pay the fee?").arg(
                BitcoinUnits::formatWithUnit(BitcoinUnits::BTC, nFeeRequired));
    QMessageBox::StandardButton retval = QMessageBox::question(
          this, tr("Confirm transaction fee"), strMessage,
          QMessageBox::Yes|QMessageBox::Cancel, QMessageBox::Yes);
    *payFee = (retval == QMessageBox::Yes);
}

void BitcoinGUI::incomingTransaction(const QModelIndex & parent, int start, int end)
{
    if(!walletModel || !clientModel)
        return;
    TransactionTableModel *ttm = walletModel->getTransactionTableModel();
    qint64 amount = ttm->index(start, TransactionTableModel::Amount, parent)
                    .data(Qt::EditRole).toULongLong();
    if(!clientModel->inInitialBlockDownload())
    {
        // On new transaction, make an info balloon
        // Unless the initial block download is in progress, to prevent balloon-spam
        QString date = ttm->index(start, TransactionTableModel::Date, parent)
                        .data().toString();
        QString type = ttm->index(start, TransactionTableModel::Type, parent)
                        .data().toString();
        QString address = ttm->index(start, TransactionTableModel::ToAddress, parent)
                        .data().toString();
        QIcon icon = qvariant_cast<QIcon>(ttm->index(start,
                            TransactionTableModel::ToAddress, parent)
                        .data(Qt::DecorationRole));

        message((amount)<0 ? tr("Sent transaction") : tr("Incoming transaction"),
           tr("Date: %1\n"
              "Amount: %2\n"
              "Type: %3\n"
              "Address: %4\n")
                .arg(date)
                .arg(BitcoinUnits::formatWithUnit(walletModel->getOptionsModel()->getDisplayUnit(), amount, true))
                .arg(type)
                .arg(address), CClientUIInterface::MSG_INFORMATION);
    }
}

void BitcoinGUI::gotoOverviewPage()
{
    overviewAction->setChecked(true);
    centralWidget->setCurrentWidget(overviewPage);

    exportAction->setEnabled(false);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
}

void BitcoinGUI::gotoHistoryPage()
{
    historyAction->setChecked(true);
    centralWidget->setCurrentWidget(transactionsPage);

    exportAction->setEnabled(true);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
    connect(exportAction, SIGNAL(triggered()), transactionView, SLOT(exportClicked()));
}

void BitcoinGUI::gotoAddressBookPage()
{
    addressBookAction->setChecked(true);
    centralWidget->setCurrentWidget(addressBookPage);

    exportAction->setEnabled(true);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
    connect(exportAction, SIGNAL(triggered()), addressBookPage, SLOT(exportClicked()));
}

void BitcoinGUI::gotoReceiveCoinsPage()
{
    receiveCoinsAction->setChecked(true);
    centralWidget->setCurrentWidget(receiveCoinsPage);

    exportAction->setEnabled(true);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
    connect(exportAction, SIGNAL(triggered()), receiveCoinsPage, SLOT(exportClicked()));
}

void BitcoinGUI::gotoSendCoinsPage()
{
    sendCoinsAction->setChecked(true);
    centralWidget->setCurrentWidget(sendCoinsPage);

    exportAction->setEnabled(false);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
}

void BitcoinGUI::gotoSignMessageTab(QString addr)
{
    // call show() in showTab_SM()
    signVerifyMessageDialog->showTab_SM(true);

    if(!addr.isEmpty())
        signVerifyMessageDialog->setAddress_SM(addr);
}

void BitcoinGUI::gotoVerifyMessageTab(QString addr)
{
    // call show() in showTab_VM()
    signVerifyMessageDialog->showTab_VM(true);

    if(!addr.isEmpty())
        signVerifyMessageDialog->setAddress_VM(addr);
}

void BitcoinGUI::dragEnterEvent(QDragEnterEvent *event)
{
    // Accept only URIs
    if(event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void BitcoinGUI::dropEvent(QDropEvent *event)
{
    if(event->mimeData()->hasUrls())
    {
        int nValidUrisFound = 0;
        QList<QUrl> uris = event->mimeData()->urls();
        foreach(const QUrl &uri, uris)
        {
            if (sendCoinsPage->handleURI(uri.toString()))
                nValidUrisFound++;
        }

        // if valid URIs were found
        if (nValidUrisFound)
            gotoSendCoinsPage();
        else
            message(tr("URI handling"), tr("URI can not be parsed! This can be caused by an invalid Philosopherstone address or malformed URI parameters."),
                    CClientUIInterface::ICON_WARNING);
    }

    event->acceptProposedAction();
}

void BitcoinGUI::handleURI(QString strURI)
{
    // URI has to be valid
    if (sendCoinsPage->handleURI(strURI))
    {
        showNormalIfMinimized();
        gotoSendCoinsPage();
    }
    else
        message(tr("URI handling"), tr("URI can not be parsed! This can be caused by an invalid Philosopherstone address or malformed URI parameters."),
                 CClientUIInterface::ICON_WARNING);
}

bool BitcoinGUI::eventFilter(QObject *object, QEvent *event)
{
    // Catch status tip events
    if (event->type() == QEvent::StatusTip)
    {
        // Prevent adding text from setStatusTip(), if we currently use the status bar for displaying other stuff
        if (progressBarLabel->isVisible() && progressBar->isVisible())
            return true;
    }
    return QMainWindow::eventFilter(object, event);
}

void BitcoinGUI::setEncryptionStatus(int status)
{
    switch(status)
    {
    case WalletModel::Unencrypted:
        labelEncryptionIcon->hide();
        encryptWalletAction->setChecked(false);
        unlockWalletAction->setChecked(false);
        lockWalletAction->setChecked(false);
        changePassphraseAction->setEnabled(false);
        changePassphraseAction->setVisible(false);
        unlockWalletAction->setEnabled(false);
        unlockWalletAction->setVisible(false);
        lockWalletAction->setEnabled(false);
        lockWalletAction->setVisible(false);
        encryptWalletAction->setEnabled(true);
        disconnect(labelEncryptionIcon,SIGNAL(clicked()), this, SLOT(lockIconClicked()));labelEncryptionIcon->setToolTip(tr("Wallet is <b>encrypted</b> and currently <b>unlocked</b>"));
        break;
    case WalletModel::Unlocked:
        labelEncryptionIcon->show();
        labelEncryptionIcon->setPixmap(QIcon(":/icons/lock_open").pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));
        labelEncryptionIcon->setToolTip(tr("Wallet is <b>encrypted</b> and currently <b>unlocked</b>"));
        encryptWalletAction->setChecked(true);
        unlockWalletAction->setChecked(true);
        lockWalletAction->setChecked(true);
        changePassphraseAction->setEnabled(true);
        encryptWalletAction->setEnabled(false); // TODO: decrypt currently not supported
        encryptWalletAction->setVisible(false);
        unlockWalletAction->setEnabled(false);
        unlockWalletAction->setVisible(false);
        lockWalletAction->setEnabled(true);
        lockWalletAction->setVisible(true);
        disconnect(labelEncryptionIcon,SIGNAL(clicked()), this, SLOT(lockIconClicked()));
        break;
    case WalletModel::Locked:
        labelEncryptionIcon->show();
        labelEncryptionIcon->setPixmap(QIcon(":/icons/lock_closed").pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));
        labelEncryptionIcon->setToolTip(tr("Wallet is <b>encrypted</b> and currently <b>locked</b>"));
        encryptWalletAction->setChecked(true);
        unlockWalletAction->setChecked(true);
        lockWalletAction->setChecked(true);
        changePassphraseAction->setEnabled(true);
        encryptWalletAction->setEnabled(false); // TODO: decrypt currently not supported
        encryptWalletAction->setVisible(false);
        unlockWalletAction->setEnabled(true);
        unlockWalletAction->setVisible(true);
        lockWalletAction->setEnabled(false);
        lockWalletAction->setVisible(false);
        connect(labelEncryptionIcon,SIGNAL(clicked()), this, SLOT(lockIconClicked()));
        break;
    }
}

void BitcoinGUI::encryptWallet(bool status)
{
    if(!walletModel)
        return;
    AskPassphraseDialog dlg(status ? AskPassphraseDialog::Encrypt:
                                     AskPassphraseDialog::Decrypt, this);
    dlg.setModel(walletModel);
    dlg.exec();

    setEncryptionStatus(walletModel->getEncryptionStatus());
}
void BitcoinGUI::checkWallet()
{

    int nMismatchSpent;
    int64 nBalanceInQuestion;
    int nOrphansFound;

    if(!walletModel)
        return;

    // Check the wallet as requested by user
    walletModel->checkWallet(nMismatchSpent, nBalanceInQuestion, nOrphansFound);

    if (nMismatchSpent == 0 && nOrphansFound == 0)
        message(tr("Check Wallet Information"),
                tr("Wallet passed integrity test!\n"
                   "Nothing found to fix.")
                  ,CClientUIInterface::MSG_INFORMATION);
  else
       message(tr("Check Wallet Information"),
               tr("Wallet failed integrity test!\n\n"
                  "Mismatched coin(s) found: %1.\n"
                  "Amount in question: %2.\n"
                  "Orphans found: %3.\n\n"
                  "Please backup wallet and run repair wallet.\n")
                        .arg(nMismatchSpent)
                        .arg(BitcoinUnits::formatWithUnit(walletModel->getOptionsModel()->getDisplayUnit(), nBalanceInQuestion,true))
                        .arg(nOrphansFound)
                 ,CClientUIInterface::MSG_WARNING); 
}

void BitcoinGUI::repairWallet()
{
    int nMismatchSpent;
    int64 nBalanceInQuestion;
    int nOrphansFound;

    if(!walletModel)
        return;

    // Repair the wallet as requested by user
    walletModel->repairWallet(nMismatchSpent, nBalanceInQuestion, nOrphansFound);

    if (nMismatchSpent == 0 && nOrphansFound == 0)
       message(tr("Repair Wallet Information"),
               tr("Wallet passed integrity test!\n"
                  "Nothing found to fix.")
                ,CClientUIInterface::MSG_INFORMATION);
    else
       message(tr("Repair Wallet Information"),
               tr("Wallet failed integrity test and has been repaired!\n"
                  "Mismatched coin(s) found: %1\n"
                  "Amount affected by repair: %2\n"
                  "Orphans removed: %3\n")
                        .arg(nMismatchSpent)
                        .arg(BitcoinUnits::formatWithUnit(walletModel->getOptionsModel()->getDisplayUnit(), nBalanceInQuestion,true))
                        .arg(nOrphansFound)
                  ,CClientUIInterface::MSG_WARNING); 
}

void BitcoinGUI::backupWallet()
{
#if QT_VERSION < 0x050000
    QString saveDir = QDesktopServices::storageLocation(QDesktopServices::DocumentsLocation);
#else
    QString saveDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
#endif
    QString filename = QFileDialog::getSaveFileName(this, tr("Backup Wallet"), saveDir, tr("Wallet Data (*.dat)"));
    if(!filename.isEmpty()) {
        if(!walletModel->backupWallet(filename)) {
            QMessageBox::warning(this, tr("Backup Failed"), tr("There was an error trying to save the wallet data to the new location."));
        }
    }
}

void BitcoinGUI::dumpWallet()
{
   if(!walletModel)
      return;

   WalletModel::UnlockContext ctx(walletModel->requestUnlock());
   if(!ctx.isValid())
   {
       // Unlock wallet failed or was cancelled
       return;
   }

#if QT_VERSION < 0x050000
    QString saveDir = QDesktopServices::storageLocation(QDesktopServices::DocumentsLocation);
#else
    QString saveDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
#endif
    QString filename = QFileDialog::getSaveFileName(this, tr("Export Wallet"), saveDir, tr("Wallet Text (*.txt)"));
    if(!filename.isEmpty()) {
        if(!walletModel->dumpWallet(filename)) {
            message(tr("Export Failed"),
                         tr("There was an error trying to save the wallet's keys to your location.\n"
                            "Keys were not saved")
                      ,CClientUIInterface::MSG_ERROR);
        }
        else
            message(tr("Export Successful"),
                       tr("Keys were saved to:\n %1")
                       .arg(filename)
                      ,CClientUIInterface::MSG_INFORMATION);
    }
}

void BitcoinGUI::importWallet()
{
   if(!walletModel)
      return;

   WalletModel::UnlockContext ctx(walletModel->requestUnlock());
   if(!ctx.isValid())
   {
       // Unlock wallet failed or was cancelled
       return;
   }

#if QT_VERSION < 0x050000
    QString openDir = QDesktopServices::storageLocation(QDesktopServices::DocumentsLocation);
#else
    QString openDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
#endif
    QString filename = QFileDialog::getOpenFileName(this, tr("Import Wallet"), openDir, tr("Wallet Text (*.txt)"));
    if(!filename.isEmpty()) {
        if(!walletModel->importWallet(filename)) {
            message(tr("Import Failed"),
                         tr("There was an error trying to import the file's keys into your wallet.\n"
                            "Some or all keys were not imported from walletfile: %1")
                         .arg(filename)
                      ,CClientUIInterface::MSG_ERROR);
        }
        else
            message(tr("Import Successful"),
                       tr("Keys %1, were imported into wallet.")
                       .arg(filename)
                      ,CClientUIInterface::MSG_INFORMATION);
    }
}

void BitcoinGUI::changePassphrase()
{
    AskPassphraseDialog dlg(AskPassphraseDialog::ChangePass, this);
    dlg.setModel(walletModel);
    dlg.exec();
}

void BitcoinGUI::unlockWallet()
{
    if(!walletModel)
        return;
    // Unlock wallet when requested by wallet model
    if(walletModel->getEncryptionStatus() == WalletModel::Locked)
    {
        AskPassphraseDialog::Mode mode = sender() == unlockWalletAction ?
              AskPassphraseDialog::UnlockStaking : AskPassphraseDialog::Unlock;
        AskPassphraseDialog dlg(mode, this);
        dlg.setModel(walletModel);
        dlg.exec();
        // Only show message if unlock is sucessfull.
        if(walletModel->getEncryptionStatus() == WalletModel::Unlocked)
          message(tr("Unlock Wallet Information"),
                  tr("Wallet has been unlocked. \n"
                     "Minting has started.\n")
                  ,CClientUIInterface::MSG_INFORMATION);
    }
}

void BitcoinGUI::lockWallet()
{
    if(!walletModel)
       return;

    // Lock wallet when requested by user
    if(walletModel->getEncryptionStatus() == WalletModel::Unlocked)
         walletModel->setWalletLocked(true,"");

     message(tr("Lock Wallet Information"),
            tr("Wallet has been locked.\n"
                  "Minting has stopped.\n")
            ,CClientUIInterface::MSG_INFORMATION);
}

void BitcoinGUI::showNormalIfMinimized(bool fToggleHidden)
{
    // activateWindow() (sometimes) helps with keyboard focus on Windows
    if (isHidden())
    {
        show();
        activateWindow();
    }
    else if (isMinimized())
    {
        showNormal();
        activateWindow();
    }
    else if (GUIUtil::isObscured(this))
    {
        raise();
        activateWindow();
    }
    else if(fToggleHidden)
        hide();
}

void BitcoinGUI::toggleHidden()
{
    showNormalIfMinimized(true);
}

void BitcoinGUI::updateStakingIcon()
{
      if (!walletModel)
         return;

      labelStakingIcon->setPixmap(QIcon(":/icons/staking_off").pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));

      if (!clientModel->getNumConnections())
        labelStakingIcon->setToolTip(tr("Not staking because wallet is offline"));
      else if (clientModel->getNumConnections() < 3 )
        labelStakingIcon->setToolTip(tr("Not staking because wallet is still acquiring nodes"));
      else if (clientModel->inInitialBlockDownload() || clientModel->getNumBlocks() < clientModel->getNumBlocksOfPeers())
        labelStakingIcon->setToolTip(tr("Not staking because wallet is syncing"));
      else if(walletModel->getEncryptionStatus() == WalletModel::Locked)
        labelStakingIcon->setToolTip(tr("Not staking because wallet is locked"));
    else
      {
         uint64 nMinWeight = 0, nMaxWeight = 0, nWeight = 0;

         walletModel->getStakeWeight(nMinWeight,nMaxWeight,nWeight);
         if (!nWeight)
            labelStakingIcon->setToolTip(tr("Not staking because you don't have mature coins"));
          else
          {
            uint64 nNetworkWeight = clientModel->getPosKernalPS();
            int nEstimateTime = clientModel->getStakeTargetSpacing() * 10 * nNetworkWeight / nWeight;
            QString text;
            if (nEstimateTime < 60)
               text = tr("%n second(s)", "", nEstimateTime);
            else if (nEstimateTime < 60*60)
               text = tr("%n minute(s)", "", nEstimateTime/60);
            else if (nEstimateTime < 24*60*60)
               text = tr("%n hour(s)", "", nEstimateTime/(60*60));
            else
               text = tr("%n day(s)", "", nEstimateTime/(60*60*24));

            labelStakingIcon->setPixmap(QIcon(":/icons/staking_on").pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));
            labelStakingIcon->setToolTip(tr("Staking.\n Your weight is %1\n Network weight is %2\n You have 50\% chance of producing a stake within %3").arg(nWeight).arg(nNetworkWeight).arg(text));
          }
       }
}

void BitcoinGUI::openCommunity() {
	QDesktopServices::openUrl(QUrl("http://philosopherstones.org/channel"));
}

void BitcoinGUI::openExchanger() {
	QDesktopServices::openUrl(QUrl("https://www.cryptopia.co.nz/Exchange/?market=PHS_BTC&referrer=vladk"));
}

void BitcoinGUI::openHomepage() {
	QDesktopServices::openUrl(QUrl("http://philosopherstones.org"));
}

void BitcoinGUI::openExplorer() {
	QDesktopServices::openUrl(QUrl("http://philosopherstones.org/block"));
}
