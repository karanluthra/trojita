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

#include "OutgoingMessage.h"

namespace Composer
{

void OutgoingMessage::setFrom(const Imap::Message::MailAddress &from)
{
    m_from = from;
}

void OutgoingMessage::setRecipients(const QList<QPair<Composer::RecipientKind, Imap::Message::MailAddress> > &recipients)
{
    m_recipients = recipients;
}

void OutgoingMessage::setTimestamp(const QDateTime &timestamp)
{
    m_timestamp = timestamp;
}

QDateTime OutgoingMessage::timestamp() const
{
    return m_timestamp;
}

QByteArray OutgoingMessage::rawFromAddress() const
{
    return m_from.asSMTPMailbox();
}

QList<QByteArray> OutgoingMessage::rawRecipientAddresses() const
{
    QList<QByteArray> res;

    for (auto it = m_recipients.begin(); it != m_recipients.end(); ++it) {
        res << it->second.asSMTPMailbox();
    }

    return res;
}
}
