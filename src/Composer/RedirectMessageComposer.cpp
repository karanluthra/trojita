/* Copyright (C) 2014 Karan Luthra <karanluthra06@gmail.com>

   This file is part of the Trojita Qt IMAP e-mail client,
   http://trojita.flaska.net/

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of
   the License or (at your option) version 3 or any later version
   accepted by the membership of KDE e.V. (or its successor approved
   by the membership of KDE e.V.), which shall act as a proxy
   defined in Section 14 of version 3 of the license.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "RedirectMessageComposer.h"
#include <QUuid>
#include "Common/Application.h"
#include "Imap/Model/FullMessageCombiner.h"
#include "Imap/Model/Model.h"
#include "Imap/Model/Utils.h"

namespace Composer {

RedirectMessageComposer::RedirectMessageComposer(QObject *parent, QModelIndex message) :
    QObject(parent), m_shouldPreload(false), isMessageAvailable(false), m_fullMessageCombiner(0), m_resentMessage(message)
{
}

void RedirectMessageComposer::prepareMessage()
{
    m_fullMessageCombiner = new Imap::Mailbox::FullMessageCombiner(m_resentMessage);
    m_fullMessageCombiner->load();
    isMessageAvailable = m_fullMessageCombiner->loaded();
    connect(m_fullMessageCombiner, SIGNAL(completed()), this, SLOT(slotMessageCombinerCompleted()));
}

void RedirectMessageComposer::slotMessageCombinerCompleted()
{
    isMessageAvailable = true;
}

bool RedirectMessageComposer::isReadyForSerialization() const
{
    return !m_shouldPreload || isMessageAvailable;
}

bool RedirectMessageComposer::asRawMessage(QIODevice *target, QString *errorMessage) const
{
    writeResentFields(target);
    if (isMessageAvailable) {
        target->write(m_fullMessageCombiner->data());
        return true;
    } else {
        *errorMessage = tr("Message not available");
        return false;
    }
}

bool RedirectMessageComposer::asCatenateData(QList<Imap::Mailbox::CatenatePair> &target, QString *errorMessage) const
{
    Q_UNUSED(target);
    *errorMessage = tr("CATENATE not available currenly");
    return false;
}

void RedirectMessageComposer::setPreloadEnabled(const bool preload)
{
    m_shouldPreload = preload;
}

QModelIndex RedirectMessageComposer::replyingToMessage() const
{
    return QModelIndex();
}

QModelIndex RedirectMessageComposer::forwardingMessage() const
{
    return QModelIndex();
}

void RedirectMessageComposer::processListOfRecipientsIntoHeader(const QByteArray &prefix, const QList<QByteArray> &addresses, QByteArray &out) const
{
    // Qt and STL are different, it looks like we cannot easily use something as simple as the ostream_iterator here :(
    if (!addresses.isEmpty()) {
        out.append(prefix);
        for (int i = 0; i < addresses.size() - 1; ++i)
        out.append(addresses[i]).append(",\r\n ");
        out.append(addresses.last()).append("\r\n");
    }
}

void RedirectMessageComposer::writeResentFields(QIODevice *target) const
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

    QByteArray messageId = generateMessageId(m_from);

    // Other message metadata
    target->write("Resent-Date: " + Imap::dateTimeToRfc2822(m_timestamp).toUtf8() + "\r\n" +
    "Resent-Message-ID: <" + messageId + ">\r\n" +
    QString::fromUtf8("Resent-User-Agent: Trojita/%1; %2\r\n").arg(
    Common::Application::version, Imap::Mailbox::systemPlatformVersion()).toUtf8() +
    "MIME-Version: 1.0\r\n");
}

QByteArray RedirectMessageComposer::generateMessageId(const Imap::Message::MailAddress &sender) const
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
