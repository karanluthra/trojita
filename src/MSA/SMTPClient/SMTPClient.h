#ifndef SMTPCLIENT_H
#define SMTPCLIENT_H

#include <QObject>
#include <QString>
#include <QLinkedList>
#include <Streams/SocketFactory.h>
#include <MSA/SMTPClient/Command.h>

namespace Streams {
class SslTlsSocket;
}

namespace MSA {

enum class State {
    DISCONNECTED,
    CONNECTING,
    READY,
    PIPELINING,
};

enum class Command {
    INIT,
    HELO,
    EHLO,
    AUTH,
    MAIL,
    RCPT,
    DATA,
    QUIT,
};

struct Response
{
    int status;
    struct EnhancedStatus
    {
        int sclass;
        int subject;
        int detail;
    } enhancedStatus;
    QString text;
    bool isMultiline;
};

typedef int CommandHandle;

class SMTPClient : public QObject
{
    Q_OBJECT
public:
    explicit SMTPClient(QObject *parent, std::unique_ptr<Streams::SocketFactory> factory);
    enum Extension {
        StartTls = 1 << 1,
        Size = 1 << 2,
        Pipelining = 1 << 3,
        EightBitMime = 1 << 4,
        DSN = 1 << 5,
        Auth = 1 << 6,
        EnhancedStatusCodes = 1 << 7,
    };
    enum AuthMode {
        Plain = 1 << 1,
        Login = 1 << 2,
    };
    Q_DECLARE_FLAGS (Extensions, Extension)
    Q_DECLARE_FLAGS (AuthModes, AuthMode)

    void doConnect();
    void closeConnection();
    void setAuthParams(QString &user, QString &password);
    void setMailParams(QByteArray &from, QList<QByteArray> &to, QByteArray &data);
    void exec();
signals:
    void submitted();

private slots:
    void slotReadyRead();
    void handleResponse(Response &response);
    void parseCapabilities(QString &response);
    Response parseLine(QByteArray &line);
    void generateFakeResponse();
    void checkMatchFakeResponse(Response &actualResponse);

private:
    void nextCommand(Response &response, Command &lastCommand);

    // @karan: commands go here for now
    CommandHandle ehlo(QByteArray &localname);
    CommandHandle auth(QByteArray method);
    CommandHandle authPlainStageTwo();

    int generateTag();
    CommandHandle queueCommand(Commands::Command &command);
    void executeCommands();

    /** @short Queue storing commands that are about to be executed */
    QLinkedList<Commands::Command> cmdQueue;
    QLinkedList<Response> responseQueue;
    QLinkedList<Response> pendingFakeResponseQueue;
    int m_commandTag;
    QString m_host;
    quint16 m_port;
    QString m_user;
    QString m_password;
    QByteArray m_from;
    QList<QByteArray> m_to;
    QByteArray m_data;
    Streams::Socket *m_socket;
    State m_state;
    Command m_command;
    QByteArray line;
    Response m_response;
    std::unique_ptr<Streams::SocketFactory> m_factory;

    Extensions m_extensions;
    AuthModes m_authModes;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(SMTPClient::Extensions)
Q_DECLARE_OPERATORS_FOR_FLAGS(SMTPClient::AuthModes)

}
#endif // SMTPCLIENT_H
