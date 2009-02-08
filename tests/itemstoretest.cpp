  /*
    Copyright (c) 2006 Volker Krause <vkrause@kde.org>
    Copyright (c) 2007 Robert Zwerus <arzie@dds.nl>

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

#include "control.h"
#include "itemstoretest.h"
#include "testattribute.h"
#include <akonadi/agentmanager.h>
#include <akonadi/agentinstance.h>
#include <akonadi/attributefactory.h>
#include <akonadi/collectionfetchjob.h>
#include <akonadi/collectionselectjob.h>
#include <akonadi/itemcreatejob.h>
#include <akonadi/itemdeletejob.h>
#include <akonadi/itemfetchjob.h>
#include <akonadi/itemfetchscope.h>
#include <akonadi/itemmodifyjob.h>
#include <qtest_akonadi.h>
#include "test_utils.h"

using namespace Akonadi;

QTEST_AKONADIMAIN( ItemStoreTest, NoGUI )

static Collection res1_foo;
static Collection res2;
static Collection res3;

void ItemStoreTest::initTestCase()
{
  Control::start();
  AttributeFactory::registerAttribute<TestAttribute>();

  // get the collections we run the tests on
  res1_foo = Collection( collectionIdFromPath( "res1/foo" ) );
  QVERIFY( res1_foo.isValid() );
  res2 = Collection( collectionIdFromPath( "res2" ) );
  QVERIFY( res2.isValid() );
  res3 = Collection( collectionIdFromPath( "res3" ) );
  QVERIFY( res3.isValid() );

  // switch all resources offline to reduce interference from them
  foreach ( Akonadi::AgentInstance agent, Akonadi::AgentManager::self()->instances() )
    agent.setIsOnline( false );
}

void ItemStoreTest::testFlagChange()
{
  ItemFetchJob *fjob = new ItemFetchJob( Item( 1 ) );
  QVERIFY( fjob->exec() );
  QCOMPARE( fjob->items().count(), 1 );
  Item item = fjob->items()[0];

  // add a flag
  Item::Flags origFlags = item.flags();
  Item::Flags expectedFlags = origFlags;
  expectedFlags.insert( "added_test_flag_1" );
  item.setFlag( "added_test_flag_1" );
  ItemModifyJob *sjob = new ItemModifyJob( item, this );
  QVERIFY( sjob->exec() );

  fjob = new ItemFetchJob( Item( 1 ) );
  QVERIFY( fjob->exec() );
  QCOMPARE( fjob->items().count(), 1 );
  item = fjob->items()[0];
  QCOMPARE( item.flags().count(), expectedFlags.count() );
  Item::Flags diff = expectedFlags - item.flags();
  QVERIFY( diff.isEmpty() );

  // set flags
  expectedFlags.insert( "added_test_flag_2" );
  item.setFlags( expectedFlags );
  sjob = new ItemModifyJob( item, this );
  QVERIFY( sjob->exec() );

  fjob = new ItemFetchJob( Item( 1 ) );
  QVERIFY( fjob->exec() );
  QCOMPARE( fjob->items().count(), 1 );
  item = fjob->items()[0];
  QCOMPARE( item.flags().count(), expectedFlags.count() );
  diff = expectedFlags - item.flags();
  QVERIFY( diff.isEmpty() );

  // remove a flag
  item.clearFlag( "added_test_flag_1" );
  item.clearFlag( "added_test_flag_2" );
  sjob = new ItemModifyJob( item, this );
  QVERIFY( sjob->exec() );

  fjob = new ItemFetchJob( Item( 1 ) );
  QVERIFY( fjob->exec() );
  QCOMPARE( fjob->items().count(), 1 );
  item = fjob->items()[0];
  QCOMPARE( item.flags().count(), origFlags.count() );
  diff = origFlags - item.flags();
  QVERIFY( diff.isEmpty() );
}

void ItemStoreTest::testDataChange_data()
{
  QTest::addColumn<QByteArray>( "data" );

  QTest::newRow( "empty" ) << QByteArray();
  QTest::newRow( "nullbyte" ) << QByteArray("\0" );
  QTest::newRow( "nullbyte2" ) << QByteArray( "\0X" );
  QTest::newRow( "linebreaks" ) << QByteArray( "line1\nline2\n\rline3\rline4\r\n" );
  QTest::newRow( "linebreaks2" ) << QByteArray( "line1\r\nline2\r\n\r\n" );
  QTest::newRow( "linebreaks3" ) << QByteArray( "line1\nline2" );
  QTest::newRow( "simple" ) << QByteArray( "testbody" );
}

void ItemStoreTest::testDataChange()
{
  QFETCH( QByteArray, data );

  Item item;
  ItemFetchJob *prefetchjob = new ItemFetchJob( Item( 1 ) );
  prefetchjob->exec();
  item = prefetchjob->items()[0];
  item.setMimeType( "application/octet-stream" );
  item.setPayload( data );
  QCOMPARE( item.payload<QByteArray>(), data );

  // delete data
  ItemModifyJob *sjob = new ItemModifyJob( item );
  QVERIFY( sjob->exec() );

  ItemFetchJob *fjob = new ItemFetchJob( Item( 1 ) );
  fjob->fetchScope().fetchFullPayload();
  QVERIFY( fjob->exec() );
  QCOMPARE( fjob->items().count(), 1 );
  item = fjob->items()[0];
  QVERIFY( item.hasPayload<QByteArray>() );
  QEXPECT_FAIL( "empty", "Item does not detect QByteArray() as loaded payload part", Continue );
  QCOMPARE( item.payload<QByteArray>(), data );
}

void ItemStoreTest::testRemoteId_data()
{
  QTest::addColumn<QString>( "rid" );
  QTest::addColumn<QString>( "exprid" );

  QTest::newRow( "set" ) << QString( "A" ) << QString( "A" );
  QTest::newRow( "no-change" ) << QString() << QString( "A" );
  QTest::newRow( "clear" ) << QString( "" ) << QString( "" );
  QTest::newRow( "reset" ) << QString( "A" ) << QString( "A" );
}

void ItemStoreTest::testRemoteId()
{
  QFETCH( QString, rid );
  QFETCH( QString, exprid );

  ItemFetchJob *prefetchjob = new ItemFetchJob( Item( 1 ) );
  prefetchjob->exec();
  Item item = prefetchjob->items()[0];

  item.setId( 1 );
  item.setRemoteId( rid );
  ItemModifyJob *store = new ItemModifyJob( item, this );
  QVERIFY( store->exec() );

  ItemFetchJob *fetch = new ItemFetchJob( item, this );
  QVERIFY( fetch->exec() );
  QCOMPARE( fetch->items().count(), 1 );
  item = fetch->items().at( 0 );
  QCOMPARE( item.remoteId(), exprid );
}

void ItemStoreTest::testMultiPart()
{
  ItemFetchJob *prefetchjob = new ItemFetchJob( Item( 1 ) );
  QVERIFY( prefetchjob->exec() );
  QCOMPARE( prefetchjob->items().count(), 1 );
  Item item = prefetchjob->items()[0];
  item.setMimeType( "application/octet-stream" );
  item.setPayload<QByteArray>( "testmailbody" );
  item.attribute<TestAttribute>( Item::AddIfMissing )->data = "extra";

  // store item
  ItemModifyJob *sjob = new ItemModifyJob( item );
  QVERIFY( sjob->exec() );

  ItemFetchJob *fjob = new ItemFetchJob( Item( 1 ) );
  fjob->fetchScope().fetchAttribute<TestAttribute>();
  fjob->fetchScope().fetchFullPayload();
  QVERIFY( fjob->exec() );
  QCOMPARE( fjob->items().count(), 1 );
  item = fjob->items()[0];
  QVERIFY( item.hasPayload<QByteArray>() );
  QCOMPARE( item.payload<QByteArray>(), QByteArray("testmailbody") );
  QVERIFY( item.hasAttribute<TestAttribute>() );
  QCOMPARE( item.attribute<TestAttribute>()->data, QByteArray("extra") );

  // clean up
  item.removeAttribute( "EXTRA" );
  sjob = new ItemModifyJob( item );
  QVERIFY( sjob->exec() );
}

void ItemStoreTest::testPartRemove()
{
  ItemFetchJob *prefetchjob = new ItemFetchJob( Item( 2 ) );
  prefetchjob->exec();
  Item item = prefetchjob->items()[0];
  item.setMimeType( "application/octet-stream" );
  item.attribute<TestAttribute>( Item::AddIfMissing )->data = "extra";

  // store item
  ItemModifyJob *sjob = new ItemModifyJob( item );
  QVERIFY( sjob->exec() );

  // fetch item and its parts (should be RFC822, HEAD and EXTRA)
  ItemFetchJob *fjob = new ItemFetchJob( Item( 2 ) );
  fjob->fetchScope().fetchFullPayload();
  fjob->fetchScope().fetchAllAttributes();
  QVERIFY( fjob->exec() );
  QCOMPARE( fjob->items().count(), 1 );
  item = fjob->items()[0];
  QCOMPARE( item.attributes().count(), 2 );
  QVERIFY( item.hasAttribute<TestAttribute>() );

  // remove a part
  item.removeAttribute<TestAttribute>();
  sjob = new ItemModifyJob( item );
  QVERIFY( sjob->exec() );

  // fetch item again (should only have RFC822 and HEAD left)
  ItemFetchJob *fjob2 = new ItemFetchJob( Item( 2 ) );
  fjob2->fetchScope().fetchFullPayload();
  fjob2->fetchScope().fetchAllAttributes();
  QVERIFY( fjob2->exec() );
  QCOMPARE( fjob2->items().count(), 1 );
  item = fjob2->items()[0];
  QCOMPARE( item.attributes().count(), 1 );
  QVERIFY( !item.hasAttribute<TestAttribute>() );
}

void ItemStoreTest::testRevisionCheck()
{
  // make sure we don't have any other collection selected
  // otherwise EXPUNGE doesn't work and will be triggered by
  // the following tests and mess up the monitor testing
  CollectionSelectJob *sel = new CollectionSelectJob( Collection::root(), this );
  QVERIFY( sel->exec() );

  // fetch same item twice
  Item ref( 2 );
  ItemFetchJob *prefetchjob = new ItemFetchJob( ref );
  QVERIFY( prefetchjob->exec() );
  QCOMPARE( prefetchjob->items().count(), 1 );
  Item item1 = prefetchjob->items()[0];
  Item item2 = prefetchjob->items()[0];

  // store first item unmodified
  ItemModifyJob *sjob = new ItemModifyJob( item1 );
  QVERIFY( sjob->exec() );

  // try to store second item
  ItemModifyJob *sjob2 = new ItemModifyJob( item2 );
  item2.attribute<TestAttribute>( Item::AddIfMissing )->data = "extra";
  QVERIFY( !sjob2->exec() );

  // fetch same again
  prefetchjob = new ItemFetchJob( ref );
  prefetchjob->exec();
  item1 = prefetchjob->items()[0];

  // delete item
  ItemDeleteJob *djob = new ItemDeleteJob( ref, this );
  djob->exec();

  // try to store it
  sjob = new ItemModifyJob( item1 );
  QVERIFY( !sjob->exec() );
}

void ItemStoreTest::testModificationTime()
{
  Item item;
  item.setMimeType( "text/directory" );
  QVERIFY( item.modificationTime().isNull() );

  ItemCreateJob *job = new ItemCreateJob( item, res1_foo );
  QVERIFY( job->exec() );

  // The item should have a datetime set now.
  item = job->item();
  QVERIFY( !item.modificationTime().isNull() );
  QDateTime initialDateTime = item.modificationTime();

  // Fetch the same item again.
  Item item2( item.id() );
  ItemFetchJob *fjob = new ItemFetchJob( item2, this );
  QVERIFY( fjob->exec() );
  item2 = fjob->items().first();
  QCOMPARE( initialDateTime, item2.modificationTime() );

  // Lets wait 5 secs.
  QTest::qWait( 5000 );

  // Modify the item
  item.attribute<TestAttribute>( Item::AddIfMissing )->data = "extra";
  ItemModifyJob *mjob = new ItemModifyJob( item );
  QVERIFY( mjob->exec() );

  // The item should still have a datetime set and that date should be somewhere
  // after the initialDateTime.
  item = mjob->item();
  QVERIFY( !item.modificationTime().isNull() );
  QVERIFY( initialDateTime < item.modificationTime() );

  // Fetch the item after modification.
  Item item3( item.id() );
  ItemFetchJob *fjob2 = new ItemFetchJob( item3, this );
  QVERIFY( fjob2->exec() );

  // item3 should have the same modification time as item.
  item3 = fjob2->items().first();
  QCOMPARE( item3.modificationTime(), item.modificationTime() );

  // Clean up
  ItemDeleteJob *idjob = new ItemDeleteJob( item, this );
  QVERIFY( idjob->exec() );
}

#include "itemstoretest.moc"
