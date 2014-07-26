#include "SMTPClient.h"
#include <QDateTime>
#include <QDebug>
#include <QLinkedList>
#include <QRegExp>
#include <QString>
#include <QSslSocket>
#include "Streams/IODeviceSocket.h"
#include "Streams/SocketFactory.h"

namespace MSA {

SMTPClient::SMTPClient(QObject *parent, std::unique_ptr<Streams::SocketFactory> factory) :
    QObject(parent), m_commandTag(0), m_state(State::DISCONNECTED), m_command(Command::INIT), m_factory(std::move(factory))
{
}

/** @short Fire up the connection */
void SMTPClient::doConnect()
{
    m_socket = m_factory->create();

    m_state = State::CONNECTING;
    connect(m_socket, SIGNAL(readyRead()), this, SLOT(slotReadyRead()));
    // TODO: handle other (useful) signals emitted by the socket
}

void SMTPClient::closeConnection()
{
    m_socket->close();
    m_socket->deleteLater();
    m_state = State::DISCONNECTED;
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

void SMTPClient::slotReadyRead()
{
    while (m_socket->canReadLine()) {
        QByteArray line = m_socket->readLine();
        qDebug() << "S: " << line.trimmed();
        Response response = parseLine(line);
        handleResponse(response);
    }
}

Response SMTPClient::parseLine(QByteArray &line)
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

void SMTPClient::handleResponse(Response &response)
{
    if (m_state == State::CONNECTING) {
        nextCommand(response, m_command);
    } else {
        Q_ASSERT(false);
    }
}

/* Determine the next SMTP command the client needs to send in order
 * to proceed with submitting a message for submission to the SMTP server.
 * The next command is a function of two variables:
 * 1. The previous command,
 * 2. The response code (error/success/failure) */
void SMTPClient::nextCommand(Response &response, Command &lastCommand)
{
    Command nextCommand = lastCommand;

    switch (lastCommand) {
    case Command::INIT:
        switch (response.status) {
        case 220: {
            m_state = State::READY;
            nextCommand = Command::EHLO;

            // lets inject commands here for testing
            QByteArray localname = QByteArray("localhost");
            ehlo(localname);
            break;
        }
        case 554:
            // TODO: error state
            Q_ASSERT(false);
            break;
        default:
            // TODO:
            Q_ASSERT(false);
        }
        break;
    case Command::EHLO:
        switch (response.status) {
        case 250:
            parseCapabilities(response.text);
            if (!response.isMultiline) {
                // TODO: if (requires auth && "AUTH" capability advertised)
                if (m_extensions.testFlag(Extension::Auth)) {
                    nextCommand = Command::AUTH;
                    if (m_authModes.testFlag(AuthMode::Plain)) {
                        auth(QByteArray("PLAIN"));
                    }
                }
            }
            break;
        case 550:
        case 502:
            // EHLO not supported, do HELO instead
            nextCommand = Command::HELO;
            break;
        default:
            // TODO:
            Q_ASSERT(false);
        }
        break;
    case Command::HELO:
        // TODO: look into HELO
        Q_ASSERT(false);
        break;
    case Command::AUTH:
        switch (response.status) {
        case 334:
            authPlainStageTwo();
            executeCommands();
            break;
        case 235:
            // Auth Successful
            qDebug() << "Auth Success";
            Q_ASSERT(false);
            nextCommand = Command::MAIL;
            break;
        case 454:
        case 500:
        case 534:
        case 535:
        case 538:
            // TODO: error state
            Q_ASSERT(false);
            break;
        default:
            Q_ASSERT(false);
            // TODO:
        }
        break;
    case Command::MAIL:
        switch (response.status) {
        case 250:
            nextCommand = Command::RCPT;
            break;
        case 552:
        case 451:
        case 452:
        case 550:
        case 553:
        case 503:
        case 455:
        case 555:
            Q_ASSERT(false);
            // TODO: error states
            break;
        default:
            Q_ASSERT(false);
            // TODO:
        }
        break;
    case Command::RCPT:
        switch (response.status) {
        case 250:
        case 251:
            if (m_to.isEmpty()) {
                nextCommand = Command::DATA;
            }
            break;
        case 550:
        case 551:
        case 552:
        case 553:
        case 450:
        case 451:
        case 452:
        case 503:
        case 455:
        case 555:
            // TODO: error states
            Q_ASSERT(false);
            break;
        default:
            // TODO:
            Q_ASSERT(false);
        }
        break;
    case Command::DATA:
        switch (response.status) {
        case 250:
            nextCommand = Command::QUIT;
            break;
        case 354:
            // proceed stage 2
            Q_ASSERT(false);
            break;
        case 552:
        case 451:
        case 452:
        case 450:
        case 550:
        case 503:
        case 554:
            // TODO: error state
            Q_ASSERT(false);
            break;
        default:
            // TODO:
            Q_ASSERT(false);
        }
        break;
    case Command::QUIT:
        switch (response.status) {
        case 220:
            break;
        default:
            // TODO:
            Q_ASSERT(false);
        }
        break;
    default:
        // TODO:
        Q_ASSERT(false);
    }
    if (nextCommand != lastCommand) {
        m_command = nextCommand;
        executeCommands();
    }
}

void SMTPClient::executeCommands()
{
    Q_ASSERT(!cmdQueue.isEmpty());
    Commands::Command &cmd = cmdQueue.first();

    QByteArray buf;

    while (1) {
        Commands::PartOfCommand part = cmd.cmds[ cmd.currentPart ];
        if (part.kind == Commands::TokenType::ATOM) {
            buf.append(part.text);
        }

        if (cmd.currentPart == cmd.cmds.size() - 1) {
            // last part
            buf.append("\r\n");
            qDebug() << "C: " << buf.trimmed();
            m_socket->write(buf);
            cmdQueue.pop_front();
            return;
        } else {
            buf.append(' ');
            cmd.currentPart++;
        }
    }
}

CommandHandle SMTPClient::ehlo(QByteArray &localname)
{
    return queueCommand(Commands::Command("EHLO") << localname);
}

CommandHandle SMTPClient::auth(QByteArray method)
{
    return queueCommand(Commands::Command("AUTH") << method);
}

CommandHandle SMTPClient::authPlainStageTwo()
{
    QByteArray ba;
    ba.append('\0');
    ba.append(m_user.toUtf8());
    ba.append('\0');
    ba.append(m_password.toUtf8());
    QByteArray encoded = ba.toBase64();
    return queueCommand(Commands::Command() << encoded);
}

int SMTPClient::generateTag()
{
    return ++m_commandTag;
}

CommandHandle SMTPClient::queueCommand(Commands::Command &command)
{
    int tag = generateTag();
    command.addTag(tag);
    cmdQueue.append(command);
    return tag;
}

void SMTPClient::parseCapabilities(QString &response)
{
    if (response.toLower() == QLatin1String("pipelining"))
        m_extensions |= Extension::Pipelining;
    else if (response.toLower() == QLatin1String("enhancedstatuscodes"))
        m_extensions |= Extension::EnhancedStatusCodes;
    else if (response.toLower() == QLatin1String("dsn"))
        m_extensions |= Extension::DSN;
    else if (response.toLower() == QLatin1String("starttls"))
        m_extensions |= Extension::StartTls;
    else if (response.toLower() == QLatin1String("8bitmime"))
        m_extensions |= Extension::EightBitMime;
    else if (response.startsWith(QLatin1String("size"))) {
        m_extensions |= Extension::Size;
        // parse size value
    } else if (response.toLower().startsWith(QLatin1String("auth "))) {
        m_extensions |= Extension::Auth;
        QStringList authModes = response.mid(5).split(QLatin1String(" "));
        foreach (const QString &authMode, authModes) {
            if (authMode.toLower() == QLatin1String("plain"))
                m_authModes |= AuthMode::Plain;
            if (authMode.toLower() == QLatin1String("login"))
                m_authModes |= AuthMode::Login;
        }
    }
}

/* Not needed any more, except for reference

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

void SMTPClient::sendQuit()
{
    QByteArray array = QByteArray("QUIT\r\n");
    m_socket->write(array);
    qDebug() << "C: " << array;
}

*/

}
