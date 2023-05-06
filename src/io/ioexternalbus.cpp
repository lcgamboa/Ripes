#include "ioexternalbus.h"
#include "ui_ioexternalbus.h"

#include <QPainter>
#include <QPen>

#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QMutexLocker>

#include "STLExtras.h"
#include "ioregistry.h"

#ifdef _MSC_VER
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

namespace Ripes {

IOExternalBus::IOExternalBus(QWidget *parent)
    : IOBase(IOType::EXTERNALBUS, parent), m_ui(new Ui::IOExternalBus),
      tcpSocket(new QTcpSocket()) {
  m_ByteSize = 0x46;
  m_ui->setupUi(this);
  connect(m_ui->connectButton, &QPushButton::clicked, this,
          &IOExternalBus::connectButtonTriggered);

  connect(this, SIGNAL(SigWrite(const QByteArray)), this,
          SLOT(SlotWrite(const QByteArray)), Qt::QueuedConnection);
  connect(this, SIGNAL(SigClose(void)), this, SLOT(SlotClose(void)),
          Qt::QueuedConnection);
  connect(tcpSocket.get(), SIGNAL(readyRead(void)), this, SLOT(SlotRead(void)),
          Qt::QueuedConnection);
}

IOExternalBus::~IOExternalBus() {
  unregister();
  delete m_ui;
}

unsigned IOExternalBus::byteSize() const { return m_ByteSize; }

QString IOExternalBus::description() const {
  return "An external bus is a memory mapped bus handled through network "
         "transactions. The peripheral connects to an "
         "IP address denoting a peripheral server - for more details, refer to "
         "the Ripes wiki.";
}

void IOExternalBus::SlotWrite(const QByteArray Data) {

  VBUS::CmdHeader *cmd_header = (VBUS::CmdHeader *)Data.data();

  qDebug() << "threadID WR=" << QThread::currentThread()
           << " simtime= " << ntohll(cmd_header->time);

  if ((tcpSocket->write(Data)) < 0) {
    disconnectOnError();
  }
}

void IOExternalBus::SlotRead(void) {

  ReceivedData.append(tcpSocket->readAll());

  VBUS::CmdHeader *cmd_header = (VBUS::CmdHeader *)ReceivedData.data();
  qDebug() << "threadID RD=" << QThread::currentThread()
           << " simtime= " << ntohll(cmd_header->time);

  emit SigReadEnd();
}

void IOExternalBus::SlotClose(void) {

  QMessageBox::information(nullptr, tr("Ripes VBus"), tcpSocket->errorString());

  qDebug() << tcpSocket->errorString();

  tcpSocket->close();
  tcpSocket->abort();
}

VInt IOExternalBus::ioRead(AInt offset, unsigned size) {
  uint32_t rvalue = 0;

  if (tcpSocket->isOpen()) {
    uint64_t simtime = std::chrono::time_point_cast<std::chrono::nanoseconds>(
                           std::chrono::system_clock::now())
                           .time_since_epoch()
                           .count();
    QMutexLocker locker(&skt_use);
    ReceivedData.clear();
    uint32_t payload = htonl(offset);
    QByteArray dp = QByteArray(reinterpret_cast<const char *>(&payload), 4);
    if (send_cmd(VBUS::VB_PREAD, dp.size(), dp, simtime) < 0) {
      return 0;
    }

    VBUS::CmdHeader cmd_header;

    if (recv_cmd(cmd_header) < 0) {
      return 0;
    }

    if (cmd_header.payload_size) {
      dp = QByteArray(8, 0);
      if (recv_payload(dp, dp.size()) < 0) {
        return 0;
      }
      uint32_t *payload = reinterpret_cast<uint32_t *>(dp.data());

      for (uint32_t i = 0; i < 2; i++) {
        payload[i] = ntohl(payload[i]);
      }
      rvalue = payload[1];
    } else {
      disconnectOnError("read error");
    }
  }
  return rvalue;
}

void IOExternalBus::ioWrite(AInt offset, VInt value, unsigned size) {
  if (tcpSocket->isOpen()) {
    uint64_t simtime = std::chrono::time_point_cast<std::chrono::nanoseconds>(
                           std::chrono::system_clock::now())
                           .time_since_epoch()
                           .count();
    QMutexLocker locker(&skt_use);
    ReceivedData.clear();
    uint32_t payload[2];
    payload[0] = htonl(offset);
    payload[1] = htonl(value);
    QByteArray dp = QByteArray(reinterpret_cast<char *>(payload), 8);
    if (send_cmd(VBUS::VB_PWRITE, dp.size(), dp, simtime) < 0) {
      return;
    }

    VBUS::CmdHeader cmd_header;

    if (recv_cmd(cmd_header) < 0) {
      return;
    }

    if (cmd_header.msg_type != VBUS::VB_PWRITE) {
      disconnectOnError("write error");
    }
  }
}

void IOExternalBus::connectButtonTriggered() {
  if (!m_Connected) {
    tcpSocket->abort();
    QMutexLocker locker(&skt_use);

    tcpSocket->connectToHost(m_ui->address->text(), m_ui->port->value());
    if (tcpSocket->waitForConnected(1000)) {
      uint64_t simtime = std::chrono::time_point_cast<std::chrono::nanoseconds>(
                             std::chrono::system_clock::now())
                             .time_since_epoch()
                             .count();
      if (send_cmd(VBUS::VB_PINFO, 0, {}, simtime) < 0) {
        return;
      }
      VBUS::CmdHeader cmd_header;

      if (recv_cmd(cmd_header) < 0) {
        return;
      }
      if (cmd_header.payload_size) {
        QByteArray buff(cmd_header.payload_size + 1, 0);
        if (recv_payload(buff, cmd_header.payload_size) < 0) {
          disconnectOnError();
          return;
        }
        QJsonParseError error;
        QJsonDocument desc = QJsonDocument::fromJson(buff.data(), &error);
        if (error.error == QJsonParseError::NoError) {
          const unsigned int addrw =
              desc.object().value(QString("address width")).toInt();
          QJsonObject osymbols =
              desc.object().value(QString("symbols")).toObject();

          m_regDescs.clear();

          for (QJsonObject::iterator i = osymbols.begin(); i != osymbols.end();
               i++) {
            m_regDescs.push_back(RegDesc{i.key(), RegDesc::RW::RW, addrw * 8,
                                         static_cast<AInt>(i.value().toInt()),
                                         true});
          }
          m_ByteSize = addrw * osymbols.count();
          updateConnectionStatus(
              true, desc.object().value(QString("name")).toString());

        } else {
          QMessageBox::information(nullptr, tr("Ripes VBus"),
                                   QString("json: ") + error.errorString());
          updateConnectionStatus(false);
          send_cmd(VBUS::VB_QUIT, 0, {}, simtime);
          tcpSocket->abort();
        }
      }
    } else {
      disconnectOnError();
    }
  } else { // disconnect
    updateConnectionStatus(false);
    send_cmd(VBUS::VB_QUIT, 0, {}, 0);
    tcpSocket->abort();
    m_regDescs.clear();
  }
  emit regMapChanged();
  emit sizeChanged();
}

void IOExternalBus::updateConnectionStatus(bool connected, QString Server) {
  m_Connected = connected;
  if (m_Connected) {
    m_ui->connectButton->setText("Disconnect");
    m_ui->status->setText("Connected");
    m_ui->server->setText(Server);
  } else {
    m_ui->connectButton->setText("Connect");
    m_ui->status->setText("Disconnected");
    m_ui->server->setText(Server);
  }
}

int32_t IOExternalBus::send_cmd(const uint32_t cmd, const uint32_t payload_size,
                                const QByteArray &payload,
                                const uint64_t time) {
  int32_t ret = 0;
  VBUS::CmdHeader cmd_header;

  cmd_header.msg_type = htonl(cmd);
  cmd_header.payload_size = htonl(payload_size);
  cmd_header.time = htonll(time);

  QByteArray dp = QByteArray(reinterpret_cast<const char *>(&cmd_header),
                             sizeof(VBUS::CmdHeader));

  if (payload_size) {
    dp.append(payload);
  }

  qDebug() << "threadID=" << QThread::currentThread() << " simtime= " << time;

  emit SigWrite(dp);

  // QCoreApplication::processEvents();

  return ret;
}

int32_t IOExternalBus::recv_cmd(VBUS::CmdHeader &cmd_header) {

  int ret = 0;
  int size = sizeof(VBUS::CmdHeader);

  QTimer timer;
  timer.setSingleShot(true);
  QEventLoop loop;

  connect(this, SIGNAL(SigReadEnd()), &loop, SLOT(quit()));
  connect(&timer, SIGNAL(timeout()), &loop, SLOT(quit()));

  while (ret < size) {
    timer.start(10000);
    loop.exec(QEventLoop::ExcludeUserInputEvents |
              QEventLoop::ExcludeSocketNotifiers);

    if (timer.isActive()) {
      ret = ReceivedData.size();
    } else {
      break;
    }
  }

  if (ret < size) {
    disconnectOnError();
    ret = -1;
  } else {

    char *cmdp = reinterpret_cast<char *>(&cmd_header);

    memcpy(cmdp, ReceivedData.data(), size);

    ReceivedData.remove(0, size);
  }

  cmd_header.msg_type = ntohl(cmd_header.msg_type);
  cmd_header.payload_size = ntohl(cmd_header.payload_size);
  cmd_header.time = ntohll(cmd_header.time);

  return ret;
}

int32_t IOExternalBus::recv_payload(QByteArray &buff,
                                    const uint32_t payload_size) {

  int ret = -1;

  if (ReceivedData.size() < payload_size) {
    disconnectOnError();
  } else {

    buff = ReceivedData;
    ReceivedData.remove(0, payload_size);
    ret = buff.size();
  }

  return ret;
}

void IOExternalBus::disconnectOnError(const QString msg) {

  emit SigClose();

  updateConnectionStatus(false);
  m_regDescs.clear();

  emit regMapChanged();
  emit sizeChanged();
}

} // namespace Ripes
