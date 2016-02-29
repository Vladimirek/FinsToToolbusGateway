#include "gatewindow.h"
#include "ui_gatewindow.h"

#include <QFileInfo>
#include <QMessageBox>
#include <QSettings>
#include <QSystemTrayIcon>

#define PROGRAM_VERSION "Ver 1.0.1     <a href='http://www.agilor.sk'>www.agilor.sk</a>"

static QByteArray syncCmd("\xAC\x01",2); //Magic begin here, will be more, stay tuned ...

QString GateWindow::getAppConfigFileName()
{
    QString appDir=QFileInfo( QCoreApplication::applicationFilePath() ).absolutePath();
    QString inipath = appDir+"/FinsToolbusGate.ini";
    return inipath;
}

GateWindow::GateWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::GateWindow)
{
    ui->setupUi(this);
    QSystemTrayIcon *trayIcon;
    trayIcon = new QSystemTrayIcon(QIcon(":AgilorIcon"));
    trayIcon->show();

    //ui->statusBar->showMessage(PROGRAM_VERSION);
    QLabel *l;
    l=new QLabel(PROGRAM_VERSION);
    l->setOpenExternalLinks( true); l->setSizePolicy(QSizePolicy::Minimum,QSizePolicy::Minimum);
    ui->statusBar->addPermanentWidget( l);
    ui->statusBar->addPermanentWidget(new QLabel(""),2);


    connect(trayIcon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)),
                this, SLOT(iconActivated(QSystemTrayIcon::ActivationReason)));

    connect(ui->actionExit, SIGNAL(triggered()),this, SLOT(close()));
    connect(ui->startButton, SIGNAL(clicked()), this, SLOT(startToolbusClient()));
    connect(ui->quitButton, SIGNAL(clicked()), this, SLOT(close()));
    connect(ui->autoStartupServer, SIGNAL(stateChanged(int)), this, SLOT(setAutoStartupServer(int)));

    connect(&tcpServer, SIGNAL(newConnection()),this, SLOT(acceptConnection()));
    connect(&serialPort, SIGNAL(readyRead()), this, SLOT(serialDataRead()));

    syncToolbusTimer.setInterval( 500);
    connect(&syncToolbusTimer, SIGNAL(timeout()), this, SLOT( initToolbusConnection()));

    //connect(ui->testButton, SIGNAL(clicked()), this, SLOT(testSerialConnection()));

    ui->comPort->setText("/dev/ttyUSB0");

    tcpServerConnection = NULL;

    //Load Config
    QSettings gateSettings( getAppConfigFileName(), QSettings::IniFormat);
    if( !gateSettings.contains("Application/RunServerOnStart")) {
        //invalid settings file - write default one
        qWarning() << "Create Default Part Types INI file";
        gateSettings.beginGroup("Application");
        gateSettings.setValue("RunServerOnStart", false);
        gateSettings.setValue("SerialCom", "/dev/ttyUSB0");
        gateSettings.endGroup();
        gateSettings.sync();
    }

    bool ros = gateSettings.value("Application/RunServerOnStart", false).toBool();
    ui->autoStartupServer->setChecked( ros);
    if( ros) {
        ui->serverStatusLabel->setText(tr("Autostart Server"));
        QTimer::singleShot( 1000, this, SLOT(startToolbusClient()));
    }
    ui->comPort->setText(gateSettings.value("Application/SerialCom", "/dev/ttyUSB0").toString());
}


GateWindow::~GateWindow()
{
    delete ui;
    ui=NULL;
}

void GateWindow::changeEvent(QEvent* e)
{
    switch (e->type())
    {
        case QEvent::LanguageChange:
            this->ui->retranslateUi(this);
            break;
        case QEvent::WindowStateChange:
            {
                if (this->windowState() & Qt::WindowMinimized)
                {
                    //trayIcon->show();
                    QTimer::singleShot(250, this, SLOT(hide()));
                }

                break;
            }
        default:
            break;
    }

    QMainWindow::changeEvent(e);
}

void GateWindow::iconActivated(QSystemTrayIcon::ActivationReason reason)
{
    qDebug() << "------" << reason;
    switch (reason) {
    case QSystemTrayIcon::Trigger:
    case QSystemTrayIcon::DoubleClick:
        this->show();
        this->raise();
        break;
    case QSystemTrayIcon::MiddleClick:
        break;
    default:
        ;
    }
}

void GateWindow::setAutoStartupServer( int state)
{
    QSettings gateSettings( getAppConfigFileName(), QSettings::IniFormat);
    gateSettings.setValue("Application/RunServerOnStart", state?true:false);
    gateSettings.setValue("Application/SerialCom",ui->comPort->text());
    gateSettings.sync();
}

void GateWindow::startToolbusClient()
{
#ifndef QT_NO_CURSOR
    QApplication::setOverrideCursor(Qt::WaitCursor);
#endif

    serialPort.setPortName( ui->comPort->text());
    while( !serialPort.open( QIODevice::ReadWrite) ) {
        QMessageBox::StandardButton ret = QMessageBox::critical(this,
                                        tr("COM Port"),
                                        tr("Unable to open Port: %1:\n%2.")
                                        .arg(serialPort.portName())
                                        .arg(serialPort.errorString()),
                                        QMessageBox::Retry
                                        | QMessageBox::Cancel);
        if (ret == QMessageBox::Cancel)
            return;
    }
    serialPort.setBaudRate(QSerialPort::Baud115200);
    serialPort.setDataBits(QSerialPort::Data8);
    serialPort.setStopBits(QSerialPort::OneStop);
    serialPort.setFlowControl(QSerialPort::NoFlowControl);
    serialPort.setDataTerminalReady(true);
    serialPort.setRequestToSend(true);
    syncToolbusTimer.start();
    initToolbusConnection();
}

void GateWindow::startFinsServer()
{
    while (!tcpServer.isListening() && !tcpServer.listen(QHostAddress::LocalHost, 9600)) {
        QMessageBox::StandardButton ret = QMessageBox::critical(this,
                                        tr("FINS Server"),
                                        tr("Unable to start the server: %1.")
                                        .arg(tcpServer.errorString()),
                                        QMessageBox::Retry
                                        | QMessageBox::Cancel);
        if (ret == QMessageBox::Cancel)
            return;
    }
    ui->startButton->setEnabled(false);
    ui->serverStatusLabel->setText(tr("Listening"));
#ifndef QT_NO_CURSOR
        QApplication::restoreOverrideCursor();
#endif

}

void GateWindow::initToolbusConnection()
{
    ui->serverStatusLabel->setText(tr("Tollbus SYNC in progress"));
    serialPort.write( syncCmd);
}

void GateWindow::testSerialConnection()
{
    //serialPort.write(QByteArray("\xAB\x00\x14\x80\x00\x03\x00\x00\x00\x00\x00\x00\x0D\x01\x01\x82\x00\x01\x00\x00\x03\x01\xD7",23));
    serialPort.write(QByteArray("\xab\x00\x0e\x80\x00\x02\x00\x00\x00\x00\x00\x00\x00\x05\x01\x01\x41",17)); //identify controller
    //serialPort.write(QByteArray("\xAC\x01",2));
}

void GateWindow::acceptConnection()
{
    qDebug() << "acceptConnection" << "Remain:" << tcpServer.maxPendingConnections();
    tcpServerConnection = tcpServer.nextPendingConnection();
    connect(tcpServerConnection, SIGNAL(readyRead()),
            this, SLOT(tcpDataRead()));
    connect(tcpServerConnection, SIGNAL(error(QAbstractSocket::SocketError)),
            this, SLOT(displayError(QAbstractSocket::SocketError)));
    connect(tcpServerConnection, SIGNAL(disconnected()),
            this, SLOT(disconnected()));

    ui->serverStatusLabel->setText(tr("Accepted connection"));
    //tcpServer.close();
}

void GateWindow::disconnected()
{
    qDebug() << "Disconnected";
    if( tcpServerConnection) {
        tcpServerConnection->disconnect();
        tcpServerConnection=NULL;
        if( ui) ui->serverStatusLabel->setText(tr("Disconnected"));
    }
}

void GateWindow::tcpDataRead()
{
    if( !tcpServerConnection) return;

    qDebug() << "finsServerDataRead";

    tcpBuff.append( tcpServerConnection->readAll());

    while( tcpBuff.length()) {
        //Check frame strcture (FINS and lengths)
        int frameBeginIdx = tcpBuff.indexOf("FINS");
        if( frameBeginIdx<0) {
            qWarning() << "Discard whole TCP data" << tcpBuff.toHex();
            tcpBuff.clear();
            return;
        }
        if( frameBeginIdx>0) {
            qWarning() << "Discard begin of TCP data" << tcpBuff.toHex();
            tcpBuff.remove(0,frameBeginIdx);
        }
        if( tcpBuff.length() < 8) return;

        quint16 fLength  = (unsigned char)(tcpBuff.at(4))*(256*256*256)
                         + (unsigned char)(tcpBuff.at(5))*(256*2256)
                         + (unsigned char)(tcpBuff.at(6))*(256)
                         + (unsigned char)(tcpBuff.at(7));

        if( tcpBuff.length() >= fLength+8) {
            //whole frame recieved
            QByteArray frame = tcpBuff.left(fLength+8);
            tcpBuff.remove(0,fLength+8);

            processRxFinsFrame( frame);
        } else {
            return;
        }
    }

}

void GateWindow::processRxFinsFrame( QByteArray frame)
{
    qDebug() << "Process FINS Frame";
    startTime.start();

    if( frame.length() >= 34) {
        QByteArray serialDataSend;
        QByteArray msgPayload = frame.mid(16);
        int serialMgsLen = msgPayload.length()+2;

        serialDataSend.append( 0xAB);
        serialDataSend.append( (serialMgsLen >> 8) & 0xff);
        serialDataSend.append( serialMgsLen & 0xff);
        serialDataSend.append( msgPayload);

        quint16 serialMgsSum=0;
        for( int i=0; i<serialDataSend.length();i++) {
            serialMgsSum += (quint8)serialDataSend.at(i);
        }
        serialDataSend.append( (serialMgsSum >> 8) & 0xff);
        serialDataSend.append( serialMgsSum & 0xff);

        //qDebug() << "SerialOUT:" << serialDataSend.toHex();
        qDebug() << " Write Toolbus Frame";
        serialPort.write( serialDataSend);
    } else if (frame.length() == 20) {
        //special FINS command
        //Be warned ! We do a bit off dirty Magic here !!!
        QByteArray tcpDataSend( "\x46\x49\x4e\x53\x00\x00\x00\x10\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\xf0\x00\x00\x00\x0a",24);
        // End of magis section ... (see Section 7-4 FINS/TCP Method)
        if( tcpServerConnection) tcpServerConnection->write( tcpDataSend);
    } else {
        qDebug() << "FINS Malformed pactet?" << frame.toHex();
    }
}

void GateWindow::serialDataRead()
{
    //qDebug() << "ToolbusDataRead";

    serialBuff.append( serialPort.readAll());

    while( serialBuff.length()) {
        //check sync condition
        if( syncToolbusTimer.isActive() || serialBuff.startsWith(0xAC) ) {
            int syncIdx = serialBuff.indexOf( syncCmd);
            if( syncIdx>=0) {
                serialBuff.remove(0,syncIdx+2);
                ui->serverStatusLabel->setText(tr("Toolbus in Sync"));
                syncToolbusTimer.stop();
                if (!tcpServer.isListening()) startFinsServer();
                continue;
            } else {
                return;
            }
        }

        //Check frame structure (Header, length)
        int frameBeginIdx = serialBuff.indexOf( 0xAB);
        if( frameBeginIdx<0) {
            qWarning() << "Discard whole Toolbus data" << serialBuff.toHex();
            serialBuff.clear();
            return;
        }
        if( frameBeginIdx>0) {
            qWarning() << "Discard begin of Toolbus data" << serialBuff.toHex();
            serialBuff.remove(0,frameBeginIdx);
        }

        if(serialBuff.length() < 5) return;

        quint16 fLength  = (quint8)(serialBuff.at(1))*(256)
                         + (quint8)(serialBuff.at(2));

        if( serialBuff.length() >= fLength+3) {
            //whole frame recieved
            QByteArray frame = serialBuff.left(fLength+3);
            serialBuff.remove(0,fLength+3);

            //check frame checksum
            quint16 f1Sum=0;
            for( int i=0; i<frame.length()-2;i++) {
                f1Sum += (quint8)frame.at(i);
            }
            quint16 f2Sum  = (quint8)(frame.at( frame.length()-2))*(256)
                          + (quint8)(frame.at( frame.length()-1));

            if( f1Sum == f2Sum) {
                processRxToolbusFrame( frame);
            } else {
                qDebug() << "Malformed Toobus Frame:" << frame.toHex();
            }
        } else {
            return;
        }
    }
}

void GateWindow::processRxToolbusFrame( QByteArray frame)
{
    qDebug() << "Process Toolbus Frame";
    //qDebug() << "SerialIn:" << frame.toHex();

    QByteArray toolbusData=frame.mid(3,frame.length()-5); //remove head, tail

    QByteArray tcpDataSend;
    tcpDataSend.append( "FINS");

    tcpDataSend.append((char)0x00); //length MSB4
    tcpDataSend.append((char)0x00); //length MSB3
    tcpDataSend.append(((toolbusData.length()+8)>>8) & 0xff);
    tcpDataSend.append(((toolbusData.length()+8)   ) & 0xff);

    tcpDataSend.append((char)0x00); //Some Magic again ...
    tcpDataSend.append((char)0x00);
    tcpDataSend.append((char)0x00);
    tcpDataSend.append((char)0x02);

    tcpDataSend.append((char)0x00); //Error
    tcpDataSend.append((char)0x00);
    tcpDataSend.append((char)0x00);
    tcpDataSend.append((char)0x00);

    tcpDataSend.append( toolbusData); //serial response minus checksum

    if( tcpServerConnection) {
        qDebug() << " Write FINS Response";
        ui->serverStatusLabel->setText(tr("Frame Processed [%1ms]").arg( startTime.elapsed()));
        tcpServerConnection->write( tcpDataSend);
    }
}

void GateWindow::displayError(QAbstractSocket::SocketError socketError)
{
    if (socketError == QTcpSocket::RemoteHostClosedError)
        return;

    QMessageBox::information(this, tr("Network error"),
                             tr("The following error occurred: %1\n.")
                             .arg(tcpServer.errorString()));

    tcpServer.close();
    ui->serverStatusLabel->setText(tr("Server ready"));
    ui->startButton->setEnabled(true);
#ifndef QT_NO_CURSOR
    QApplication::restoreOverrideCursor();
#endif
}
