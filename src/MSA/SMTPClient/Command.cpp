#include <ctype.h>
#include <QStringList>
#include "Command.h"

namespace Commands
{

QTextStream &operator<<(QTextStream &stream, const Command &cmd)
{
    for (QList<PartOfCommand>::const_iterator it = cmd.cmds.begin(); it != cmd.cmds.end(); ++it) {
        if (it != cmd.cmds.begin()) {
            stream << " ";
        }
        stream << *it;
    }
    return stream << endl;
}

QTextStream &operator<<(QTextStream &stream, const PartOfCommand &part)
{
    switch (part.kind) {
    case ATOM:
        stream << part.text;
        break;
    }
    return stream;
}

}
