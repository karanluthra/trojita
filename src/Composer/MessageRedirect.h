#ifndef MESSAGE_REDIRECT_H
#define MESSAGE_REDIRECT_H

#include <QObject>
#include <QPersistentModelIndex>
#include "Imap/Model/Utils.h"
#include "Imap/Parser/Message.h"
#include "Composer/Recipients.h"

namespace Imap {
namespace Mailbox {

class Model;
class FullMessageCombiner;
}
}

namespace Composer {

class MessageRedirect : public QObject
{
    Q_OBJECT
public:
    explicit MessageRedirect(QModelIndex message);
    ~MessageRedirect();

    bool asRawMessage(QIODevice *target, QString *errorMessage);
    void setData(Imap::Message::MailAddress from,
                 QList<QPair<Composer::RecipientKind, Imap::Message::MailAddress> > recipients,
                 QDateTime timestamp);

signals:

private slots:
    void slotMessageAvailable();

private:
    QByteArray generateMessageId(const Imap::Message::MailAddress &sender);
    void writeResentFields(QIODevice *target);
    void processListOfRecipientsIntoHeader(const QByteArray &prefix, const QList<QByteArray> &addresses, QByteArray &out);

    Imap::Message::MailAddress m_from;
    QList<QPair<Composer::RecipientKind, Imap::Message::MailAddress> > m_recipients;
    QDateTime m_timestamp;
    QByteArray m_messageId;

    QPersistentModelIndex m_message;
    Imap::Mailbox::FullMessageCombiner *fullMessageCombiner;
    bool messageAvailable;

};

}
#endif // MESSAGE_REDIRECT_H
