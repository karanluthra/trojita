/* Copyright (C) 2006 - 2014 Jan Kundrát <jkt@flaska.net>
   Copyright (C) 2013 Pali Rohár <pali.rohar@gmail.com>

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
#include "SMTP.h"

namespace MSA
{

SMTP::SMTP(QObject *parent, const QString &host, quint16 port, bool encryptedConnect, bool startTls, bool auth,
           const QString &user):
    AbstractMSA(parent), host(host), port(port),
    encryptedConnect(encryptedConnect), startTls(startTls), auth(auth),
    user(user), failed(false), isWaitingForPassword(false), sendingMode(MODE_SMTP_INVALID)
{
    std::unique_ptr<Streams::SocketFactory> socketFactory;
    if (encryptedConnect)
        socketFactory.reset(new Streams::SslSocketFactory(host, port));
    else
        socketFactory.reset(new Streams::TlsAbleSocketFactory(host, port));

    // TODO: Add ability to specify proxy settings in the settings menu
    socketFactory->setProxySettings(Streams::ProxySettings::DirectConnect, QLatin1String("smtp"));

    client =  new SMTPClient(this, std::move(socketFactory));
    connect(client, SIGNAL(submitted()), this, SIGNAL(sent()));
}

void SMTP::cancel()
{
    // TODO:
    Q_ASSERT(false);
}

void SMTP::handleError(QAbstractSocket::SocketError err, const QString &msg)
{
    // TODO:
    Q_ASSERT(false);
    Q_UNUSED(err);
    failed = true;
    emit error(msg);
}

void SMTP::setPassword(const QString &password)
{
    pass = password;
    if (isWaitingForPassword)
        sendContinueGotPassword();
}

void SMTP::sendMail(const QByteArray &from, const QList<QByteArray> &to, const QByteArray &data)
{
    this->from = from;
    this->to = to;
    this->data = data;
    this->sendingMode = MODE_SMTP_DATA;
    this->isWaitingForPassword = true;
    emit progressMax(data.size());
    emit progress(0);
    emit connecting();
    if (!auth || !pass.isEmpty()) {
        sendContinueGotPassword();
        return;
    }
    emit passwordRequested(user, host);
}

void SMTP::sendContinueGotPassword()
{
    isWaitingForPassword = false;

    if (auth)
        client->setAuthParams(user, pass);

    emit sending(); // FIXME: later

    switch (sendingMode) {
    case MODE_SMTP_DATA:
        client->setMailParams(from, to, data);
        break;
    case MODE_SMTP_BURL:
        // TODO: add support for BURL
        break;
    default:
        failed = true;
        emit error(tr("Unknown SMTP mode"));
        break;
    }

    // Start the connection now
    client->doConnect();

    if (startTls) {
        // TODO: add startTls()
        Q_ASSERT(false);
    }

    // Enter the client state-machine
    // TODO: client->exec();

    client->closeConnection();
}

bool SMTP::supportsBurl() const
{
    return true;
}

void SMTP::sendBurl(const QByteArray &from, const QList<QByteArray> &to, const QByteArray &imapUrl)
{
    this->from = from;
    this->to = to;
    this->data = imapUrl;
    this->sendingMode = MODE_SMTP_BURL;
    this->isWaitingForPassword = true;
    emit progressMax(1);
    emit progress(0);
    emit connecting();
    if (!auth || !pass.isEmpty()) {
        sendContinueGotPassword();
        return;
    }
    emit passwordRequested(user, host);
}

SMTPFactory::SMTPFactory(const QString &host, quint16 port, bool encryptedConnect, bool startTls,
                         bool auth, const QString &user):
    m_host(host), m_port(port), m_encryptedConnect(encryptedConnect), m_startTls(startTls),
    m_auth(auth), m_user(user)
{
}

SMTPFactory::~SMTPFactory()
{
}

AbstractMSA *SMTPFactory::create(QObject *parent) const
{
    return new SMTP(parent, m_host, m_port, m_encryptedConnect, m_startTls, m_auth, m_user);
}

}
