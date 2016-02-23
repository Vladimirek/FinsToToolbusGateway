#include "gatewindow.h"
#include "ui_gatewindow.h"

#include <QMessageBox>


static const int TotalBytes = 50 * 1024 * 1024;
static const int PayloadSize = 64 * 1024; // 64 KB

GateWindow::GateWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::GateWindow)
{
    ui->setupUi(this);

    connect(ui->startButton, SIGNAL(clicked()), this, SLOT(startFinsServer()));
    connect(ui->quitButton, SIGNAL(clicked()), this, SLOT(close()));

    connect(&tcpServer, SIGNAL(newConnection()),this, SLOT(acceptConnection()));
    connect(&serialPort, SIGNAL(readyRead()), this, SLOT(serialDataRead()));

    connect(ui->testButton, SIGNAL(clicked()), this, SLOT(testSerialConnection()));

    ui->comPort->setText("/dev/ttyUSB0");

    tcpServerConnection = NULL;
}



GateWindow::~GateWindow()
{
    delete ui;
}

void GateWindow::startFinsServer()
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

void GateWindow::testSerialConnection()
{
    serialPort.write(QByteArray("\xAB\x00\x14\x80\x00\x03\x00\x00\x00\x00\x00\x00\x0D\x01\x01\x82\x00\x01\x00\x00\x03\x01\xD7",23));
    serialPort.waitForBytesWritten( 500);
    serialPort.waitForReadyRead(500);
    QByteArray buff;
    buff = serialPort.readAll();
    while( serialPort.waitForReadyRead(50)) {
        buff += serialPort.readAll();
    }
    qDebug() << buff;
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
        ui->serverStatusLabel->setText(tr("Disconnected"));
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
        //Be warned ! We do a bit off Magic here !!!
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
