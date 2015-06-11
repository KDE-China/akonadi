/*
 * Copyright (C) 2014  Daniel Vrátil <dvratil@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#ifndef FAKEAKONADISERVER_H
#define FAKEAKONADISERVER_H

#include "akonadi.h"
#include "exception.h"

#include <QSignalSpy>
#include <QBuffer>
#include <QDataStream>
#include <QtTest/QTest>

#include <type_traits>

#include <private/protocol_p.h>

class QLocalServer;
class QEventLoop;

namespace Akonadi {
namespace Server {

class FakeSearchManager;
class FakeDataStore;
class FakeConnection;
class FakeClient;

class TestScenario {
public:
    typedef QList<TestScenario> List;

    enum Action {
        ServerCmd,
        ClientCmd,
        Wait,
        Quit,
        Ignore
    };

    Action action;
    QByteArray data;

    static TestScenario create(qint64 tag, Action action, const Protocol::Command &response);

    static TestScenario wait(int timeout)
    {
        return TestScenario { Wait, QByteArray::number(timeout) };
    }

    static TestScenario quit()
    {
        return TestScenario { Quit, QByteArray() };
    }

    static TestScenario ignore(int count)
    {
        return TestScenario { Ignore, QByteArray::number(count) };
    }
};

/**
 * A fake server used for testing. Losely based on KIMAP::FakeServer
 */
class FakeAkonadiServer : public AkonadiServer
{
    Q_OBJECT

public:
    static FakeAkonadiServer *instance();

    ~FakeAkonadiServer();

    /* Reimpl */
    bool init();

    /* Reimpl */
    bool quit();

    FakeDataStore *dataStore() const;

    static QString basePath();
    static QString socketFile();
    static QString instanceName();

    static TestScenario::List loginScenario();
    static TestScenario::List selectCollectionScenario(const QString &name);
    static TestScenario::List selectResourceScenario(const QString &name);

    void setScenarios(const TestScenario::List &scenarios);

    void runTest();

    QSignalSpy *notificationSpy() const;

    void setPopulateDb(bool populate);

protected:
    /* Reimpl */
    void incomingConnection(quintptr socketDescriptor);

private:
    explicit FakeAkonadiServer();

    bool deleteDirectory(const QString &path);

    FakeDataStore *mDataStore;
    FakeSearchManager *mSearchManager;
    FakeConnection *mConnection;
    FakeClient *mClient;

    QEventLoop *mServerLoop;

    QSignalSpy *mNotificationSpy;

    bool mPopulateDb;

    static FakeAkonadiServer *sInstance;
};

AKONADI_EXCEPTION_MAKE_INSTANCE(FakeAkonadiServerException);

}
}

Q_DECLARE_METATYPE(Akonadi::Server::TestScenario)
Q_DECLARE_METATYPE(Akonadi::Server::TestScenario::List)

#endif // FAKEAKONADISERVER_H
