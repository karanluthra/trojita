#include "SMTPClient.h"
#include <QDateTime>
#include <QDebug>
#include <QRegExp>
#include <QString>
#include <QSslSocket>
#include "Streams/IODeviceSocket.h"

namespace MSA {

SMTPClient::SMTPClient(QObject *parent, QString &host, quint16 port) :
    QObject(parent)
{
    m_host = host;
    m_port = port;
    m_state = MSA::State::DISCONNECTED;

    QSslSocket *sslSock = new QSslSocket();
    m_socket = new Streams::SslTlsSocket(sslSock, m_host, m_port, true);
    m_state = State::CONNECTING;

    connect(m_socket, SIGNAL(encrypted()), this, SLOT(slotEncrypted()));
    connect(m_socket, SIGNAL(readyRead()), this, SLOT(slotReadyRead()));
}

void SMTPClient::slotEncrypted()
{
    qDebug() << QDateTime::currentDateTime() << "Encryption Done";
}

void SMTPClient::slotReadyRead()
{
    while (m_socket->canReadLine()) {
        line = m_socket->readLine();
        parseServerResponse();
        qDebug() << "S: " << line;
    }
}

void SMTPClient::parseServerResponse()
{
    QRegExp rx(QLatin1String("(\\d+)-(.*)\n"));        // multiline response (aka 250-XYZ)
    QRegExp rxlast(QLatin1String("(\\d+) (.*)\n"));    // single or last line response (aka 250 XYZ)

    QString response = QString::fromUtf8(line);
    bool mid = rx.exactMatch(response);
    bool last = rxlast.exactMatch(response);
    Q_UNUSED(mid)
    Q_UNUSED(last)
    int status = rx.cap(1).toInt();

    switch (m_state) {
    case State::CONNECTING:
        if (status == 220) {
            m_state = State::CONNECTED;
            sendEhlo();
        }
        break;
    case State::HANDSHAKE:
        parseCapabilities(response);
        break;
    case State::CONNECTED:
    case State::DISCONNECTED:
    case State::MAIL:
        break;
    }
}

void SMTPClient::parseCapabilities(QString &response)
{
    if (response.toLower() == QLatin1String("pipelining"))
        m_options |= Option::PipeliningOption;
    else if (response.toLower() == QLatin1String("starttls"))
        m_options |= Option::StartTlsOption;
    else if (response.toLower() == QLatin1String("8bitmime"))
        m_options |= Option::EightBitMimeOption;
    else if (response.toLower().startsWith(QLatin1String("auth "))) {
        m_options |= Option::AuthOption;
        QStringList authModes = response.mid(5).split(QLatin1String(" "));
        foreach (const QString &authMode, authModes){
            if (authMode.toLower() == QLatin1String("plain"))
                m_authModes |= AuthMode::AuthPlain;
            if (authMode.toLower() == QLatin1String("login"))
                m_authModes |= AuthMode::AuthLogin;
        }
    }
}

void SMTPClient::sendEhlo()
{
    QByteArray array = QByteArray("EHLO localhost\r\n");
    qDebug() << "C: " << array;
    m_socket->write(array);
    m_state = State::HANDSHAKE;
}

}
