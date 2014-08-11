#ifndef ENVELOPEFORMWIDGET_H
#define ENVELOPEFORMWIDGET_H

#include <QPointer>
#include <QWidget>
#include "Composer/Recipients.h"

namespace Ui {
class EnvelopeFormWidget;
}

namespace Gui {
class ComposeWidget;
class MainWindow;
}

class QComboBox;
class QLineEdit;
class QMenu;
class QTimer;

typedef QPair<QComboBox*, QLineEdit*> Recipient;

class EnvelopeFormWidget : public QWidget
{
    Q_OBJECT

public:
    explicit EnvelopeFormWidget(QWidget *parent = 0);
    ~EnvelopeFormWidget();

    void setData(Gui::MainWindow *mainWindow);
    bool parseRecipients(QList<QPair<Composer::RecipientKind, Imap::Message::MailAddress> > &results, QString &errorMessage);
    void addRecipient(int position, Composer::RecipientKind kind, const QString &address);
    void removeRecipient(int pos);
    QList<Recipient> *recipients() { return &m_recipients; }

signals:
    void recipientTextChanged(QString text);

public slots:
    void updateRecipientList();
    void calculateMaxVisibleRecipients();

protected:
    bool eventFilter(QObject *o, QEvent *e);

private slots:
    void slotCheckAddress();
    void slotCheckAddress(QLineEdit *edit);

    void completeRecipient(QAction *act);
    void completeRecipients(const QString &text);

    void handleFocusChange();
    void scrollToFocus();
    void scrollRecipients(int);
    void collapseRecipients();

    void slotFadeFinished();

private:
    static Composer::RecipientKind recipientKindForNextRow(const Composer::RecipientKind kind);
    void fadeIn(QWidget *w);

    Ui::EnvelopeFormWidget *ui;

    Gui::ComposeWidget *m_composeWidget;
    Gui::MainWindow *m_mainWindow;

    QMenu *m_completionPopup;
    QLineEdit *m_completionReceiver;
    int m_completionCount;

    QList<Recipient> m_recipients;
    QPointer<QWidget> m_lastFocusedRecipient;
    int m_maxVisibleRecipients;
    QTimer *m_recipientListUpdateTimer;

    friend class Gui::ComposeWidget;
};

#endif // ENVELOPEFORMWIDGET_H
