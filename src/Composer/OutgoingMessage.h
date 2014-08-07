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

#ifndef OUTGOINGMESSAGE_H
#define OUTGOINGMESSAGE_H

#include "Composer/Recipients.h"
#include "Imap/Model/CatenateData.h"
#include "Imap/Parser/Message.h"

namespace Composer
{

class OutgoingMessage
{
public:
    virtual ~OutgoingMessage() {}

    virtual bool isReadyForSerialization() const = 0;
    virtual bool asRawMessage(QIODevice *target, QString *errorMessage) const = 0;
    virtual bool asCatenateData(QList<Imap::Mailbox::CatenatePair> &target, QString *errorMessage) const = 0;
    virtual void setPreloadEnabled(const bool preload) = 0;

    virtual QModelIndex replyingToMessage() const = 0;
    virtual QModelIndex forwardingMessage() const = 0;

    QDateTime timestamp() const;
    QByteArray rawFromAddress() const;
    QList<QByteArray> rawRecipientAddresses() const;

    void setFrom(const Imap::Message::MailAddress &from);
    void setRecipients(const QList<QPair<Composer::RecipientKind, Imap::Message::MailAddress> > &recipients);
    void setTimestamp(const QDateTime &timestamp);

protected:
    Imap::Message::MailAddress m_from;
    QList<QPair<Composer::RecipientKind, Imap::Message::MailAddress> > m_recipients;
    QDateTime m_timestamp;
};

}
#endif // OUTGOINGMESSAGE_H
