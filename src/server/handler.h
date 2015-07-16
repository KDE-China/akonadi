/***************************************************************************
 *   Copyright (C) 2006 by Till Adam <adam@kde.org>                        *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library General Public License as       *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this program; if not, write to the                 *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.             *
 ***************************************************************************/
#ifndef AKONADIHANDLER_H
#define AKONADIHANDLER_H

#include <QtCore/QObject>
#include <QtCore/QDataStream>

#include "global.h"
#include "exception.h"
#include "connection.h"

#include <private/protocol_p.h>

namespace Akonadi {

class ImapSet;

namespace Server {

class Response;
class Connection;
class QueryBuilder;
class ImapStreamParser;

AKONADI_EXCEPTION_MAKE_INSTANCE(HandlerException);

/**
  \defgroup akonadi_server_handler Command handlers

  All commands supported by the Akonadi server are implemented as sub-classes of Akonadi::Handler.
*/

/**
The handler interfaces describes an entity capable of handling an AkonadiIMAP command.*/
class Handler : public QObject
{
    Q_OBJECT
public:
    Handler();

    virtual ~Handler();

    /**
     * Set the tag of the command to be processed, and thus of the response
     * generated by this handler.
     * @param tag The command tag, an alphanumerical string, normally.
     */
    void setTag(quint64 tag);

    /**
     * The tag of the command associated with this handler.
     */
    quint64 tag() const;

    void setCommand(const Protocol::Command &cmd);

    Protocol::Command command() const;

    /**
     * Find a handler for a command that is always allowed, like LOGOUT.
     * @param line the command string
     * @return an instance to the handler. The handler is deleted after @see handelLine is executed. The caller needs to delete the handler in case an exception is thrown from handelLine.
     */
    static Handler *findHandlerForCommandAlwaysAllowed(Protocol::Command::Type cmd);

    /**
     * Find a handler for a command that is allowed when the client is not yet authenticated, like LOGIN.
     * @param line the command string
     * @return an instance to the handler. The handler is deleted after @see handelLine is executed. The caller needs to delete the handler in case an exception is thrown from handelLine.
     */
    static Handler *findHandlerForCommandNonAuthenticated(Protocol::Command::Type cmd);

    /**
     * Find a handler for a command that is allowed when the client is authenticated, like LIST, FETCH, etc.
     * @param line the command string
     * @return an instance to the handler. The handler is deleted after @see handelLine is executed. The caller needs to delete the handler in case an exception is thrown from handelLine.
     */
    static Handler *findHandlerForCommandAuthenticated(Protocol::Command::Type cmd);

    void setConnection(Connection *connection);
    Connection *connection() const;

    bool failureResponse(const char *response);
    bool failureResponse(const QByteArray &response);
    bool failureResponse(const QString &response);

    template<typename T>
    typename std::enable_if<std::is_base_of<Protocol::Command, T>::value, bool>::type
    successResponse(const T &response = T());

    template<typename T>
    typename std::enable_if<std::is_base_of<Protocol::Command, T>::value, void>::type
    sendResponse(const T&response = T());


    /**
     * Parse and handle the IMAP message using the streaming parser. The implementation MUST leave the trailing newline character(s) in the stream!
     * @return true if parsed successfully, false in case of parse failure
     */
    virtual bool parseStream() = 0;

    bool checkScopeConstraints(const Scope &scope, int permittedScopes);

public Q_SLOTS:
    void sendResponse(const Protocol::Command &response);

Q_SIGNALS:
    /**
     * Emitted whenever a handler wants the connection to change into a
     * different state. The connection usually honors such requests, but
     * the decision is up to it.
     * @param state The new state the handler suggests to enter.
     */
    void connectionStateChange(ConnectionState state);

private:
    quint64 m_tag;
    Connection *m_connection;

protected:
    Protocol::Command m_command;
};

template<typename T>
typename std::enable_if<std::is_base_of<Protocol::Command, T>::value, bool>::type
Handler::successResponse(const T &response)
{
    sendResponse(response);
    return true;
}

template<typename T>
typename std::enable_if<std::is_base_of<Protocol::Command, T>::value, void>::type
Handler::sendResponse(const T &response)
{
    sendResponse(static_cast<const Protocol::Command&>(response));
}

} // namespace Server
} // namespace Akonadi

#endif