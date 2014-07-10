#ifndef SMTPCLIENT_H
#define SMTPCLIENT_H

#include <QObject>
#include <QString>

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
};

class SMTPClient : public QObject
{
    Q_OBJECT
public:
    explicit SMTPClient(QObject *parent, QString &host, quint16 port);
    // TODO: Convert these enums to C++11 style enum classes
    enum Option {
        NoOptions,
        StartTlsOption,
        SizeOption,
        PipeliningOption,
        EightBitMimeOption,
        DSNOption,
        AuthOption,
    };
    // TODO: Convert these enums to C++11 style enum classes
    enum AuthMode {
        AuthNone,
        AuthPlain,
        AuthLogin,
    };
    Q_DECLARE_FLAGS (Options, Option)
    Q_DECLARE_FLAGS (AuthModes, AuthMode)

    void setAuthParams(QString &user, QString &password);
    void setMailParams(QByteArray &from, QList<QByteArray> &to, QByteArray &data);

private slots:
    void slotEncrypted();
    void slotReadyRead();
    void parseServerResponse();
    void parseCapabilities(QString &response);

private:

    void sendEhlo();
    void sendAuth(bool ready);
    void sendMailFrom();
    void sendRcpt(QByteArray &recipient);
    void sendData(bool ready);

    QString m_host;
    quint16 m_port;
    QString m_user;
    QString m_password;
    QByteArray m_from;
    QList<QByteArray> m_to;
    QByteArray m_data;
    Streams::SslTlsSocket *m_socket;
    MSA::State m_state;
    QByteArray line;

    Options m_options;
    AuthModes m_authModes;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(SMTPClient::Options)
Q_DECLARE_OPERATORS_FOR_FLAGS(SMTPClient::AuthModes)

}
#endif // SMTPCLIENT_H
