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
        QByteArray line = m_socket->readLine();
        parseServerResponse(line);
        qDebug() << "S: " << line;
    }
}

void SMTPClient::setAuthParams(QString &user, QString &password)
{
    m_user = user;
    m_password = password;
}

void SMTPClient::setMailParams(QByteArray &from, QList<QByteArray> &to, QByteArray &data)
{
    m_from = "<" + from + ">";

    Q_FOREACH (QByteArray recipient, to) {
        m_to.append("<" + recipient + ">");
    }

    //RFC5321 specifies to prepend a period to lines starting with a period in section 4.5.2
    if (data.startsWith('.'))
        data.prepend('.');
    data.replace("\n.", "\n..");

    m_data = data;
}

Response SMTPClient::lowLevelParser(QByteArray &line)
{
    // TODO: Add tests for this parser in the unit test suite

    Response response;
    QString responseLine = QString::fromUtf8(line);

    QRegExp rxMulti(QLatin1String("^(\\d+)-((\\d+).(\\d+).(\\d+) )?(.*)\n"));        // multiline response (aka 250-XYZ)
    QRegExp rxSingle(QLatin1String("^(\\d+) ((\\d+).(\\d+).(\\d+) )?(.*)\n"));    // single or last line response (aka 250 XYZ)

    bool multiLine = rxMulti.exactMatch(responseLine);
    bool singleLine = rxSingle.exactMatch(responseLine);

    if (multiLine) {
        response.status = rxMulti.cap(1).toInt();
        response.text = rxMulti.cap(6);
        if (!rxMulti.cap(2).isEmpty()) {
            response.enhancedStatus.sclass = rxMulti.cap(3).toInt();
            response.enhancedStatus.subject = rxMulti.cap(4).toInt();
            response.enhancedStatus.detail = rxMulti.cap(5).toInt();
        }
    } else if (singleLine) {
        response.status = rxSingle.cap(1).toInt();
        response.text = rxSingle.cap(6);
        if (!rxSingle.cap(2).isEmpty()) {
            response.enhancedStatus.sclass = rxSingle.cap(3).toInt();
            response.enhancedStatus.subject = rxSingle.cap(4).toInt();
            response.enhancedStatus.detail = rxSingle.cap(5).toInt();
        }
    } else {
        Q_ASSERT(false);
    }

    response.isMultiline = multiLine;

    return response;
}

void SMTPClient::parseServerResponse(QByteArray &line)
{
    Response response = lowLevelParser(line);

    switch (m_state) {
    case State::CONNECTING:
        if (response.status == 220) {
            m_state = State::CONNECTED;
            sendEhlo();
            m_state = State::HANDSHAKE;
        }
        break;
    case State::HANDSHAKE:
        parseCapabilities(response.text);
        if (!response.isMultiline) {
            m_state = State::AUTH;
            sendAuth(false);
        }
        break;
    case State::AUTH:
        if (response.status == 334)
            sendAuth(true);
        if (response.status == 235) {
            m_state = State::MAIL;
            sendMailFrom();
        }
        break;
    case State::MAIL:
        if (response.status == 250) {
            m_state = State::RCPT;
            if (!m_to.isEmpty()) {
                sendRcpt(m_to.front());
            }
        }
        break;
    case State::RCPT:
        if (response.status == 250) {
            m_to.removeFirst();
            if (!m_to.isEmpty()) {
                sendRcpt(m_to.front());
            } else {
                m_state = State::DATA;
                sendData(false);
            }
        } else {
            qDebug() << "Error: " << line;
        }
        break;
    case State::DATA:
        if (response.status == 354) {
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
    else if (response.toLower() == QLatin1String("enhancedstatuscodes"))
        m_options |= Option::EnhancedStatusCodesOption;
    else if (response.toLower() == QLatin1String("dsn"))
        m_options |= Option::DSNOption;
    else if (response.toLower() == QLatin1String("starttls"))
        m_options |= Option::StartTlsOption;
    else if (response.toLower() == QLatin1String("8bitmime"))
        m_options |= Option::EightBitMimeOption;
    else if (response.startsWith(QLatin1String("size"))) {
        m_options |= Option::SizeOption;
        // parse size value
    } else if (response.toLower().startsWith(QLatin1String("auth "))) {
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
        qDebug() << "C " << "AUTH PLAIN\r\n";
    }
}

void SMTPClient::sendMailFrom()
{
    // quick hack
    QByteArray array = QByteArray("MAIL FROM:").append(m_from).append("\r\n");
    m_socket->write(array);
    qDebug() << "C: " << array;
}

void SMTPClient::sendRcpt(QByteArray &recipient)
{
    QByteArray array = QByteArray("RCPT TO:").append(recipient).append("\r\n");
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
