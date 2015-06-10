/*
    Copyright (c) 2014 Christian Mollekopf <mollekopf@kolabsys.com>

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
#include <QObject>
#include <handler/create.h>
#include <response.h>
#include <storage/entity.h>

#include "fakeakonadiserver.h"
#include "dbinitializer.h"
#include "aktest.h"
#include "akdebug.h"
#include "entities.h"

#include <private/scope_p.h>

#include <QtTest/QTest>

using namespace Akonadi;
using namespace Akonadi::Server;

class CreateHandlerTest : public QObject
{
    Q_OBJECT

public:
    CreateHandlerTest()
    {
        qRegisterMetaType<Akonadi::Server::Response>();

        try {
            FakeAkonadiServer::instance()->init();
        } catch (const FakeAkonadiServerException &e) {
            akError() << "Server exception: " << e.what();
            akFatal() << "Fake Akonadi Server failed to start up, aborting test";
        }
    }

    ~CreateHandlerTest()
    {
        FakeAkonadiServer::instance()->quit();
    }

private Q_SLOTS:
    void testCreate_data()
    {
        DbInitializer dbInitializer;

        QTest::addColumn<TestScenario::List >("scenarios");
        QTest::addColumn<Akonadi::NotificationMessageV3>("notification");

        Akonadi::NotificationMessageV3 notificationTemplate;
        notificationTemplate.setType(NotificationMessageV2::Collections);
        notificationTemplate.setOperation(NotificationMessageV2::Add);
        notificationTemplate.setParentCollection(3);
        notificationTemplate.setResource("akonadi_fake_resource_0");
        notificationTemplate.setSessionId(FakeAkonadiServer::instanceName().toLatin1());

        {
            Protocol::CreateCollectionCommand cmd;
            cmd.setName(QLatin1String("New Name"));
            cmd.setParent(Scope(3));
            cmd.setAttributes({ { "MYRANDOMATTRIBUTE", "" } });

            Protocol::FetchCollectionsResponse resp(8);
            resp.setName(QLatin1String("New Name"));
            resp.setParentId(3);
            resp.setAttributes({ { "MYRANDOMATTRIBUTE", "" } });
            resp.setResource(QLatin1String("akonadi_fake_resource_0"));
            resp.cachePolicy().setLocalParts({ QLatin1String("ALL") });
            resp.setMimeTypes({ QLatin1String("application/octet-stream"),
                                QLatin1String("inode/directory") });

            TestScenario::List scenarios;
            scenarios << FakeAkonadiServer::loginScenario()
                      << TestScenario::create(5, TestScenario::ClientCmd, cmd)
                      << TestScenario::create(5, TestScenario::ServerCmd, resp)
                      << TestScenario::create(5, TestScenario::ServerCmd, Protocol::CreateCollectionResponse());

            Akonadi::NotificationMessageV3 notification = notificationTemplate;
            notification.addEntity(8, QLatin1String(""), QLatin1String(""));

            QTest::newRow("create collection") << scenarios <<  notification;
        }
        {
            Protocol::CreateCollectionCommand cmd;
            cmd.setName(QLatin1String("Name 2"));
            cmd.setParent(Scope(3));
            cmd.setEnabled(false);
            cmd.setDisplayPref(Tristate::True);
            cmd.setSyncPref(Tristate::True);
            cmd.setIndexPref(Tristate::True);

            Protocol::FetchCollectionsResponse resp(9);
            resp.setName(QLatin1String("Name 2"));
            resp.setParentId(3);
            resp.setEnabled(false);
            resp.setDisplayPref(Tristate::True);
            resp.setSyncPref(Tristate::True);
            resp.setIndexPref(Tristate::True);
            resp.setResource(QLatin1String("akonadi_fake_resource_0"));
            resp.cachePolicy().setLocalParts({ QLatin1String("ALL") });
            resp.setMimeTypes({ QLatin1String("application/octet-stream"),
                                QLatin1String("inode/directory") });


            TestScenario::List scenarios;
            scenarios << FakeAkonadiServer::loginScenario()
                      << TestScenario::create(5, TestScenario::ClientCmd, cmd)
                      << TestScenario::create(5, TestScenario::ServerCmd, resp)
                      << TestScenario::create(5, TestScenario::ServerCmd, Protocol::CreateCollectionResponse());

            Akonadi::NotificationMessageV3 notification = notificationTemplate;
            notification.addEntity(9, QLatin1String(""), QLatin1String(""));

            QTest::newRow("create collection with local override") << scenarios <<  notification;
        }
    }

    void testCreate()
    {
        QFETCH(TestScenario::List, scenarios);
        QFETCH(NotificationMessageV3, notification);

        FakeAkonadiServer::instance()->setScenarios(scenarios);
        FakeAkonadiServer::instance()->runTest();

        QSignalSpy *notificationSpy = FakeAkonadiServer::instance()->notificationSpy();
        if (notification.isValid()) {
            QCOMPARE(notificationSpy->count(), 1);
            const NotificationMessageV3::List notifications = notificationSpy->takeFirst().first().value<NotificationMessageV3::List>();
            QCOMPARE(notifications.count(), 1);
            QCOMPARE(notifications.first(), notification);
        } else {
            QVERIFY(notificationSpy->isEmpty() || notificationSpy->takeFirst().first().value<NotificationMessageV3::List>().isEmpty());
        }
    }

};

AKTEST_FAKESERVER_MAIN(CreateHandlerTest)

#include "createhandlertest.moc"
