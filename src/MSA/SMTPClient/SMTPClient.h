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
    MAIL,
};

class SMTPClient : public QObject
{
    Q_OBJECT
public:
    explicit SMTPClient(QObject *parent, QString &host, quint16 port);
    enum Option {
        NoOptions,
        StartTlsOption,
        SizeOption,
        PipeliningOption,
        EightBitMimeOption,
        AuthOption,
    };
    enum AuthMode {
        AuthNone,
        AuthPlain,
        AuthLogin,
    };
    Q_DECLARE_FLAGS (Options, Option)
    Q_DECLARE_FLAGS (AuthModes, AuthMode)

private slots:
    void slotEncrypted();
    void slotReadyRead();
    void parseServerResponse();
    void sendEhlo();
    void parseCapabilities(QString &response);

private:
    QString m_host;
    quint16 m_port;
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
