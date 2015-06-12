/***************************************************************************
 *   Copyright (C) 2006 by Till Adam <adam@kde.org>                        *
 *   Copyright (C) 2013 by Volker Krause <vkrause@kde.org>                 *
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
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.         *
 ***************************************************************************/
#include "connection.h"
#include "akonadiserver_debug.h"

#include <QtCore/QDebug>
#include <QtCore/QEventLoop>
#include <QtCore/QLatin1String>
#include <QSettings>

#include "storage/datastore.h"
#include "handler.h"

#include "tracer.h"
#include "clientcapabilityaggregator.h"
#include "collectionreferencemanager.h"

#include <shared/akdebug.h>
#include <shared/akcrash.h>
#include <shared/akstandarddirs.h>

#include <assert.h>

#include <private/protocol_p.h>

#define AKONADI_PROTOCOL_VERSION 50

using namespace Akonadi;
using namespace Akonadi::Server;

Connection::Connection(QObject *parent)
    : QObject(parent)
    , m_socketDescriptor(0)
    , m_socket(0)
    , m_currentHandler(0)
    , m_connectionState(NonAuthenticated)
    , m_backend(0)
    , m_verifyCacheOnRetrieval(false)
    , m_totalTime( 0 )
    , m_reportTime( false )
{
}

Connection::Connection(quintptr socketDescriptor, QObject *parent)
    : Connection(parent)
{
    m_socketDescriptor = socketDescriptor;
    m_identifier.sprintf("%p", static_cast<void *>(this));
    ClientCapabilityAggregator::addSession(m_clientCapabilities);

    const QSettings settings(AkStandardDirs::serverConfigFile(), QSettings::IniFormat);
    m_verifyCacheOnRetrieval = settings.value(QLatin1String("Cache/VerifyOnRetrieval"), m_verifyCacheOnRetrieval).toBool();

    QLocalSocket *socket = new QLocalSocket();

    if (!socket->setSocketDescriptor(m_socketDescriptor)) {
        qCWarning(AKONADISERVER_LOG) << "Connection(" << m_identifier
                   << ")::run: failed to set socket descriptor: "
                   << socket->error() << "(" << socket->errorString() << ")";
        delete socket;
        return;
    }

    m_socket = socket;

    /* Whenever a full command has been read, it is delegated to the responsible
     * handler and processed by that. If that command needs to do something
     * asynchronous such as ask the db for data, it returns and the input
     * queue can continue to be processed. Whenever there is something to
     * be sent back to the user it is queued in the form of a Response object.
     * All this is meant to make it possible to process large incoming or
     * outgoing data transfers in a streaming manner, without having to
     * hold them in memory 'en gros'. */

    connect(socket, SIGNAL(readyRead()),
            this, SLOT(slotNewData()));
    connect(socket, SIGNAL(disconnected()),
            this, SIGNAL(disconnected()));

    // don't send before the event loop is active, since waitForBytesWritten() can cause interesting reentrancy issues
    // TODO should be QueueConnection, but unfortunately that doesn't work (yet), since
    // "this" belongs to the wrong thread, but that requires a slightly larger refactoring
    QMetaObject::invokeMethod(this, "slotSendHello", Qt::DirectConnection);
}

void Connection::slotSendHello()
{
    sendResponse(0, Protocol::HelloResponse(QStringLiteral("Akonadi"),
                                            QStringLiteral("Not Really IMAP server"),
                                            protocolVersion()));
}

int Connection::protocolVersion()
{
    return AKONADI_PROTOCOL_VERSION;
}

DataStore *Connection::storageBackend()
{
    if (!m_backend) {
        m_backend = DataStore::self();
    }
    return m_backend;
}

CollectionReferenceManager *Connection::collectionReferenceManager()
{
    return CollectionReferenceManager::instance();
}

Connection::~Connection()
{
    delete m_socket;
    m_socket = 0;

    ClientCapabilityAggregator::removeSession(m_clientCapabilities);
    Tracer::self()->endConnection(m_identifier, QString());
    collectionReferenceManager()->removeSession(m_sessionId);

    if (m_reportTime) {
        reportTime();
    }
}

void Connection::slotNewData()
{
    // On Windows, calling readLiteralPart() triggers the readyRead() signal recursively and leads to parse errors
    if (m_currentHandler) {
        return;
    }

    QString currentCommand;
    while (m_socket->bytesAvailable() > 0) {
        QDataStream stream(m_socket);

        qint64 tag = -1;
        stream >> tag;
        // TODO: Check tag is incremental sequence

        Protocol::Command cmd = Protocol::deserialize(m_socket);

        // TODO: The resend-on-read-past-end is not really a solution, because
        // the same will probably happen again. We should send overall size of the
        // message as first argument and ensure we have buffered enough data.
        // That however requires some larger changes to the protocol and the way
        // we (de)serialize data.
        if (stream.status() == QDataStream::ReadCorruptData || stream.status() == QDataStream::ReadPastEnd) {
            // TODO: Create a proper Protocol::ResendDataResponse class
            Protocol::Response resp;
            resp.setError(1, QLatin1String("Corrupted data or incomplete data, please resend"));
            sendResponse(resp);
            return;
        }

        if (cmd.type() == Protocol::Command::Invalid) {
            // TODO: Don't so harsh, just send back an error
            slotConnectionStateChange(Server::LoggingOut);
            return;
        }

        // Tag context and collection context is not persistent.
        //        with SELECT job
        context()->setTag(-1);
        context()->setCollection(Collection());
        //Tracer::self()->connectionInput(m_identifier, (tag + ' ' + command + ' ' + m_streamParser->readRemainingData()));

        m_currentHandler = findHandlerForCommand(cmd.type());
        assert(m_currentHandler);
        if (m_reportTime) {
            startTime();
        }
        connect(m_currentHandler, SIGNAL(connectionStateChange(ConnectionState)),
                this, SLOT(slotConnectionStateChange(ConnectionState)),
                Qt::DirectConnection);

        m_currentHandler->setConnection(this);
        m_currentHandler->setTag(tag);
        m_currentHandler->setCommand(cmd);
        try {
            if (!m_currentHandler->parseStream()) {
                // TODO: What to do? How do we know we reached the end of command?
            }
        } catch (const Akonadi::Server::HandlerException &e) {
            if (m_currentHandler) {
                m_currentHandler->failureResponse(e.what());
            }
        } catch (const Akonadi::Server::Exception &e) {
            if (m_currentHandler) {
                m_currentHandler->failureResponse(QString::fromUtf8(e.type()) + QLatin1String(": ") + QString::fromUtf8(e.what()));
            }
        } catch (...) {
            akError() << "Unknown exception caught: " << akBacktrace();
            if (m_currentHandler) {
                m_currentHandler->failureResponse("Unknown exception caught");
            }
        }
        if (m_reportTime) {
            stopTime(currentCommand);
        }
        delete m_currentHandler;
        m_currentHandler = 0;
    }
}

CommandContext *Connection::context() const
{
    return const_cast<CommandContext *>(&m_context);
}

Handler *Connection::findHandlerForCommand(Protocol::Command::Type command)
{
    Handler *handler = Handler::findHandlerForCommandAlwaysAllowed(command);
    if (handler) {
        return handler;
    }

    switch (m_connectionState) {
    case NonAuthenticated:
        handler =  Handler::findHandlerForCommandNonAuthenticated(command);
        break;
    case Authenticated:
        handler =  Handler::findHandlerForCommandAuthenticated(command);
        break;
    case Selected:
        break;
    case LoggingOut:
        break;
    }

    return handler;
}

void Connection::slotConnectionStateChange(ConnectionState state)
{
    if (state == m_connectionState) {
        return;
    }
    m_connectionState = state;
    switch (m_connectionState) {
    case NonAuthenticated:
        assert(0);   // can't happen, it's only the initial state, we can't go back to it
        break;
    case Authenticated:
        break;
    case Selected:
        break;
    case LoggingOut:
        if (dynamic_cast<QLocalSocket *>(m_socket)) {
            dynamic_cast<QLocalSocket *>(m_socket)->disconnectFromServer();
        }
        break;
    }
}

void Connection::setSessionId(const QByteArray &id)
{
    m_identifier.sprintf("%s (%p)", id.data(), static_cast<void *>(this));
    Tracer::self()->beginConnection(m_identifier, QString());
    //m_streamParser->setTracerIdentifier(m_identifier);

    m_sessionId = id;
    setObjectName(QString::fromLatin1(id));
    storageBackend()->setSessionId(id);
    storageBackend()->notificationCollector()->setSessionId(id);
}

QByteArray Connection::sessionId() const
{
    return m_sessionId;
}

bool Connection::isOwnerResource(const PimItem &item) const
{
    if (context()->resource().isValid() && item.collection().resourceId() == context()->resource().id()) {
        return true;
    }
    // fallback for older resources
    if (sessionId() == item.collection().resource().name().toUtf8()) {
        return true;
    }
    return false;
}

bool Connection::isOwnerResource(const Collection &collection) const
{
    if (context()->resource().isValid() && collection.resourceId() == context()->resource().id()) {
        return true;
    }
    if (sessionId() == collection.resource().name().toUtf8()) {
        return true;
    }
    return false;
}

const ClientCapabilities &Connection::capabilities() const
{
    return m_clientCapabilities;
}

void Connection::setCapabilities(const ClientCapabilities &capabilities)
{
    ClientCapabilityAggregator::removeSession(m_clientCapabilities);
    m_clientCapabilities = capabilities;
    ClientCapabilityAggregator::addSession(m_clientCapabilities);
}

bool Connection::verifyCacheOnRetrieval() const
{
    return m_verifyCacheOnRetrieval;
}

void Connection::startTime()
{
    m_time.start();
}

void Connection::stopTime(const QString &identifier)
{
    int elapsed = m_time.elapsed();
    m_totalTime += elapsed;
    m_totalTimeByHandler[identifier] += elapsed;
    m_executionsByHandler[identifier]++;
    qCDebug(AKONADISERVER_LOG) << identifier <<" time : " << elapsed << " total: " << m_totalTime;
}

void Connection::reportTime() const
{
    qCDebug(AKONADISERVER_LOG) << "===== Time report for " << m_identifier << " =====";
    qCDebug(AKONADISERVER_LOG) << " total: " << m_totalTime;
    Q_FOREACH (const QString &handler, m_totalTimeByHandler.keys()) {
        qCDebug(AKONADISERVER_LOG) << "handler : " << handler << " time: " << m_totalTimeByHandler.value(handler) << " executions " << m_executionsByHandler.value(handler) << " avg: " << m_totalTimeByHandler.value(handler)/m_executionsByHandler.value(handler);
    }
}

void Connection::sendResponse(qint64 tag, const Protocol::Command &response)
{
    QDataStream stream(m_socket);
    stream << tag;
    Protocol::serialize(m_socket, response);
}

void Connection::sendResponse(const Protocol::Command &response)
{
    sendResponse(m_currentHandler->tag(), response);
}

Protocol::Command Connection::readCommand()
{
    m_socket->waitForReadyRead(500);

    QDataStream stream(m_socket);
    qint64 tag;
    stream >> tag;

    // TODO: compare tag with m_currentHandler->tag() ?
    return Protocol::deserialize(m_socket);
}
