#include "EnvelopeFormWidget.h"
#include "ui_EnvelopeFormWidget.h"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QGraphicsOpacityEffect>
#include <QKeyEvent>
#include <QMenu>
#include <QPropertyAnimation>
#include <QTimer>
#include "Common/InvokeMethod.h"
#include "Composer/SenderIdentitiesModel.h"
#include "Gui/AbstractAddressbook.h"
#include "Gui/AutoCompletion.h"
#include "Gui/ComposeWidget.h"
#include "Gui/FromAddressProxyModel.h"
#include "Gui/LineEdit.h"
#include "Gui/Window.h"
#include "UiUtils/Color.h"

static int actualRow(QFormLayout *form, int row);
static QWidget* formPredecessor(QFormLayout *form, QWidget *w);
static inline Composer::RecipientKind currentRecipient(const QComboBox *box);
static const QString trojita_opacityAnimation = QLatin1String("trojita_opacityAnimation");
enum { OFFSET_OF_FIRST_ADDRESSEE = 1, MIN_MAX_VISIBLE_RECIPIENTS = 4 };

EnvelopeFormWidget::EnvelopeFormWidget(QWidget *parent) :
    QWidget(parent), ui(new Ui::EnvelopeFormWidget), m_maxVisibleRecipients(MIN_MAX_VISIBLE_RECIPIENTS)
{
    ui->setupUi(this);

    m_completionPopup = new QMenu(this);
    m_completionPopup->installEventFilter(this);
    connect(m_completionPopup, SIGNAL(triggered(QAction*)), SLOT(completeRecipient(QAction*)));
    // TODO: make this configurable?
    m_completionCount = 8;

    m_recipientListUpdateTimer = new QTimer(this);
    m_recipientListUpdateTimer->setSingleShot(true);
    m_recipientListUpdateTimer->setInterval(250);
    connect(m_recipientListUpdateTimer, SIGNAL(timeout()), SLOT(updateRecipientList()));

    connect(ui->recipientSlider, SIGNAL(valueChanged(int)), SLOT(scrollRecipients(int)));
    connect(qApp, SIGNAL(focusChanged(QWidget*,QWidget*)), SLOT(handleFocusChange()));
    ui->recipientSlider->setMinimum(0);
    ui->recipientSlider->setMaximum(0);
    ui->recipientSlider->setVisible(false);
    installEventFilter(this);
}

EnvelopeFormWidget::~EnvelopeFormWidget()
{
    delete ui;
}

void EnvelopeFormWidget::setData(Gui::MainWindow *mainWindow)
{
    m_mainWindow = mainWindow;

    Gui::FromAddressProxyModel *proxy = new Gui::FromAddressProxyModel(this);
    proxy->setSourceModel(m_mainWindow->senderIdentitiesModel());
    ui->sender->setModel(proxy);
    connect(ui->sender->lineEdit(), SIGNAL(textChanged(QString)), SLOT(slotCheckAddress()));
}

void EnvelopeFormWidget::addRecipient(int position, Composer::RecipientKind kind, const QString &address)
{
    QComboBox *combo = new QComboBox(this);
    combo->addItem(tr("To"), Composer::ADDRESS_TO);
    combo->addItem(tr("Cc"), Composer::ADDRESS_CC);
    combo->addItem(tr("Bcc"), Composer::ADDRESS_BCC);
    combo->setCurrentIndex(combo->findData(kind));
    LineEdit *edit = new LineEdit(address, this);
    slotCheckAddress(edit);
    connect(edit, SIGNAL(textChanged(QString)), this, SIGNAL(recipientTextChanged(QString)));
    connect(edit, SIGNAL(textChanged(QString)), this, SLOT(slotCheckAddress()));
    connect(edit, SIGNAL(textEdited(QString)), SLOT(completeRecipients(QString)));
    connect(edit, SIGNAL(editingFinished()), SLOT(collapseRecipients()));
    connect(edit, SIGNAL(textChanged(QString)), m_recipientListUpdateTimer, SLOT(start()));
    m_recipients.insert(position, Recipient(combo, edit));
    setUpdatesEnabled(false);
    ui->envelopeLayout->insertRow(actualRow(ui->envelopeLayout, position + OFFSET_OF_FIRST_ADDRESSEE), combo, edit);
    setTabOrder(formPredecessor(ui->envelopeLayout, combo), combo);
    setTabOrder(combo, edit);
    const int max = qMax(0, m_recipients.count() - m_maxVisibleRecipients);

    ui->recipientSlider->setMaximum(max);
    ui->recipientSlider->setVisible(max > 0);
    if (ui->recipientSlider->isVisible()) {
        const int v = ui->recipientSlider->value();
        int keepInSight = ++position;
        for (int i = 0; i < m_recipients.count(); ++i) {
            if (m_recipients.at(i).first->hasFocus() || m_recipients.at(i).second->hasFocus()) {
                keepInSight = i;
                break;
            }
        }
        if (qAbs(keepInSight - position) < m_maxVisibleRecipients)
            ui->recipientSlider->setValue(position*max/m_recipients.count());
        if (v == ui->recipientSlider->value()) // force scroll update
            scrollRecipients(v);
        Q_UNUSED(v);
    }
    setUpdatesEnabled(true);
}

void EnvelopeFormWidget::completeRecipients(const QString &text)
{
    if (text.isEmpty()) {
        if (m_completionPopup) {
            // if there's a popup close it and set back the receiver
            m_completionPopup->close();
            m_completionReceiver = 0;
        }
        return; // we do not suggest "nothing"
    }
    Q_ASSERT(sender());
    QLineEdit *toEdit = static_cast<QLineEdit*>(sender());
    QStringList contacts = m_mainWindow->addressBook()->complete(text, QStringList(), m_completionCount);
    if (contacts.isEmpty() && m_completionPopup) {
        m_completionPopup->close();
        m_completionReceiver = 0;
    } else {
        m_completionReceiver = toEdit;
        m_completionPopup->setUpdatesEnabled(false);
        m_completionPopup->clear();
        Q_FOREACH(const QString &s, contacts)
            m_completionPopup->addAction(s);
        if (m_completionPopup->isHidden())
            m_completionPopup->popup(toEdit->mapToGlobal(QPoint(0, toEdit->height())));
        m_completionPopup->setUpdatesEnabled(true);
    }
}

void EnvelopeFormWidget::completeRecipient(QAction *act)
{
    if (act->text().isEmpty())
        return;
    m_completionReceiver->setText(act->text());
    if (m_completionPopup) {
        m_completionPopup->close();
        m_completionReceiver = 0;
    }
}

bool EnvelopeFormWidget::eventFilter(QObject *o, QEvent *e)
{
    if (o == m_completionPopup) {
        if (e->type() == QEvent::KeyPress || e->type() == QEvent::KeyRelease) {
            QKeyEvent *ke = static_cast<QKeyEvent*>(e);
            if (!(  ke->key() == Qt::Key_Up || ke->key() == Qt::Key_Down || // Navigation
                    ke->key() == Qt::Key_Escape || // "escape"
                    ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter)) { // selection
                QCoreApplication::sendEvent(m_completionReceiver, e);
                return true;
            }
        }
        return false;
    }

    if (o == this)  {
        if (e->type() == QEvent::Wheel) {
            int v = ui->recipientSlider->value();
            if (static_cast<QWheelEvent*>(e)->delta() > 0)
                --v;
            else
                ++v;
            // just QApplication::sendEvent(ui->recipientSlider, e) will cause a recursion if
            // ui->recipientSlider ignores the event (eg. because it would lead to an invalid value)
            // since ui->recipientSlider is child, my guts tell me to not send events
            // to children if it can be avoided, but its just a gut feeling
            ui->recipientSlider->setValue(v);
            e->accept();
            return true;
        }
        if (e->type() == QEvent::KeyPress && hasFocus()) {
            scrollToFocus();
            QWidget *focus = QApplication::focusWidget();
            if (focus && focus != this) {
                int key = static_cast<QKeyEvent*>(e)->key();
                if (!(key == Qt::Key_Tab || key == Qt::Key_Backtab)) // those alter the focus again
                    QApplication::sendEvent(focus, e);
            }
            return true;
        }
        if (e->type() == QEvent::Resize) {
            QResizeEvent *re = static_cast<QResizeEvent*>(e);
            if (re->size().height() != re->oldSize().height())
                calculateMaxVisibleRecipients();
            return false;
        }
        return false;
    }
    return false;
}

void EnvelopeFormWidget::slotCheckAddress()
{
    QLineEdit *edit = qobject_cast<QLineEdit*>(sender());
    Q_ASSERT(edit);
    slotCheckAddress(edit);
}

void EnvelopeFormWidget::slotCheckAddress(QLineEdit *edit)
{
    Imap::Message::MailAddress addr;
    if (edit->text().isEmpty() || Imap::Message::MailAddress::fromPrettyString(addr, edit->text())) {
        edit->setPalette(QPalette());
    } else {
        QPalette p;
        p.setColor(QPalette::Base, UiUtils::tintColor(p.color(QPalette::Base), QColor(0xff, 0, 0, 0x20)));
        edit->setPalette(p);
    }
}

void EnvelopeFormWidget::removeRecipient(int pos)
{
    // removing the widgets from the layout is important
    // a) not doing so leaks (minor)
    // b) deleteLater() crosses the evenchain and so our actualRow function would be tricked
    QWidget *formerFocus = QApplication::focusWidget();
    if (!formerFocus)
        formerFocus = m_lastFocusedRecipient;

    if (pos + 1 < m_recipients.count()) {
        if (m_recipients.at(pos).first == formerFocus) {
            m_recipients.at(pos + 1).first->setFocus();
            formerFocus = m_recipients.at(pos + 1).first;
        } else if (m_recipients.at(pos).second == formerFocus) {
            m_recipients.at(pos + 1).second->setFocus();
            formerFocus = m_recipients.at(pos + 1).second;
        }
    } else if (m_recipients.at(pos).first == formerFocus || m_recipients.at(pos).second == formerFocus) {
            formerFocus = 0;
    }

    ui->envelopeLayout->removeWidget(m_recipients.at(pos).first);
    ui->envelopeLayout->removeWidget(m_recipients.at(pos).second);
    m_recipients.at(pos).first->deleteLater();
    m_recipients.at(pos).second->deleteLater();
    m_recipients.removeAt(pos);
    const int max = qMax(0, m_recipients.count() - m_maxVisibleRecipients);
    ui->recipientSlider->setMaximum(max);
    ui->recipientSlider->setVisible(max > 0);
    if (formerFocus) {
        // skip event loop, remove might be triggered by imminent focus loss
        CALL_LATER_NOARG(formerFocus, setFocus);
    }
}

void EnvelopeFormWidget::handleFocusChange()
{
    // got explicit focus on other widget - don't restore former focused recipient on scrolling
    m_lastFocusedRecipient = QApplication::focusWidget();

    if (m_lastFocusedRecipient)
        QTimer::singleShot(150, this, SLOT(scrollToFocus())); // give user chance to notice the focus change disposition
}

void EnvelopeFormWidget::scrollToFocus()
{
    if (!ui->recipientSlider->isVisible())
        return;

    QWidget *focus = QApplication::focusWidget();
    if (focus == this)
        focus = m_lastFocusedRecipient;
    if (!focus)
        return;

    // if this is the first or last visible recipient, show one more (to hint there's more and allow tab progression)
    for (int i = 0, pos = 0; i < m_recipients.count(); ++i) {
        if (m_recipients.at(i).first->isVisible())
            ++pos;
        if (focus == m_recipients.at(i).first || focus == m_recipients.at(i).second) {
            if (pos > 1 && pos < m_maxVisibleRecipients) // prev & next are in sight
                break;
            if (pos == 1)
                ui->recipientSlider->setValue(i - 1); // scroll to prev
            else
                ui->recipientSlider->setValue(i + 2 - m_maxVisibleRecipients);  // scroll to next
            break;
        }
    }
    if (focus == m_lastFocusedRecipient)
        focus->setFocus(); // in case we scrolled to m_lastFocusedRecipient
}

void EnvelopeFormWidget::scrollRecipients(int value)
{
    // ignore focus changes caused by "scrolling"
    disconnect(qApp, SIGNAL(focusChanged(QWidget*,QWidget*)), this, SLOT(handleFocusChange()));

    QList<QWidget*> visibleWidgets;
    for (int i = 0; i < m_recipients.count(); ++i) {
        // remove all widgets from the form because of vspacing - causes spurious padding

        QWidget *toCC = m_recipients.at(i).first;
        QWidget *lineEdit = m_recipients.at(i).second;
        if (!m_lastFocusedRecipient) { // apply only _once_
            if (toCC->hasFocus())
                m_lastFocusedRecipient = toCC;
            else if (lineEdit->hasFocus())
                m_lastFocusedRecipient = lineEdit;
        }
        if (toCC->isVisible())
            visibleWidgets << toCC;
        if (lineEdit->isVisible())
            visibleWidgets << lineEdit;
        ui->envelopeLayout->removeWidget(toCC);
        ui->envelopeLayout->removeWidget(lineEdit);
        toCC->hide();
        lineEdit->hide();
    }

    const int begin = qMin(m_recipients.count(), value);
    const int end   = qMin(m_recipients.count(), value + m_maxVisibleRecipients);
    for (int i = begin, j = 0; i < end; ++i, ++j) {
        const int pos = actualRow(ui->envelopeLayout, j + OFFSET_OF_FIRST_ADDRESSEE);
        QWidget *toCC = m_recipients.at(i).first;
        QWidget *lineEdit = m_recipients.at(i).second;
        ui->envelopeLayout->insertRow(pos, toCC, lineEdit);
        if (!visibleWidgets.contains(toCC))
            fadeIn(toCC);
        visibleWidgets.removeOne(toCC);
        if (!visibleWidgets.contains(lineEdit))
            fadeIn(lineEdit);
        visibleWidgets.removeOne(lineEdit);
        toCC->show();
        lineEdit->show();
        setTabOrder(formPredecessor(ui->envelopeLayout, toCC), toCC);
        setTabOrder(toCC, lineEdit);
        if (toCC == m_lastFocusedRecipient)
            toCC->setFocus();
        else if (lineEdit == m_lastFocusedRecipient)
            lineEdit->setFocus();
    }

    if (m_lastFocusedRecipient && !m_lastFocusedRecipient->hasFocus() && QApplication::focusWidget())
        setFocus();

    Q_FOREACH (QWidget *w, visibleWidgets) {
        // was visible, is no longer -> stop animation so it won't conflict later ones
        w->setGraphicsEffect(0); // deletes old one
        if (QPropertyAnimation *pa = w->findChild<QPropertyAnimation*>(trojita_opacityAnimation))
            pa->stop();
    }
    connect(qApp, SIGNAL(focusChanged(QWidget*,QWidget*)), SLOT(handleFocusChange()));
}

void EnvelopeFormWidget::fadeIn(QWidget *w)
{
    QGraphicsOpacityEffect *effect = new QGraphicsOpacityEffect(w);
    w->setGraphicsEffect(effect);
    QPropertyAnimation *animation = new QPropertyAnimation(effect, "opacity", w);
    connect(animation, SIGNAL(finished()), SLOT(slotFadeFinished()));
    animation->setObjectName(trojita_opacityAnimation);
    animation->setDuration(333);
    animation->setStartValue(0.0);
    animation->setEndValue(1.0);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

void EnvelopeFormWidget::slotFadeFinished()
{
    Q_ASSERT(sender());
    QWidget *animatedEffectWidget = qobject_cast<QWidget*>(sender()->parent());
    Q_ASSERT(animatedEffectWidget);
    animatedEffectWidget->setGraphicsEffect(0); // deletes old one
}

void EnvelopeFormWidget::collapseRecipients()
{
    QLineEdit *edit = qobject_cast<QLineEdit*>(sender());
    Q_ASSERT(edit);
    if (edit->hasFocus() || !edit->text().isEmpty())
        return; // nothing to clean up

    // an empty recipient line just lost focus -> we "place it at the end", ie. simply remove it
    // and append a clone
    bool needEmpty = false;
    Composer::RecipientKind carriedKind = recipientKindForNextRow(Composer::ADDRESS_TO);
    for (int i = 0; i < m_recipients.count() - 1; ++i) { // sic! on the -1, no action if it trails anyway
        if (m_recipients.at(i).second == edit) {
            carriedKind = currentRecipient(m_recipients.last().first);
            removeRecipient(i);
            needEmpty = true;
            break;
        }
    }
    if (needEmpty)
        addRecipient(m_recipients.count(), carriedKind, QString());
}

/** @short Find out what type of recipient to use for the last row */
Composer::RecipientKind EnvelopeFormWidget::recipientKindForNextRow(const Composer::RecipientKind kind)
{
    using namespace Imap::Mailbox;
    switch (kind) {
    case Composer::ADDRESS_TO:
        // Heuristic: if the last one is "to", chances are that the next one shall not be "to" as well.
        // Cc is reasonable here.
        return Composer::ADDRESS_CC;
    case Composer::ADDRESS_CC:
    case Composer::ADDRESS_BCC:
        // In any other case, it is probably better to just reuse the type of the last row
        return kind;
    case Composer::ADDRESS_FROM:
    case Composer::ADDRESS_SENDER:
    case Composer::ADDRESS_REPLY_TO:
        // shall never be used here
        Q_ASSERT(false);
        return kind;
    }
    Q_ASSERT(false);
    return Composer::ADDRESS_TO;
}

void EnvelopeFormWidget::calculateMaxVisibleRecipients()
{
    const int oldMaxVisibleRecipients = m_maxVisibleRecipients;
    int spacing, bottom;
    ui->envelopeLayout->getContentsMargins(&spacing, &spacing, &spacing, &bottom);
    // we abuse the fact that there's always an addressee and that they all look the same
    QRect itemRects[2];
    for (int i = 0; i < 2; ++i) {
        if (QLayoutItem *li = ui->envelopeLayout->itemAt(OFFSET_OF_FIRST_ADDRESSEE - i, QFormLayout::LabelRole)) {
            itemRects[i] |= li->geometry();
        }
        if (QLayoutItem *li = ui->envelopeLayout->itemAt(OFFSET_OF_FIRST_ADDRESSEE - i, QFormLayout::FieldRole)) {
            itemRects[i] |= li->geometry();
        }
        if (QLayoutItem *li = ui->envelopeLayout->itemAt(OFFSET_OF_FIRST_ADDRESSEE - i, QFormLayout::SpanningRole)) {
            itemRects[i] |= li->geometry();
        }
    }
    int itemHeight = itemRects[0].height();
    spacing = qMax(0, itemRects[0].top() - itemRects[1].bottom() - 1); // QFormLayout::[vertical]spacing() is useless ...
    int firstTop = itemRects[0].top();
    const int subjectHeight =  ui->subject->height();
    const int height = this->height() -
                       firstTop - // offset of first recipient
                       (subjectHeight + spacing) - // for the subject
                       bottom - // layout bottom padding
                       2; // extra pixels padding to detect that the user wants to shrink
    if (itemHeight + spacing == 0) {
        m_maxVisibleRecipients = MIN_MAX_VISIBLE_RECIPIENTS;
    } else {
        m_maxVisibleRecipients = height / (itemHeight + spacing);
    }
    if (m_maxVisibleRecipients < MIN_MAX_VISIBLE_RECIPIENTS)
        m_maxVisibleRecipients = MIN_MAX_VISIBLE_RECIPIENTS; // allow up to 4 recipients w/o need for a sliding
    if (oldMaxVisibleRecipients != m_maxVisibleRecipients) {
        const int max = qMax(0, m_recipients.count() - m_maxVisibleRecipients);
        int v = qRound(1.0f*(ui->recipientSlider->value()*m_maxVisibleRecipients)/oldMaxVisibleRecipients);
        ui->recipientSlider->setMaximum(max);
        ui->recipientSlider->setVisible(max > 0);
        scrollRecipients(qMin(qMax(0, v), max));
    }
}

void EnvelopeFormWidget::updateRecipientList()
{
    // we ensure there's always one empty available
    bool haveEmpty = false;
    for (int i = 0; i < m_recipients.count(); ++i) {
        if (m_recipients.at(i).second->text().isEmpty()) {
            if (haveEmpty) {
                removeRecipient(i);
            }
            haveEmpty = true;
        }
    }
    if (!haveEmpty) {
        addRecipient(m_recipients.count(),
                     m_recipients.isEmpty() ?
                         Composer::ADDRESS_TO :
                         recipientKindForNextRow(currentRecipient(m_recipients.last().first)),
                     QString());
    }
}

bool EnvelopeFormWidget::parseRecipients(QList<QPair<Composer::RecipientKind, Imap::Message::MailAddress> > &results, QString &errorMessage)
{
    for (int i = 0; i < m_recipients.size(); ++i) {
        Composer::RecipientKind kind = currentRecipient(m_recipients.at(i).first);

        QString text = m_recipients.at(i).second->text();
        if (text.isEmpty())
            continue;
        Imap::Message::MailAddress addr;
        bool ok = Imap::Message::MailAddress::fromPrettyString(addr, text);
        if (ok) {
            // TODO: should we *really* learn every junk entered into a recipient field?
            // m_mainWindow->addressBook()->learn(addr);
            results << qMakePair(kind, addr);
        } else {
            errorMessage = tr("Can't parse \"%1\" as an e-mail address.").arg(text);
            return false;
        }
    }
    return true;
}

static inline Composer::RecipientKind currentRecipient(const QComboBox *box)
{
    return Composer::RecipientKind(box->itemData(box->currentIndex()).toInt());
}

//BEGIN QFormLayout workarounds

/** First issue: QFormLayout messes up rows by never removing them
 * ----------------------------------------------------------------
 * As a result insertRow(int pos, .) does not pick the expected row, but usually minor
 * (if you ever removed all items of a row in this layout)
 *
 * Solution: we count all rows non empty rows and when we have enough, return the row suitable for
 * QFormLayout (which is usually behind the requested one)
 */
static int actualRow(QFormLayout *form, int row)
{
    for (int i = 0, c = 0; i < form->rowCount(); ++i) {
        if (c == row) {
            return i;
        }
        if (form->itemAt(i, QFormLayout::LabelRole) || form->itemAt(i, QFormLayout::FieldRole) ||
            form->itemAt(i, QFormLayout::SpanningRole))
            ++c;
    }
    return form->rowCount(); // append
}

/** Second (related) issue: QFormLayout messes the tab order
 * ----------------------------------------------------------
 * "Inserted" rows just get appended to the present ones and by this to the tab focus order
 * It's therefore necessary to fix this forcing setTabOrder()
 *
 * Approach: traverse all rows until we have the widget that shall be inserted in tab order and
 * return it's predecessor
 */

static QWidget* formPredecessor(QFormLayout *form, QWidget *w)
{
    QWidget *pred = 0;
    QWidget *runner = 0;
    QLayoutItem *item = 0;
    for (int i = 0; i < form->rowCount(); ++i) {
        if ((item = form->itemAt(i, QFormLayout::LabelRole))) {
            runner = item->widget();
            if (runner == w)
                return pred;
            else if (runner)
                pred = runner;
        }
        if ((item = form->itemAt(i, QFormLayout::FieldRole))) {
            runner = item->widget();
            if (runner == w)
                return pred;
            else if (runner)
                pred = runner;
        }
        if ((item = form->itemAt(i, QFormLayout::SpanningRole))) {
            runner = item->widget();
            if (runner == w)
                return pred;
            else if (runner)
                pred = runner;
        }
    }
    return pred;
}

//END QFormLayout workarounds
