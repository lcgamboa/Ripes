#include <QFuture>
#include <QtConcurrent/QtConcurrent>
#include <QtTest/QTest>

#include "io/XTcpSocket.h"

class tst_xtcpsocket : public QObject {
    Q_OBJECT

private:
    XTcpSocket client;
    XTcpSocket server;

private slots:
    void tst_pinpong();
};

void tst_xtcpsocket::tst_pinpong() {
    const char in[11] = "0123456789";
    char out[11];

    server.serverStart(7890);
    QFuture<void> future = QtConcurrent::run(&server, &XTcpSocket::serverAccept);
    client.connectToHost("127.0.0.1", 7890);
    future.waitForFinished();

    memset(out, 0, 11);
    server.write(in, 11);
    client.read(out, 11);

    QCOMPARE(in, out);

    memset(out, 0, 11);
    client.write(in, 10);
    server.read(out, 10);

    QCOMPARE(in, out);

    server.serverClose();
    server.close();
    client.close();
}

QTEST_APPLESS_MAIN(tst_xtcpsocket)
#include "tst_xtcpsocket.moc"