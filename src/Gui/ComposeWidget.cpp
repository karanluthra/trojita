/* Copyright (C) 2006 - 2014 Jan Kundrát <jkt@flaska.net>
   Copyright (C) 2012 Peter Amidon <peter@picnicpark.org>
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
#include <QAbstractProxyModel>
#include <QBuffer>
#include <QFileDialog>
#include <QGraphicsOpacityEffect>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QToolButton>
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
#include <QUrlQuery>
#endif

#include "ui_ComposeWidget.h"
#include "ui_EnvelopeFormWidget.h"
#include "Composer/MessageComposer.h"
#include "Composer/ReplaceSignature.h"
#include "Composer/Mailto.h"
#include "Composer/SenderIdentitiesModel.h"
#include "Composer/Submission.h"
#include "Common/InvokeMethod.h"
#include "Common/Paths.h"
#include "Common/SettingsNames.h"
#include "Gui/AbstractAddressbook.h"
#include "Gui/AutoCompletion.h"
#include "Gui/ComposeWidget.h"
#include "Gui/FromAddressProxyModel.h"
#include "Gui/IconLoader.h"
#include "Gui/LineEdit.h"
#include "Gui/OverlayWidget.h"
#include "Gui/PasswordDialog.h"
#include "Gui/ProgressPopUp.h"
#include "Gui/Util.h"
#include "Gui/Window.h"
#include "Imap/Model/ItemRoles.h"
#include "Imap/Model/Model.h"
#include "Imap/Tasks/AppendTask.h"
#include "Imap/Tasks/GenUrlAuthTask.h"
#include "Imap/Tasks/UidSubmitTask.h"
#include "Plugins/PluginManager.h"
#include "ShortcutHandler/ShortcutHandler.h"
#include "UiUtils/Color.h"

namespace
{
enum { OFFSET_OF_FIRST_ADDRESSEE = 1 };
}

namespace Gui
{

static const QString trojita_opacityAnimation = QLatin1String("trojita_opacityAnimation");

ComposeWidget::ComposeWidget(MainWindow *mainWindow, MSA::MSAFactory *msaFactory) :
    QWidget(0, Qt::Window),
    ui(new Ui::ComposeWidget),
    m_sentMail(false),
    m_messageUpdated(false),
    m_messageEverEdited(false),
    m_explicitDraft(false),
    m_appendUidReceived(false), m_appendUidValidity(0), m_appendUid(0), m_genUrlAuthReceived(false),
    m_mainWindow(mainWindow),
    m_settings(mainWindow->settings())
{
    setAttribute(Qt::WA_DeleteOnClose, true);

    QIcon winIcon;
    winIcon.addFile(QLatin1String(":/icons/trojita-edit-big.png"), QSize(128, 128));
    winIcon.addFile(QLatin1String(":/icons/trojita-edit-small.png"), QSize(22, 22));
    setWindowIcon(winIcon);

    Q_ASSERT(m_mainWindow);
    m_submission = new Composer::Submission(this, m_mainWindow->imapModel(), msaFactory);
    connect(m_submission, SIGNAL(succeeded()), this, SLOT(sent()));
    connect(m_submission, SIGNAL(failed(QString)), this, SLOT(gotError(QString)));
    connect(m_submission, SIGNAL(passwordRequested(QString,QString)), this, SLOT(passwordRequested(QString,QString)), Qt::QueuedConnection);

    ui->setupUi(this);
    ui->attachmentsView->setComposer(m_submission->composer());

    // FIXME: Get rid of this and find a way to send these references through EnvelopeFormWidget's ctor
    ui->envelopeWidget->setData(m_mainWindow);

    sendButton = ui->buttonBox->addButton(tr("Send"), QDialogButtonBox::AcceptRole);
    connect(sendButton, SIGNAL(clicked()), this, SLOT(send()));
    cancelButton = ui->buttonBox->addButton(QDialogButtonBox::Cancel);
    connect(cancelButton, SIGNAL(clicked()), this, SLOT(close()));
    connect(ui->attachButton, SIGNAL(clicked()), this, SLOT(slotAskForFileAttachment()));

    connect(ui->verticalSplitter, SIGNAL(splitterMoved(int, int)), ui->envelopeWidget ,SLOT(calculateMaxVisibleRecipients()));
    ui->envelopeWidget->calculateMaxVisibleRecipients();

    m_markButton = new QToolButton(ui->buttonBox);
    m_markButton->setPopupMode(QToolButton::MenuButtonPopup);
    m_markButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_markAsReply = new QActionGroup(m_markButton);
    m_markAsReply->setExclusive(true);
    auto *asReplyMenu = new QMenu(m_markButton);
    m_markButton->setMenu(asReplyMenu);
    m_actionStandalone = asReplyMenu->addAction(Gui::loadIcon(QLatin1String("mail-view-flat")), tr("New Thread"));
    m_actionStandalone->setActionGroup(m_markAsReply);
    m_actionStandalone->setCheckable(true);
    m_actionStandalone->setToolTip(tr("This mail will be sent as a standalone message.<hr/>Change to preserve the reply hierarchy."));
    m_actionInReplyTo = asReplyMenu->addAction(Gui::loadIcon(QLatin1String("mail-view-threaded")), tr("Threaded"));
    m_actionInReplyTo->setActionGroup(m_markAsReply);
    m_actionInReplyTo->setCheckable(true);

    // This is a "quick shortcut action". It shows the UI bits of the current option, but when the user clicks it,
    // the *other* action is triggered.
    m_actionToggleMarking = new QAction(m_markButton);
    connect(m_actionToggleMarking, SIGNAL(triggered()), this, SLOT(toggleReplyMarking()));
    m_markButton->setDefaultAction(m_actionToggleMarking);

    // Unfortunately, there's no signal for toggled(QAction*), so we'll have to call QAction::trigger() to have this working
    connect(m_markAsReply, SIGNAL(triggered(QAction*)), this, SLOT(updateReplyMarkingAction()));
    m_actionStandalone->trigger();

    m_replyModeButton = new QToolButton(ui->buttonBox);
    m_replyModeButton->setPopupMode(QToolButton::InstantPopup);
    m_replyModeButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    QMenu *replyModeMenu = new QMenu(m_replyModeButton);
    m_replyModeButton->setMenu(replyModeMenu);

    m_replyModeActions = new QActionGroup(m_replyModeButton);
    m_replyModeActions->setExclusive(true);

    m_actionHandPickedRecipients = new QAction(Gui::loadIcon(QLatin1String("document-edit")) ,QLatin1String("Hand Picked Recipients"), this);
    replyModeMenu->addAction(m_actionHandPickedRecipients);
    m_actionHandPickedRecipients->setActionGroup(m_replyModeActions);
    m_actionHandPickedRecipients->setCheckable(true);

    replyModeMenu->addSeparator();

    QAction *placeHolderAction = ShortcutHandler::instance()->action(QLatin1String("action_reply_private"));
    m_actionReplyModePrivate = replyModeMenu->addAction(placeHolderAction->icon(), placeHolderAction->text());
    m_actionReplyModePrivate->setActionGroup(m_replyModeActions);
    m_actionReplyModePrivate->setCheckable(true);

    placeHolderAction = ShortcutHandler::instance()->action(QLatin1String("action_reply_all_but_me"));
    m_actionReplyModeAllButMe = replyModeMenu->addAction(placeHolderAction->icon(), placeHolderAction->text());
    m_actionReplyModeAllButMe->setActionGroup(m_replyModeActions);
    m_actionReplyModeAllButMe->setCheckable(true);

    placeHolderAction = ShortcutHandler::instance()->action(QLatin1String("action_reply_all"));
    m_actionReplyModeAll = replyModeMenu->addAction(placeHolderAction->icon(), placeHolderAction->text());
    m_actionReplyModeAll->setActionGroup(m_replyModeActions);
    m_actionReplyModeAll->setCheckable(true);

    placeHolderAction = ShortcutHandler::instance()->action(QLatin1String("action_reply_list"));
    m_actionReplyModeList = replyModeMenu->addAction(placeHolderAction->icon(), placeHolderAction->text());
    m_actionReplyModeList->setActionGroup(m_replyModeActions);
    m_actionReplyModeList->setCheckable(true);

    connect(m_replyModeActions, SIGNAL(triggered(QAction*)), this, SLOT(updateReplyMode()));

    // We want to have the button aligned to the left; the only "portable" way of this is the ResetRole
    // (thanks to TL for mentioning this, and for the Qt's doc for providing pretty pictures on different platforms)
    ui->buttonBox->addButton(m_markButton, QDialogButtonBox::ResetRole);
    // Using ResetRole for reasons same as with m_markButton. We want this button to be second from the left.
    ui->buttonBox->addButton(m_replyModeButton, QDialogButtonBox::ResetRole);

    m_markButton->hide();
    m_replyModeButton->hide();

    ui->mailText->setFont(Gui::Util::systemMonospaceFont());

    connect(ui->mailText, SIGNAL(urlsAdded(QList<QUrl>)), SLOT(slotAttachFiles(QList<QUrl>)));
    connect(ui->mailText, SIGNAL(sendRequest()), SLOT(send()));
    connect(ui->mailText, SIGNAL(textChanged()), SLOT(setMessageUpdated()));
    connect(ui->envelopeWidget->ui->subject, SIGNAL(textChanged(QString)), SLOT(updateWindowTitle()));
    connect(ui->envelopeWidget->ui->subject, SIGNAL(textChanged(QString)), SLOT(setMessageUpdated()));
    updateWindowTitle();

    connect(ui->envelopeWidget->ui->sender, SIGNAL(currentIndexChanged(int)), SLOT(slotUpdateSignature()));
    connect(ui->envelopeWidget->ui->sender, SIGNAL(editTextChanged(QString)), SLOT(setMessageUpdated()));
    connect(ui->envelopeWidget, SIGNAL(recipientTextChanged(QString)), this, SLOT(setMessageUpdated()));
    connect(ui->envelopeWidget, SIGNAL(recipientTextChanged(QString)), this, SLOT(markReplyModeHandpicked()));

    QTimer *autoSaveTimer = new QTimer(this);
    connect(autoSaveTimer, SIGNAL(timeout()), SLOT(autoSaveDraft()));
    autoSaveTimer->start(30*1000);

    // these are for the automatically saved drafts, i.e. no i18n for the dir name
    m_autoSavePath = QString(Common::writablePath(Common::LOCATION_CACHE) + QLatin1String("Drafts/"));
    QDir().mkpath(m_autoSavePath);

#if QT_VERSION < QT_VERSION_CHECK(4, 7, 0)
    m_autoSavePath += QString::number(QDateTime(QDate(1970, 1, 1), QTime(0, 0, 0)).secsTo(QDateTime::currentDateTime())) + QLatin1String(".draft");
#else
    m_autoSavePath += QString::number(QDateTime::currentMSecsSinceEpoch()) + QLatin1String(".draft");
#endif

    // Add a blank recipient row to start with
    ui->envelopeWidget->addRecipient(ui->envelopeWidget->recipients()->count(), Composer::ADDRESS_TO, QString());
    ui->envelopeWidget->ui->envelopeLayout->itemAt(OFFSET_OF_FIRST_ADDRESSEE, QFormLayout::FieldRole)->widget()->setFocus();
}

ComposeWidget::~ComposeWidget()
{
    delete ui;
}

/** @short Throw a warning at an attempt to create a Compose Widget while the MSA is not configured */
ComposeWidget *ComposeWidget::warnIfMsaNotConfigured(ComposeWidget *widget, MainWindow *mainWindow)
{
    if (!widget)
        QMessageBox::critical(mainWindow, tr("Error"), tr("Please set appropriate settings for outgoing messages."));
    return widget;
}

/** @short Create a blank composer window */
ComposeWidget *ComposeWidget::createBlank(MainWindow *mainWindow)
{
    MSA::MSAFactory *msaFactory = mainWindow->msaFactory();
    if (!msaFactory)
        return 0;

    ComposeWidget *w = new ComposeWidget(mainWindow, msaFactory);
    Util::centerWidgetOnScreen(w);
    w->show();
    return w;
}

/** @short Load a draft in composer window */
ComposeWidget *ComposeWidget::createDraft(MainWindow *mainWindow, const QString &path)
{
    MSA::MSAFactory *msaFactory = mainWindow->msaFactory();
    if (!msaFactory)
        return 0;

    ComposeWidget *w = new ComposeWidget(mainWindow, msaFactory);
    w->loadDraft(path);
    Util::centerWidgetOnScreen(w);
    w->show();
    return w;
}

/** @short Create a composer window with data from a URL */
ComposeWidget *ComposeWidget::createFromUrl(MainWindow *mainWindow, const QUrl &url)
{
    MSA::MSAFactory *msaFactory = mainWindow->msaFactory();
    if (!msaFactory)
        return 0;

    ComposeWidget *w = new ComposeWidget(mainWindow, msaFactory);
    QString subject;
    QString body;
    QList<QPair<Composer::RecipientKind,QString> > recipients;
    QList<QByteArray> inReplyTo;
    QList<QByteArray> references;

#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
    const QUrl &q = url;
#else
    const QUrlQuery q(url);
#endif

    if (!q.queryItemValue(QLatin1String("X-Trojita-DisplayName")).isEmpty()) {
        // There should be only single email address created by Imap::Message::MailAddress::asUrl()
        Imap::Message::MailAddress addr;
        if (Imap::Message::MailAddress::fromUrl(addr, url, QLatin1String("mailto")))
            recipients << qMakePair(Composer::ADDRESS_TO, addr.asPrettyString());
    } else {
        // This should be real RFC 6068 mailto:
        Composer::parseRFC6068Mailto(url, subject, body, recipients, inReplyTo, references);
    }

    // NOTE: we need inReplyTo and references parameters without angle brackets, so remove them
    for (int i = 0; i < inReplyTo.size(); ++i) {
        if (inReplyTo[i].startsWith('<') && inReplyTo[i].endsWith('>')) {
            inReplyTo[i] = inReplyTo[i].mid(1, inReplyTo[i].size()-2);
        }
    }
    for (int i = 0; i < references.size(); ++i) {
        if (references[i].startsWith('<') && references[i].endsWith('>')) {
            references[i] = references[i].mid(1, references[i].size()-2);
        }
    }

    w->setResponseData(recipients, subject, body, inReplyTo, references, QModelIndex());
    Util::centerWidgetOnScreen(w);
    w->show();
    return w;
}

/** @short Create a composer window for a reply */
ComposeWidget *ComposeWidget::createReply(MainWindow *mainWindow, const Composer::ReplyMode &mode, const QModelIndex &replyingToMessage,
                                          const QList<QPair<Composer::RecipientKind, QString> > &recipients, const QString &subject,
                                          const QString &body, const QList<QByteArray> &inReplyTo, const QList<QByteArray> &references)
{
    MSA::MSAFactory *msaFactory = mainWindow->msaFactory();
    if (!msaFactory)
        return 0;

    ComposeWidget *w = new ComposeWidget(mainWindow, msaFactory);
    w->setResponseData(recipients, subject, body, inReplyTo, references, replyingToMessage);
    bool ok = w->setReplyMode(mode);
    if (!ok) {
        QString err;
        switch (mode) {
        case Composer::REPLY_ALL:
        case Composer::REPLY_ALL_BUT_ME:
            // do nothing
            break;
        case Composer::REPLY_LIST:
            err = tr("It doesn't look like this is a message to the mailing list. Please fill in the recipients manually.");
            break;
        case Composer::REPLY_PRIVATE:
            err = trUtf8("Trojitá was unable to safely determine the real e-mail address of the author of the message. "
                         "You might want to use the \"Reply All\" function and trim the list of addresses manually.");
            break;
        }
        if (!err.isEmpty())
            QMessageBox::warning(w, tr("Cannot Determine Recipients"), err);
    }
    Util::centerWidgetOnScreen(w);
    w->show();
    return w;
}

/** @short Create a composer window for a mail-forward action */
ComposeWidget *ComposeWidget::createForward(MainWindow *mainWindow, const Composer::ForwardMode mode, const QModelIndex &forwardingMessage,
                                    const QString &subject, const QList<QByteArray> &inReplyTo, const QList<QByteArray> &references)
{
    MSA::MSAFactory *msaFactory = mainWindow->msaFactory();
    if (!msaFactory)
        return 0;

    ComposeWidget *w = new ComposeWidget(mainWindow, msaFactory);
    w->setResponseData(QList<QPair<Composer::RecipientKind, QString>>(), subject, QString(), inReplyTo, references, QModelIndex());
    // We don't need to expose any UI here, but we want the in-reply-to and references information to be carried with this message
    w->m_actionInReplyTo->setChecked(true);

    // Only those changes that are made to the composer's fields *after* it has been created should qualify as "edits"
    w->m_messageEverEdited = false;

    // Prepare the message to be forwarded and add it to the attachments view
    w->m_submission->composer()->prepareForwarding(forwardingMessage, mode);

    Util::centerWidgetOnScreen(w);
    w->show();
    return w;
}

void ComposeWidget::updateReplyMode()
{
    if (m_actionHandPickedRecipients->isChecked())
        markReplyModeHandpicked();
    else if (m_actionReplyModePrivate->isChecked())
        setReplyMode(Composer::REPLY_PRIVATE);
    else if (m_actionReplyModeAllButMe->isChecked())
        setReplyMode(Composer::REPLY_ALL_BUT_ME);
    else if (m_actionReplyModeAll->isChecked())
        setReplyMode(Composer::REPLY_ALL);
    else if (m_actionReplyModeList->isChecked())
        setReplyMode(Composer::REPLY_LIST);
}

void ComposeWidget::markReplyModeHandpicked()
{
    if (m_replyModeButton->isEnabled()) {
        m_actionHandPickedRecipients->setChecked(true);
        m_replyModeButton->setText(m_actionHandPickedRecipients->text());
        m_replyModeButton->setIcon(m_actionHandPickedRecipients->icon());
    }
}

void ComposeWidget::passwordRequested(const QString &user, const QString &host)
{
    Plugins::PasswordPlugin *password = m_mainWindow->pluginManager()->password();
    if (!password) {
        askPassword(user, host);
        return;
    }

    Plugins::PasswordJob *job = password->requestPassword(QLatin1String("account-0"), QLatin1String("smtp"));
    if (!job) {
        askPassword(user, host);
        return;
    }

    connect(job, SIGNAL(passwordAvailable(QString)), m_submission, SLOT(setPassword(QString)));
    connect(job, SIGNAL(error(Plugins::PasswordJob::Error,QString)), this, SLOT(passwordError()));

    job->setAutoDelete(true);
    job->setProperty("user", user);
    job->setProperty("host", host);
    job->start();
}

void ComposeWidget::passwordError()
{
    Plugins::PasswordJob *job = static_cast<Plugins::PasswordJob *>(sender());
    const QString &user = job->property("user").toString();
    const QString &host = job->property("host").toString();
    askPassword(user, host);
}

void ComposeWidget::askPassword(const QString &user, const QString &host)
{
    bool ok;
    const QString &password = Gui::PasswordDialog::getPassword(this, tr("Authentication Required"),
                                           tr("<p>Please provide SMTP password for user <b>%1</b> on <b>%2</b>:</p>").arg(
#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
                                               Qt::escape(user),
                                               Qt::escape(host)
#else
                                               user.toHtmlEscaped(),
                                               host.toHtmlEscaped()
#endif
                                               ),
                                           QString(), &ok);
    if (ok)
        m_submission->setPassword(password);
    else
        m_submission->cancelPassword();
}

void ComposeWidget::changeEvent(QEvent *e)
{
    QWidget::changeEvent(e);
    switch (e->type()) {
    case QEvent::LanguageChange:
        ui->retranslateUi(this);
        break;
    default:
        break;
    }
}

/**
 * We capture the close event and check whether there's something to save
 * (not sent, not up-to-date or persistent autostore)
 * The offer the user to store or omit the message or not close at all
 */

void ComposeWidget::closeEvent(QCloseEvent *ce)
{
    const bool noSaveRequired = m_sentMail || !m_messageEverEdited ||
                                (m_explicitDraft && !m_messageUpdated); // autosave to permanent draft and no update
    if (!noSaveRequired) {  // save is required
        QMessageBox msgBox(this);
        msgBox.setWindowModality(Qt::WindowModal);
        msgBox.setWindowTitle(tr("Save Draft?"));
        QString message(tr("The mail has not been sent.<br>Do you want to save the draft?"));
        if (ui->attachmentsView->model()->rowCount() > 0)
            message += tr("<br><span style=\"color:red\">Warning: Attachments are <b>not</b> saved with the draft!</span>");
        msgBox.setText(message);
        msgBox.setStandardButtons(QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        msgBox.setDefaultButton(QMessageBox::Save);
        int ret = msgBox.exec();
        if (ret == QMessageBox::Save) {
            if (m_explicitDraft) { // editing a present draft - override it
                saveDraft(m_autoSavePath);
            } else {
                // Explicitly stored drafts should be saved in a location with proper i18n support, so let's make sure both main
                // window and this code uses the same tr() calls
                QString path(Common::writablePath(Common::LOCATION_DATA) + Gui::MainWindow::tr("Drafts"));
                QDir().mkpath(path);
                QString filename = ui->envelopeWidget->ui->subject->text();
                if (filename.isEmpty()) {
                    filename = QDateTime::currentDateTime().toString(Qt::ISODate);
                }
                // Some characters are best avoided in file names. This is probably not a definitive list, but the hope is that
                // it's going to be more readable than an unformatted hash or similar stuff.  The list of characters was taken
                // from http://en.wikipedia.org/wiki/Filename#Reserved_characters_and_words .
                filename.replace(QRegExp(QLatin1String("[/\\\\:\"|<>*?]")), QLatin1String("_"));
                path = QFileDialog::getSaveFileName(this, tr("Save as"), path + QLatin1Char('/') + filename + QLatin1String(".draft"),
                                                    tr("Drafts") + QLatin1String(" (*.draft)"));
                if (path.isEmpty()) { // cancelled save
                    ret = QMessageBox::Cancel;
                } else {
                    m_explicitDraft = true;
                    saveDraft(path);
                    if (path != m_autoSavePath) // we can remove the temp save
                        QFile::remove(m_autoSavePath);
                }
            }
        }
        if (ret == QMessageBox::Cancel) {
            ce->ignore(); // don't close the window
            return;
        }
    }
    if (m_sentMail || !m_explicitDraft) // is the mail has been sent or the user does not want to store it
        QFile::remove(m_autoSavePath); // get rid of draft
    ce->accept(); // ultimately close the window
}



bool ComposeWidget::buildMessageData()
{
    QList<QPair<Composer::RecipientKind,Imap::Message::MailAddress> > recipients;
    QString errorMessage;
    if (!ui->envelopeWidget->parseRecipients(recipients, errorMessage)) {
        gotError(tr("Cannot parse recipients:\n%1").arg(errorMessage));
        return false;
    }
    if (recipients.isEmpty()) {
        gotError(tr("You haven't entered any recipients"));
        return false;
    }
    m_submission->composer()->setRecipients(recipients);

    Imap::Message::MailAddress fromAddress;
    if (!Imap::Message::MailAddress::fromPrettyString(fromAddress, ui->envelopeWidget->ui->sender->currentText())) {
        gotError(tr("The From: address does not look like a valid one"));
        return false;
    }
    if (ui->envelopeWidget->ui->subject->text().isEmpty()) {
        gotError(tr("You haven't entered any subject. Cannot send such a mail, sorry."));
        ui->envelopeWidget->ui->subject->setFocus();
        return false;
    }
    m_submission->composer()->setFrom(fromAddress);

    m_submission->composer()->setTimestamp(QDateTime::currentDateTime());
    m_submission->composer()->setSubject(ui->envelopeWidget->ui->subject->text());

    QAbstractProxyModel *proxy = qobject_cast<QAbstractProxyModel*>(ui->envelopeWidget->ui->sender->model());
    Q_ASSERT(proxy);

    if (ui->envelopeWidget->ui->sender->findText(ui->envelopeWidget->ui->sender->currentText()) != -1) {
        QModelIndex proxyIndex = ui->envelopeWidget->ui->sender->model()->index(ui->envelopeWidget->ui->sender->currentIndex(), 0, ui->envelopeWidget->ui->sender->rootModelIndex());
        Q_ASSERT(proxyIndex.isValid());
        m_submission->composer()->setOrganization(
                    proxy->mapToSource(proxyIndex).sibling(proxyIndex.row(), Composer::SenderIdentitiesModel::COLUMN_ORGANIZATION)
                    .data().toString());
    }
    m_submission->composer()->setText(ui->mailText->toPlainText());

    if (m_actionInReplyTo->isChecked()) {
        m_submission->composer()->setInReplyTo(m_inReplyTo);
        m_submission->composer()->setReferences(m_references);
        m_submission->composer()->setReplyingToMessage(m_replyingToMessage);
    } else {
        m_submission->composer()->setInReplyTo(QList<QByteArray>());
        m_submission->composer()->setReferences(QList<QByteArray>());
        m_submission->composer()->setReplyingToMessage(QModelIndex());
    }

    return m_submission->composer()->isReadyForSerialization();
}

void ComposeWidget::send()
{
    // Well, Trojita is of course rock solid and will never ever crash :), but experience has shown that every now and then,
    // there is a subtle issue $somewhere. This means that it's probably a good idea to save the draft explicitly -- better
    // than losing some work. It's cheap anyway.
    saveDraft(m_autoSavePath);

    if (!buildMessageData())
        return;

    m_submission->setImapOptions(m_settings->value(Common::SettingsNames::composerSaveToImapKey, true).toBool(),
                                 m_settings->value(Common::SettingsNames::composerImapSentKey, tr("Sent")).toString(),
                                 m_settings->value(Common::SettingsNames::imapHostKey).toString(),
                                 m_settings->value(Common::SettingsNames::imapUserKey).toString(),
                                 m_settings->value(Common::SettingsNames::msaMethodKey).toString() == Common::SettingsNames::methodImapSendmail);
    m_submission->setSmtpOptions(m_settings->value(Common::SettingsNames::smtpUseBurlKey, false).toBool(),
                                 m_settings->value(Common::SettingsNames::smtpUserKey).toString());


    ProgressPopUp *progress = new ProgressPopUp();
    OverlayWidget *overlay = new OverlayWidget(progress, this);
    overlay->show();
    setUiWidgetsEnabled(false);

    connect(m_submission, SIGNAL(progressMin(int)), progress, SLOT(setMinimum(int)));
    connect(m_submission, SIGNAL(progressMax(int)), progress, SLOT(setMaximum(int)));
    connect(m_submission, SIGNAL(progress(int)), progress, SLOT(setValue(int)));
    connect(m_submission, SIGNAL(updateStatusMessage(QString)), progress, SLOT(setLabelText(QString)));
    connect(m_submission, SIGNAL(succeeded()), overlay, SLOT(deleteLater()));
    connect(m_submission, SIGNAL(failed(QString)), overlay, SLOT(deleteLater()));

    m_submission->send();
}

void ComposeWidget::setUiWidgetsEnabled(const bool enabled)
{
    ui->verticalSplitter->setEnabled(enabled);
    ui->buttonBox->setEnabled(enabled);
}

/** @short Set private data members to get pre-filled by available parameters

The semantics of the @arg inReplyTo and @arg references are the same as described for the Composer::MessageComposer,
i.e. the data are not supposed to contain the angle bracket.  If the @arg replyingToMessage is present, it will be used
as an index to a message which will get marked as replied to.  This is needed because IMAP doesn't really support site-wide
search by a Message-Id (and it cannot possibly support it in general, either), and because Trojita's lazy loading and lack
of cross-session persistent indexes means that "mark as replied" and "extract message-id from" are effectively two separate
operations.
*/
void ComposeWidget::setResponseData(const QList<QPair<Composer::RecipientKind, QString> > &recipients,
                            const QString &subject, const QString &body, const QList<QByteArray> &inReplyTo,
                            const QList<QByteArray> &references, const QModelIndex &replyingToMessage)
{
    for (int i = 0; i < recipients.size(); ++i) {
        ui->envelopeWidget->addRecipient(i, recipients.at(i).first, recipients.at(i).second);
    }
    ui->envelopeWidget->updateRecipientList();
    //ui->envelopeWidget->ui->envelopeLayout->itemAt(OFFSET_OF_FIRST_ADDRESSEE, QFormLayout::FieldRole)->widget()->setFocus();
    ui->envelopeWidget->ui->subject->setText(subject);
    const bool wasEdited = m_messageEverEdited;
    ui->mailText->setText(body);
    m_messageEverEdited = wasEdited;
    m_inReplyTo = inReplyTo;

    // Trim the References header as per RFC 5537
    QList<QByteArray> trimmedReferences = references;
    int referencesSize = QByteArray("References: ").size();
    const int lineOverhead = 3; // one for the " " prefix, two for the \r\n suffix
    Q_FOREACH(const QByteArray &item, references)
        referencesSize += item.size() + lineOverhead;
    // The magic numbers are from RFC 5537
    while (referencesSize >= 998 && trimmedReferences.size() > 3) {
        referencesSize -= trimmedReferences.takeAt(1).size() + lineOverhead;
    }
    m_references = trimmedReferences;
    m_replyingToMessage = replyingToMessage;
    if (m_replyingToMessage.isValid()) {
        m_markButton->show();
        m_replyModeButton->show();
        // Got to use trigger() so that the default action of the QToolButton is updated
        m_actionInReplyTo->setToolTip(tr("This mail will be marked as a response<hr/>%1").arg(
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
                                          m_replyingToMessage.data(Imap::Mailbox::RoleMessageSubject).toString().toHtmlEscaped()
#else
                                          Qt::escape(m_replyingToMessage.data(Imap::Mailbox::RoleMessageSubject).toString())
#endif
                                          ));
        m_actionInReplyTo->trigger();

        // Enable only those Reply Modes that are applicable to the message to be replied
        Composer::RecipientList dummy;
        m_actionReplyModePrivate->setEnabled(Composer::Util::replyRecipientList(Composer::REPLY_PRIVATE,
                                                                                m_mainWindow->senderIdentitiesModel(),
                                                                                m_replyingToMessage, dummy));
        m_actionReplyModeAllButMe->setEnabled(Composer::Util::replyRecipientList(Composer::REPLY_ALL_BUT_ME,
                                                                                 m_mainWindow->senderIdentitiesModel(),
                                                                                 m_replyingToMessage, dummy));
        m_actionReplyModeAll->setEnabled(Composer::Util::replyRecipientList(Composer::REPLY_ALL,
                                                                            m_mainWindow->senderIdentitiesModel(),
                                                                            m_replyingToMessage, dummy));
        m_actionReplyModeList->setEnabled(Composer::Util::replyRecipientList(Composer::REPLY_LIST,
                                                                             m_mainWindow->senderIdentitiesModel(),
                                                                             m_replyingToMessage, dummy));
    } else {
        m_markButton->hide();
        m_replyModeButton->hide();
        m_actionInReplyTo->setToolTip(QString());
        m_actionStandalone->trigger();
    }

    int row = -1;
    bool ok = Composer::Util::chooseSenderIdentityForReply(m_mainWindow->senderIdentitiesModel(), replyingToMessage, row);
    if (ok) {
        Q_ASSERT(row >= 0 && row < m_mainWindow->senderIdentitiesModel()->rowCount());
        ui->envelopeWidget->ui->sender->setCurrentIndex(row);
    }

    slotUpdateSignature();
}

void ComposeWidget::gotError(const QString &error)
{
    QMessageBox::critical(this, tr("Failed to Send Mail"), error);
    setUiWidgetsEnabled(true);
}

void ComposeWidget::sent()
{
    // FIXME: move back to the currently selected mailbox

    m_sentMail = true;
    QTimer::singleShot(0, this, SLOT(close()));
}

void ComposeWidget::slotAskForFileAttachment()
{
    QString fileName = QFileDialog::getOpenFileName(this, tr("Attach File..."), QString(), QString(), 0,
                                                    QFileDialog::DontResolveSymlinks);
    if (!fileName.isEmpty()) {
        m_submission->composer()->addFileAttachment(fileName);
    }
}

void ComposeWidget::slotAttachFiles(QList<QUrl> urls)
{
    foreach (const QUrl &url, urls) {

#if QT_VERSION >= QT_VERSION_CHECK(4, 8, 0)
        if (url.isLocalFile()) {
#else
        if (url.scheme() == QLatin1String("file")) {
#endif
            m_submission->composer()->addFileAttachment(url.path());
        }
    }
}

void ComposeWidget::slotUpdateSignature()
{
    QAbstractProxyModel *proxy = qobject_cast<QAbstractProxyModel*>(ui->envelopeWidget->ui->sender->model());
    Q_ASSERT(proxy);
    QModelIndex proxyIndex = ui->envelopeWidget->ui->sender->model()->index(ui->envelopeWidget->ui->sender->currentIndex(), 0, ui->envelopeWidget->ui->sender->rootModelIndex());

    if (!proxyIndex.isValid()) {
        // This happens when the settings dialog gets closed and the SenderIdentitiesModel reloads data from the on-disk cache
        return;
    }

    QString newSignature = proxy->mapToSource(proxyIndex).sibling(proxyIndex.row(),
                                                                  Composer::SenderIdentitiesModel::COLUMN_SIGNATURE)
            .data().toString();

    const bool wasEdited = m_messageEverEdited;
    Composer::Util::replaceSignature(ui->mailText->document(), newSignature);
    m_messageEverEdited = wasEdited;
}

/** @short Massage the list of recipients so that they match the desired type of reply

In case of an error, the original list of recipients is left as is.
*/
bool ComposeWidget::setReplyMode(const Composer::ReplyMode mode)
{
    if (!m_replyingToMessage.isValid())
        return false;

    // Determine the new list of recipients
    Composer::RecipientList list;
    if (!Composer::Util::replyRecipientList(mode, m_mainWindow->senderIdentitiesModel(),
                                            m_replyingToMessage, list)) {
        return false;
    }

    while (!ui->envelopeWidget->recipients()->isEmpty())
        ui->envelopeWidget->removeRecipient(0);

    Q_FOREACH(Composer::RecipientList::value_type recipient, list) {
        if (!recipient.second.hasUsefulDisplayName())
            recipient.second.name.clear();
        ui->envelopeWidget->addRecipient(ui->envelopeWidget->recipients()->size(), recipient.first, recipient.second.asPrettyString());
    }

    ui->envelopeWidget->updateRecipientList();

    switch (mode) {
    case Composer::REPLY_PRIVATE:
        m_actionReplyModePrivate->setChecked(true);
        break;
    case Composer::REPLY_ALL_BUT_ME:
        m_actionReplyModeAllButMe->setChecked(true);
        break;
    case Composer::REPLY_ALL:
        m_actionReplyModeAll->setChecked(true);
        break;
    case Composer::REPLY_LIST:
        m_actionReplyModeList->setChecked(true);
        break;
    }

    m_replyModeButton->setText(m_replyModeActions->checkedAction()->text());
    m_replyModeButton->setIcon(m_replyModeActions->checkedAction()->icon());

    ui->mailText->setFocus();

    return true;
}

/** local draft serializaton:
 * Version (int)
 * Whether this draft was stored explicitly (bool)
 * The sender (QString)
 * Amount of recipients (int)
 * n * (RecipientKind ("int") + recipient (QString))
 * Subject (QString)
 * The message text (QString)
 */

void ComposeWidget::saveDraft(const QString &path)
{
    static const int trojitaDraftVersion = 3;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return; // TODO: error message?
    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_4_6);
    stream << trojitaDraftVersion << m_explicitDraft << ui->envelopeWidget->ui->sender->currentText();
    stream << ui->envelopeWidget->recipients()->count();
    for (int i = 0; i < ui->envelopeWidget->recipients()->count(); ++i) {
        stream << ui->envelopeWidget->recipients()->at(i).first->itemData(ui->envelopeWidget->recipients()->at(i).first->currentIndex()).toInt();
        stream << ui->envelopeWidget->recipients()->at(i).second->text();
    }
    stream << m_submission->composer()->timestamp() << m_inReplyTo << m_references;
    stream << m_actionInReplyTo->isChecked();
    stream << ui->envelopeWidget->ui->subject->text();
    stream << ui->mailText->toPlainText();
    // we spare attachments
    // a) serializing isn't an option, they could be HUUUGE
    // b) storing urls only works for urls
    // c) the data behind the url or the url validity might have changed
    // d) nasty part is writing mails - DnD a file into it is not a problem
    file.close();
    file.setPermissions(QFile::ReadOwner|QFile::WriteOwner);
}

/**
 * When loading a draft we omit the present autostorage (content is replaced anyway) and make
 * the loaded path the autosave path, so all further automatic storage goes into the present
 * draft file
 */

void ComposeWidget::loadDraft(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;

    if (m_autoSavePath != path) {
        QFile::remove(m_autoSavePath);
        m_autoSavePath = path;
    }

    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_4_6);
    QString string;
    int version, recipientCount;
    stream >> version;
    stream >> m_explicitDraft;
    stream >> string >> recipientCount; // sender / amount of recipients
    int senderIndex = ui->envelopeWidget->ui->sender->findText(string);
    if (senderIndex != -1) {
        ui->envelopeWidget->ui->sender->setCurrentIndex(senderIndex);
    } else {
        ui->envelopeWidget->ui->sender->setEditText(string);
    }
    for (int i = 0; i < recipientCount; ++i) {
        int kind;
        stream >> kind >> string;
        if (!string.isEmpty())
            ui->envelopeWidget->addRecipient(i, static_cast<Composer::RecipientKind>(kind), string);
    }
    if (version >= 2) {
        QDateTime timestamp;
        stream >> timestamp >> m_inReplyTo >> m_references;
        m_submission->composer()->setTimestamp(timestamp);
        if (!m_inReplyTo.isEmpty()) {
            m_markButton->show();
            // FIXME: in-reply-to's validitiy isn't the best check for showing or not showing the reply mode.
            // For eg: consider cases of mailto, forward, where valid in-reply-to won't mean choice of reply modes.
            m_replyModeButton->show();

            m_actionReplyModeAll->setEnabled(false);
            m_actionReplyModeAllButMe->setEnabled(false);
            m_actionReplyModeList->setEnabled(false);
            m_actionReplyModePrivate->setEnabled(false);
            markReplyModeHandpicked();

            // We do not have the message index at this point, but we can at least show the Message-Id here
            QStringList inReplyTo;
            Q_FOREACH(auto item, m_inReplyTo) {
                // There's no HTML escaping to worry about
                inReplyTo << QLatin1Char('<') + QString::fromUtf8(item.constData()) + QLatin1Char('>');
            }
            m_actionInReplyTo->setToolTip(tr("This mail will be marked as a response<hr/>%1").arg(
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
                                              inReplyTo.join(tr("<br/>")).toHtmlEscaped()
#else
                                              Qt::escape(inReplyTo.join(tr("<br/>")))
#endif
                                              ));
            if (version == 2) {
                // it is always marked as a reply in v2
                m_actionInReplyTo->trigger();
            }
        }
    }
    if (version >= 3) {
        bool replyChecked;
        stream >> replyChecked;
        // Got to use trigger() so that the default action of the QToolButton is updated
        if (replyChecked) {
            m_actionInReplyTo->trigger();
        } else {
            m_actionStandalone->trigger();
        }
    }
    stream >> string;
    ui->envelopeWidget->ui->subject->setText(string);
    stream >> string;
    ui->mailText->setPlainText(string);
    m_messageUpdated = false; // this is now the most up-to-date one
    file.close();
}

void ComposeWidget::autoSaveDraft()
{
    if (m_messageUpdated) {
        m_messageUpdated = false;
        saveDraft(m_autoSavePath);
    }
}

void ComposeWidget::setMessageUpdated()
{
    m_messageEverEdited = m_messageUpdated = true;
}

void ComposeWidget::updateWindowTitle()
{
    if (ui->envelopeWidget->ui->subject->text().isEmpty()) {
        setWindowTitle(tr("Compose Mail"));
    } else {
        setWindowTitle(tr("%1 - Compose Mail").arg(ui->envelopeWidget->ui->subject->text()));
    }
}

void ComposeWidget::toggleReplyMarking()
{
    (m_actionInReplyTo->isChecked() ? m_actionStandalone : m_actionInReplyTo)->trigger();
}

void ComposeWidget::updateReplyMarkingAction()
{
    auto action = m_markAsReply->checkedAction();
    m_actionToggleMarking->setText(action->text());
    m_actionToggleMarking->setIcon(action->icon());
    m_actionToggleMarking->setToolTip(action->toolTip());
}

}
