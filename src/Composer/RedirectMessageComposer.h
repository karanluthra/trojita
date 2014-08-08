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

#ifndef REDIRECTMESSAGECOMPOSER_H
#define REDIRECTMESSAGECOMPOSER_H

#include <QObject>
#include <Composer/MessageComposer.h>
#include <Composer/OutgoingMessage.h>

namespace Imap {
namespace Mailbox {

class FullMessageCombiner;
}
}

namespace Composer {

class RedirectMessageComposer : public QObject, public OutgoingMessage
{
    Q_OBJECT
public:
    explicit RedirectMessageComposer(QObject *parent, QModelIndex message);

    virtual bool isReadyForSerialization() const;
    virtual bool asRawMessage(QIODevice *target, QString *errorMessage) const;
    virtual bool asCatenateData(QList<Imap::Mailbox::CatenatePair> &target, QString *errorMessage) const;
    virtual void setPreloadEnabled(const bool preload);

    virtual QModelIndex replyingToMessage() const;
    virtual QModelIndex forwardingMessage() const;

    void prepareMessage();

private slots:
    void slotMessageCombinerCompleted();
    QByteArray generateMessageId(const Imap::Message::MailAddress &sender) const;

private:
    void processListOfRecipientsIntoHeader(const QByteArray &prefix, const QList<QByteArray> &addresses, QByteArray &out) const;
    void writeResentFields(QIODevice *target) const;

    bool m_shouldPreload;
    bool isMessageAvailable;
    Imap::Mailbox::FullMessageCombiner *m_fullMessageCombiner;
    QModelIndex m_resentMessage;
};
}

#endif // REDIRECTMESSAGECOMPOSER_H
