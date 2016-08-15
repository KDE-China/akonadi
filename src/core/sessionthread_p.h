/*
    Copyright (c) 2016 Daniel Vrátil <dvratil@kde.org>

    This library is free software; you can redistribute it and/or modify it
    under the terms of the GNU Library General Public License as published by
    the Free Software Foundation; either version 2 of the License, or (at your
    option) any later version.

    This library is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Library General Public
    License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to the
    Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
    02110-1301, USA.
*/

#ifndef SESSIONTHREAD_P_H
#define SESSIONTHREAD_P_H

#include <QObject>
#include <QVector>

#include "connection_p.h"

namespace Akonadi
{

class SessionThread : public QObject
{
    Q_OBJECT

public:
    explicit SessionThread(QObject *parent = Q_NULLPTR);
    ~SessionThread();

    Connection *createConnection(Connection::ConnectionType connType, const QByteArray &sessionId);

private:
    Q_INVOKABLE Akonadi::Connection *doCreateConnection(Akonadi::Connection::ConnectionType connType,
                                                        const QByteArray &sessionId);

    Q_INVOKABLE void doThreadQuit();

private:
    QVector<Connection *> mConnections;
};

}


#endif
