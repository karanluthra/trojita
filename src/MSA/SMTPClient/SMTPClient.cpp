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

void SMTPClient::setAuthParams(QString &user, QString &password)
{
    m_user = user;
    m_password = password;
}

void SMTPClient::setMailParams(QByteArray &from, QByteArray &to, QByteArray &data)
{
    m_from = "<" + from + ">";
    m_to = "<" + to + ">";

    //RFC5321 specifies to prepend a period to lines starting with a period in section 4.5.2
    if (data.startsWith('.'))
        data.prepend('.');
    data.replace("\n.", "\n..");

    m_data = data;
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
    int status = mid? rx.cap(1).toInt(): rxlast.cap(1).toInt();

    switch (m_state) {
    case State::CONNECTING:
        if (status == 220) {
            m_state = State::CONNECTED;
            sendEhlo();
        }
        break;
    case State::HANDSHAKE:
        parseCapabilities(response);
        if (last) {
            m_state = State::AUTH;
            sendAuth(false);
        }
        break;
    case State::AUTH:
        if (status == 334)
            sendAuth(true);
        if (status == 235) {
            m_state = State::MAIL;
            sendMailFrom();
        }
        break;
    case State::MAIL:
        if (status == 250) {
            m_state = State::RCPT;
            sendRcpt();
        }
        break;
    case State::RCPT:
        if (status == 250) {
            m_state = State::DATA;
            sendData(false);
        }
        break;
    case State::DATA:
        qDebug() << "Status: " << status;
        if (status == 354) {
            sendData(true);
        }
        break;
    case State::CONNECTED:
    case State::DISCONNECTED:
        break;
    }
}

void SMTPClient::parseCapabilities(QString &response)
{
    if (response.toLower() == QLatin1String("pipelining"))
        m_options |= Option::PipeliningOption;
    else if (response.startsWith(QLatin1String("size"))) {
        m_options |= Option::SizeOption;
        // parse size value
    } else if (response.toLower() == QLatin1String("dsn"))
        m_options |= Option::DSNOption;
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
    m_socket->write(array);
    qDebug() << "C: " << array;
    m_state = State::HANDSHAKE;
}

void SMTPClient::sendAuth(bool ready)
{
    if (ready) {
        QByteArray ba;
        ba.append('\0');
        ba.append(m_user.toUtf8());
        ba.append('\0');
        ba.append(m_password.toUtf8());
        QByteArray encoded = ba.toBase64();
        qDebug() << "C " << encoded;
        m_socket->write(encoded);
        m_socket->write("\r\n");
    } else {
        m_socket->write("AUTH PLAIN\r\n");
    }
}

void SMTPClient::sendMailFrom()
{
    // quick hack
    QByteArray array = QByteArray("MAIL FROM:").append(m_from).append("\r\n");
    m_socket->write(array);
    qDebug() << "C: " << array;
}

void SMTPClient::sendRcpt()
{
    QByteArray array = QByteArray("RCPT TO:").append(m_to).append("\r\n");
    m_socket->write(array);
    qDebug() << "C: " << array;
}

void SMTPClient::sendData(bool ready)
{
    if (ready) {
        QByteArray array = QByteArray(m_data).append("\r\n.\r\n");
        m_socket->write(array);
        qDebug() << "C: " << array;
    } else {
        QByteArray array = QByteArray("DATA\r\n");
        m_socket->write(array);
        qDebug() << "C: " << array;
    }
}

}
