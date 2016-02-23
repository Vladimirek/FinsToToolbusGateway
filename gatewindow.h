#ifndef GATEWINDOW_H
#define GATEWINDOW_H

#include <QMainWindow>
#include <QSerialPort>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTime>
#include <QTimer>

namespace Ui {
class GateWindow;
}

class GateWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit GateWindow(QWidget *parent = 0);
    ~GateWindow();

private slots:
    void startToolbusClient();
    void acceptConnection();
    void tcpDataRead();
    void displayError(QAbstractSocket::SocketError socketError);
    void testSerialConnection();
    void disconnected();
    void serialDataRead();
    void processRxFinsFrame(QByteArray frame);
    void processRxToolbusFrame(QByteArray frame);
    void initToolbusConnection();
    void startFinsServer();
private:
    Ui::GateWindow *ui;

    QTcpServer tcpServer;
    QTcpSocket *tcpServerConnection;

    QSerialPort serialPort;

    QByteArray tcpBuff;
    QByteArray serialBuff;

    QTime startTime;
    QTimer syncToolbusTimer;

};

#endif // GATEWINDOW_H
