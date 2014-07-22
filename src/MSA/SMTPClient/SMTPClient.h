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
    CONNECTED,
    HANDSHAKE,
    AUTH,
    MAIL,
    RCPT,
    DATA,
    CLOSING,
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
        StartTls,
        Size,
        Pipelining,
        EightBitMime,
        DSN,
        Auth,
        EnhancedStatusCodes,
    };
    enum AuthMode {
        Plain,
        Login,
    };
    Q_DECLARE_FLAGS (Extensions, Extension)
    Q_DECLARE_FLAGS (AuthModes, AuthMode)

    void doConnect();
    void closeConnection();
    void setAuthParams(QString &user, QString &password);
    void setMailParams(QByteArray &from, QList<QByteArray> &to, QByteArray &data);
signals:
    void submitted();
private slots:
    void slotReadyRead();
    void handleResponse(Response &response);
    //void parseServerResponse(QByteArray &line);
    void parseCapabilities(QString &response);
    Response parseLine(QByteArray &line);
private:
    void sendEhlo();
    void sendAuth(bool ready);
    void sendMailFrom();
    void sendRcpt(QByteArray &recipient);
    void sendData(bool ready);
    void sendQuit();

    // @karan: commands go here for now
    CommandHandle ehlo(QByteArray &localname);

    int generateTag();
    CommandHandle queueCommand(Commands::Command &command);
    void executeCommands();

    /** @short Queue storing commands that are about to be executed */
    QLinkedList<Commands::Command> cmdQueue;
    int m_commandTag;
    QString m_host;
    quint16 m_port;
    QString m_user;
    QString m_password;
    QByteArray m_from;
    QList<QByteArray> m_to;
    QByteArray m_data;
    Streams::Socket *m_socket;
    MSA::State m_state;
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
