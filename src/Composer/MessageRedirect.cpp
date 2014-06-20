#include "MessageRedirect.h"
#include "Imap/Model/FullMessageCombiner.h"
#include "Common/Application.h"
#include "Composer/MessageComposer.h"
#include <QDebug>
#include <QUuid>

namespace Composer {

MessageRedirect::MessageRedirect(QModelIndex message) : m_message(message)
{
    fullMessageCombiner = new Imap::Mailbox::FullMessageCombiner(m_message);
    fullMessageCombiner->load();
    messageAvailable = fullMessageCombiner->loaded();
    connect(fullMessageCombiner, SIGNAL(completed()), this, SLOT(slotMessageAvailable()));
}

MessageRedirect::~MessageRedirect()
{
    delete fullMessageCombiner;
}

void MessageRedirect::slotMessageAvailable()
{
    messageAvailable = true;
}

void MessageRedirect::setData(Imap::Message::MailAddress from,
                              QList<QPair<Composer::RecipientKind, Imap::Message::MailAddress> > recipients,
                              QDateTime timestamp)
{
    m_from = from;
    m_recipients = recipients;
    m_timestamp = timestamp;
    m_messageId = generateMessageId(m_from);
}

bool MessageRedirect::asRawMessage(QIODevice *target, QString *errorMessage)
{
    // will be used by submission
    writeResentFields(target);

    if (messageAvailable) {
        target->write(fullMessageCombiner->data());
        return true;
    } else {
        *errorMessage = tr("Message not available");
        return false;
    }
}

void MessageRedirect::processListOfRecipientsIntoHeader(const QByteArray &prefix, const QList<QByteArray> &addresses, QByteArray &out)
{
    // Qt and STL are different, it looks like we cannot easily use something as simple as the ostream_iterator here :(
    if (!addresses.isEmpty()) {
        out.append(prefix);
        for (int i = 0; i < addresses.size() - 1; ++i)
            out.append(addresses[i]).append(",\r\n ");
        out.append(addresses.last()).append("\r\n");
    }
}

void MessageRedirect::writeResentFields(QIODevice *target)
{
    // The From header
    target->write(QByteArray("Resent-From: ").append(m_from.asMailHeader()).append("\r\n"));

    // All recipients
    // Got to group the headers so that both of (Resent-To, Resent-Cc) are present at most once
    QList<QByteArray> rcptTo, rcptCc;
    for (auto it = m_recipients.begin(); it != m_recipients.end(); ++it) {
        switch(it->first) {
        case Composer::ADDRESS_TO:
            rcptTo << it->second.asMailHeader();
            break;
        case Composer::ADDRESS_CC:
            rcptCc << it->second.asMailHeader();
            break;
        case Composer::ADDRESS_BCC:
            break;
        case Composer::ADDRESS_FROM:
        case Composer::ADDRESS_SENDER:
        case Composer::ADDRESS_REPLY_TO:
            // These should never ever be produced by Trojita for now
            Q_ASSERT(false);
            break;
        }
    }

    QByteArray recipientHeaders;
    processListOfRecipientsIntoHeader("Resent-To: ", rcptTo, recipientHeaders);
    processListOfRecipientsIntoHeader("Resent-Cc: ", rcptCc, recipientHeaders);
    target->write(recipientHeaders);

    // Other message metadata
    target->write("Resent-Date: " + Imap::dateTimeToRfc2822(m_timestamp).toUtf8() + "\r\n" +
                  "Message-ID: <" + m_messageId + ">\r\n" +
                  QString::fromUtf8("Resent-User-Agent: Trojita/%1; %2\r\n").arg(
                      Common::Application::version, Imap::Mailbox::systemPlatformVersion()).toUtf8() +
                  "MIME-Version: 1.0\r\n");
}

QByteArray MessageRedirect::generateMessageId(const Imap::Message::MailAddress &sender)
{
    if (sender.host.isEmpty()) {
        // There's no usable domain, let's just bail out of here
        return QByteArray();
    }
    return QUuid::createUuid()
#if QT_VERSION >= 0x040800
            .toByteArray()
#else
            .toString().toAscii()
#endif
            .replace("{", "").replace("}", "") + "@" + sender.host.toUtf8();
}

}
