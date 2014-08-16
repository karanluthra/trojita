/* Copyright (C) 2006 - 2014 Jan Kundrát <jkt@flaska.net>

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

#ifndef IMAP_MESSAGECOMPOSER_H
#define IMAP_MESSAGECOMPOSER_H

#include <QAbstractListModel>
#include <QPointer>

#include "Composer/ContentDisposition.h"
#include "Composer/OutgoingMessage.h"
#include "Composer/Recipients.h"
#include "Imap/Model/CatenateData.h"
#include "Imap/Parser/Message.h"

namespace Imap {
namespace Mailbox {
class Model;
}
}

namespace Composer {

class AttachmentItem;

/** @short Model storing individual parts of a composed message */
class MessageComposer : public QAbstractListModel, public OutgoingMessage
{
    Q_OBJECT
public:

    explicit MessageComposer(Imap::Mailbox::Model *model, QObject *parent = 0);
    ~MessageComposer();

    virtual int rowCount(const QModelIndex &parent = QModelIndex()) const;
    virtual QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const;
    virtual Qt::DropActions supportedDropActions() const;
    virtual Qt::ItemFlags flags(const QModelIndex &index) const;
    virtual bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent);
    virtual QStringList mimeTypes() const;
    virtual QMimeData *mimeData(const QModelIndexList &indexes) const;

    void setInReplyTo(const QList<QByteArray> &inReplyTo);
    void setReferences(const QList<QByteArray> &references);
    void setSubject(const QString &subject);
    void setOrganization(const QString &organization);
    void setText(const QString &text);
    void setReplyingToMessage(const QModelIndex &index);
    void prepareForwarding(const QModelIndex &index, const ForwardMode mode);

    virtual bool isReadyForSerialization() const;
    virtual bool asRawMessage(QIODevice *target, QString *errorMessage) const;
    virtual bool asCatenateData(QList<Imap::Mailbox::CatenatePair> &target, QString *errorMessage) const;

    QList<QByteArray> inReplyTo() const;
    QList<QByteArray> references() const;
    virtual QModelIndex replyingToMessage() const;
    virtual QModelIndex forwardingMessage() const;

    bool addFileAttachment(const QString &path);
    void removeAttachment(const QModelIndex &index);
    void setAttachmentContentDisposition(const QModelIndex &index, const ContentDisposition disposition);
    void setAttachmentName(const QModelIndex &index, const QString &newName);

    virtual void setPreloadEnabled(const bool preload);

private:
    static QByteArray generateMessageId(const Imap::Message::MailAddress &sender);
    static QByteArray encodeHeaderField(const QString &text);
    static QByteArray generateMimeBoundary();

    void writeCommonMessageBeginning(QIODevice *target, const QByteArray boundary) const;
    bool writeAttachmentHeader(QIODevice *target, QString *errorMessage, const AttachmentItem *attachment, const QByteArray &boundary) const;
    bool writeAttachmentBody(QIODevice *target, QString *errorMessage, const AttachmentItem *attachment) const;

    void writeHeaderWithMsgIds(QIODevice *target, const QByteArray &headerName, const QList<QByteArray> &messageIds) const;

    bool validateDropImapMessage(QDataStream &stream, QString &mailbox, uint &uidValidity, QList<uint> &uids) const;
    bool validateDropImapPart(QDataStream &stream, QString &mailbox, uint &uidValidity, uint &uid, QByteArray &trojitaPath) const;
    bool dropImapMessage(QDataStream &stream);
    bool dropImapPart(QDataStream &stream);
    bool dropAttachmentList(QDataStream &stream);

    QList<QByteArray> m_inReplyTo;
    QList<QByteArray> m_references;
    QString m_subject;
    QString m_organization;
    QString m_text;
    QPersistentModelIndex m_replyingTo;
    QPersistentModelIndex m_forwarding;

    QList<AttachmentItem *> m_attachments;
    QPointer<Imap::Mailbox::Model> m_model;
    bool m_shouldPreload;
};

}

#endif // IMAP_MESSAGECOMPOSER_H
