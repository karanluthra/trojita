#ifndef SMTP_COMMAND_H
#define SMTP_COMMAND_H

#include <QByteArray>
#include <QTextStream>

namespace MSA {
class SMTPClient;
}

namespace Commands
{

enum TokenType {
    ATOM,
};

class PartOfCommand
{
    TokenType kind;
    QByteArray text;

    friend QTextStream &operator<<(QTextStream &stream, const PartOfCommand &c);
    friend class MSA::SMTPClient;

public:
    PartOfCommand(const TokenType kind, const QByteArray &text): kind(kind), text(text) {}
};

class Command
{
    friend QTextStream &operator<<(QTextStream &stream, const Command &c);
    QList<PartOfCommand> cmds;
    int currentPart;
    int m_tag;
    friend class MSA::SMTPClient;
public:
    Command &operator<<(const PartOfCommand &part) { cmds << part; return *this; }
    Command &operator<<(const QByteArray &text) { cmds << PartOfCommand(TokenType::ATOM, text); return *this; }
    Command(): currentPart(0) {}
    explicit Command(const QByteArray &name): currentPart(0) { cmds << PartOfCommand(ATOM, name); }
    void addTag(const int &tag) { m_tag = tag; }
};

QTextStream &operator<<(QTextStream &stream, const Command &cmd);

}

#endif // SMTP_COMMAND_H
