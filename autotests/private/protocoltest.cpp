/*
 * Copyright 2015  Daniel Vratil <dvratil@redhat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License or (at your option) version 3 or any later version
 * accepted by the membership of KDE e.V. (or its successor approved
 * by the membership of KDE e.V.), which shall act as a proxy
 * defined in Section 14 of version 3 of the license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "protocoltest.h"

#include <QtTest/QTest>

using namespace Akonadi::Protocol;

void ProtocolTest::testProtocolVersion()
{
    // So, this test started failing because you bumped protocol version in
    // protocol.cpp. That means that you most probably changed something
    // in the protocol. Before you just bump the number here to make the test
    // pass, please please pretty please make sure to extend the respective
    // test somewhere below to cover the change you've made. Protocol is the
    // most critical part of Akonadi and we can't afford not having it tested
    // properly.
    //
    // If it wasn't you who broke it, please go find that person who was too
    // lazy to extend the test case and beat them with a stick. -- Dan

    QCOMPARE(Akonadi::Protocol::version(), 52);
}

void ProtocolTest::testFactory_data()
{
    QTest::addColumn<Command::Type>("type");
    QTest::addColumn<bool>("response");
    QTest::addColumn<bool>("success");

    QTest::newRow("invalid cmd") << Command::Invalid << false << false;
    QTest::newRow("invalid resp") << Command::Invalid << true << false;
    QTest::newRow("hello cmd") << Command::Hello << false << false;
    QTest::newRow("hello resp") << Command::Hello << true << true;
    QTest::newRow("login cmd") << Command::Login << false << true;
    QTest::newRow("login resp") << Command::Login << true << true;
    QTest::newRow("logout cmd") << Command::Logout << false << true;
    QTest::newRow("logout resp") << Command::Logout << true << true;
    QTest::newRow("transaction cmd") << Command::Transaction << false << true;
    QTest::newRow("transaction resp") << Command::Transaction << true << true;
    QTest::newRow("createItem cmd") << Command::CreateItem << false << true;
    QTest::newRow("createItem resp") << Command::CreateItem << true << true;
    QTest::newRow("copyItems cmd") << Command::CopyItems << false << true;
    QTest::newRow("copyItems resp") << Command::CopyItems << true << true;
    QTest::newRow("deleteItems cmd") << Command::DeleteItems << false << true;
    QTest::newRow("deleteItems resp") << Command::DeleteItems << true << true;
    QTest::newRow("fetchItems cmd") << Command::FetchItems << false << true;
    QTest::newRow("fetchItems resp") << Command::FetchItems << true << true;
    QTest::newRow("linkItems cmd") << Command::LinkItems << false << true;
    QTest::newRow("linkItems resp") << Command::LinkItems << true << true;
    QTest::newRow("modifyItems cmd") << Command::ModifyItems << false << true;
    QTest::newRow("modifyItems resp") << Command::ModifyItems << true << true;
    QTest::newRow("moveItems cmd") << Command::MoveItems << false << true;
    QTest::newRow("moveItems resp") << Command::MoveItems << true << true;
    QTest::newRow("createCollection cmd") << Command::CreateCollection << false << true;
    QTest::newRow("createCollection resp") << Command::CreateCollection << true << true;
    QTest::newRow("copyCollection cmd") << Command::CopyCollection << false << true;
    QTest::newRow("copyCollection resp") << Command::CopyCollection << true << true;
    QTest::newRow("deleteCollection cmd") << Command::DeleteCollection << false << true;
    QTest::newRow("deleteCollection resp") << Command::DeleteCollection << true << true;
    QTest::newRow("fetchCollections cmd") << Command::FetchCollections << false << true;
    QTest::newRow("fetchCollections resp") << Command::FetchCollections << true << true;
    QTest::newRow("fetchCollectionStats cmd") << Command::FetchCollectionStats << false << true;
    QTest::newRow("fetchCollectionStats resp") << Command::FetchCollectionStats << false << true;
    QTest::newRow("modifyCollection cmd") << Command::ModifyCollection << false << true;
    QTest::newRow("modifyCollection resp") << Command::ModifyCollection << true << true;
    QTest::newRow("moveCollection cmd") << Command::MoveCollection << false << true;
    QTest::newRow("moveCollection resp") << Command::MoveCollection << true << true;
    QTest::newRow("search cmd") << Command::Search << false << true;
    QTest::newRow("search resp") << Command::Search << true << true;
    QTest::newRow("searchResult cmd") << Command::SearchResult << false << true;
    QTest::newRow("searchResult resp") << Command::SearchResult << true << true;
    QTest::newRow("storeSearch cmd") << Command::StoreSearch << false << true;
    QTest::newRow("storeSearch resp") << Command::StoreSearch << true << true;
    QTest::newRow("createTag cmd") << Command::CreateTag << false << true;
    QTest::newRow("createTag resp") << Command::CreateTag << true << true;
    QTest::newRow("deleteTag cmd") << Command::DeleteTag << false << true;
    QTest::newRow("deleteTag resp") << Command::DeleteTag << true << true;
    QTest::newRow("fetchTags cmd") << Command::FetchTags << false << true;
    QTest::newRow("fetchTags resp") << Command::FetchTags << true << true;
    QTest::newRow("modifyTag cmd") << Command::ModifyTag << false << true;
    QTest::newRow("modifyTag resp") << Command::ModifyTag << true << true;
    QTest::newRow("fetchRelations cmd") << Command::FetchRelations << false << true;
    QTest::newRow("fetchRelations resp") << Command::FetchRelations << true << true;
    QTest::newRow("modifyRelation cmd") << Command::ModifyRelation << false << true;
    QTest::newRow("modifyRelation resp") << Command::ModifyRelation << true << true;
    QTest::newRow("removeRelations cmd") << Command::RemoveRelations << false << true;
    QTest::newRow("removeRelations resp") << Command::RemoveRelations << true << true;
    QTest::newRow("selectResource cmd") << Command::SelectResource << false << true;
    QTest::newRow("selectResource resp") << Command::SelectResource << true << true;
    QTest::newRow("streamPayload cmd") << Command::StreamPayload << false << true;
    QTest::newRow("streamPayload resp") << Command::StreamPayload << true << true;
    QTest::newRow("changeNotification cmd") << Command::ChangeNotification << false << true;
    QTest::newRow("changeNotification resp") << Command::ChangeNotification << true << false;
    QTest::newRow("_responseBit cmd") << Command::_ResponseBit << false << false;
    QTest::newRow("_responseBit resp") << Command::_ResponseBit << true << false;
}

void ProtocolTest::testFactory()
{
    QFETCH(Command::Type, type);
    QFETCH(bool, response);
    QFETCH(bool, success);

    Command result;
    if (response) {
        result = Factory::response(type);
    } else {
        result = Factory::command(type);
    }

    QCOMPARE(result.isValid(), success);
    QCOMPARE(result.isResponse(), response);
    if (success) {
        QCOMPARE(result.type(), type);
    }
}



void ProtocolTest::testCommand()
{
    // There is no way to construct a valid Command directly
    Command cmd;
    QCOMPARE(cmd.type(), Command::Invalid);
    QVERIFY(!cmd.isValid());
    QVERIFY(!cmd.isResponse());

    Command cmdTest = serializeAndDeserialize(cmd);
    QCOMPARE(cmdTest.type(), Command::Invalid);
    QVERIFY(!cmd.isValid());
    QVERIFY(!cmd.isResponse());
}

void ProtocolTest::testResponse_data()
{
    QTest::addColumn<bool>("isError");
    QTest::addColumn<int>("errorCode");
    QTest::addColumn<QString>("errorString");

    QTest::newRow("no error") << false << 0 << QString();
    QTest::newRow("error") << true << 10 << QStringLiteral("Oh noes, there was an error!");
}

void ProtocolTest::testResponse()
{
    QFETCH(bool, isError);
    QFETCH(int, errorCode);
    QFETCH(QString, errorString);

    Response response;
    if (isError) {
        response.setError(errorCode, errorString);
    }

    Response res = serializeAndDeserialize(response);
    QCOMPARE(res.type(), Command::Invalid);
    QVERIFY(!res.isValid());
    QVERIFY(res.isResponse());
    QCOMPARE(res.isError(), isError);
    QCOMPARE(res.errorCode(), errorCode);
    QCOMPARE(res.errorMessage(), errorString);
    QVERIFY(res == response);
    const bool notEquals = (res != response);
    QVERIFY(!notEquals);
}

void ProtocolTest::testAncestor()
{
    Ancestor in;
    in.setId(42);
    in.setRemoteId(QStringLiteral("remoteId"));
    in.setName(QStringLiteral("Col 42"));
    in.setAttributes({{ "Attr1", "Val 1" }, { "Attr2", "Röndom útéef řetězec" }});

    Ancestor out = serializeAndDeserialize(in);
    QCOMPARE(out.id(), 42);
    QCOMPARE(out.remoteId(), QStringLiteral("remoteId"));
    QCOMPARE(out.name(), QStringLiteral("Col 42"));
    QCOMPARE(out.attributes(), Attributes({{ "Attr1", "Val 1" }, { "Attr2", "Röndom útéef řetězec" }}));
}

void ProtocolTest::testFetchScope_data()
{
    QTest::addColumn<bool>("fullPayload");
    QTest::addColumn<QVector<QByteArray>>("requestedParts");
    QTest::addColumn<QVector<QByteArray>>("expectedParts");
    QTest::addColumn<QVector<QByteArray>>("expectedPayloads");
    QTest::newRow("full payload (via flag") << true
                                << QVector<QByteArray>{ "PLD:HEAD", "ATR:MYATR" }
                                << QVector<QByteArray>{ "PLD:HEAD", "ATR:MYATR", "PLD:RFC822" }
                                << QVector<QByteArray>{ "PLD:HEAD", "PLD:RFC822" };
    QTest::newRow("full payload (via part name") << false
                                << QVector<QByteArray>{ "PLD:HEAD", "ATR:MYATR", "PLD:RFC822" }
                                << QVector<QByteArray>{ "PLD:HEAD", "ATR:MYATR", "PLD:RFC822" }
                                << QVector<QByteArray>{ "PLD:HEAD", "PLD:RFC822" };
    QTest::newRow("full payload (via both") << true
                                << QVector<QByteArray>{ "PLD:HEAD", "ATR:MYATR", "PLD:RFC822" }
                                << QVector<QByteArray>{ "PLD:HEAD", "ATR:MYATR", "PLD:RFC822" }
                                << QVector<QByteArray>{ "PLD:HEAD", "PLD:RFC822" };
    QTest::newRow("without full payload") << false
                                << QVector<QByteArray>{ "PLD:HEAD", "ATR:MYATR" }
                                << QVector<QByteArray>{ "PLD:HEAD", "ATR:MYATR" }
                                << QVector<QByteArray>{ "PLD:HEAD" };
}


void ProtocolTest::testFetchScope()
{
    QFETCH(bool, fullPayload);
    QFETCH(QVector<QByteArray>, requestedParts);
    QFETCH(QVector<QByteArray>, expectedParts);
    QFETCH(QVector<QByteArray>, expectedPayloads);

    FetchScope in;
    for (int i = FetchScope::CacheOnly; i <= FetchScope::VirtReferences; i = i << 1) {
        QVERIFY(!in.fetch(static_cast<FetchScope::FetchFlag>(i)));
    }
    QVERIFY(in.fetch(FetchScope::None));

    in.setRequestedParts(requestedParts);
    in.setChangedSince(QDateTime(QDate(2015, 8, 10), QTime(23, 52, 20), Qt::UTC));
    in.setTagFetchScope({ "TAGID" });
    in.setAncestorDepth(Ancestor::AllAncestors);
    in.setFetch(FetchScope::CacheOnly);
    in.setFetch(FetchScope::CheckCachedPayloadPartsOnly);
    in.setFetch(FetchScope::FullPayload, fullPayload);
    in.setFetch(FetchScope::AllAttributes);
    in.setFetch(FetchScope::Size);
    in.setFetch(FetchScope::MTime);
    in.setFetch(FetchScope::RemoteRevision);
    in.setFetch(FetchScope::IgnoreErrors);
    in.setFetch(FetchScope::Flags);
    in.setFetch(FetchScope::RemoteID);
    in.setFetch(FetchScope::GID);
    in.setFetch(FetchScope::Tags);
    in.setFetch(FetchScope::Relations);
    in.setFetch(FetchScope::VirtReferences);

    FetchScope out = serializeAndDeserialize(in);
    QCOMPARE(out.requestedParts(), expectedParts);
    QCOMPARE(out.requestedPayloads(), expectedPayloads);
    QCOMPARE(out.changedSince(), QDateTime(QDate(2015, 8, 10), QTime(23, 52, 20), Qt::UTC));
    QCOMPARE(out.tagFetchScope(), QSet<QByteArray>{ "TAGID" });
    QCOMPARE(out.ancestorDepth(), Ancestor::AllAncestors);
    QCOMPARE(out.fetch(FetchScope::None), false);
    QCOMPARE(out.cacheOnly(), true);
    QCOMPARE(out.checkCachedPayloadPartsOnly(), true);
    QCOMPARE(out.fullPayload(), fullPayload);
    QCOMPARE(out.allAttributes(), true);
    QCOMPARE(out.fetchSize(), true);
    QCOMPARE(out.fetchMTime(), true);
    QCOMPARE(out.fetchRemoteRevision(), true);
    QCOMPARE(out.ignoreErrors(), true);
    QCOMPARE(out.fetchFlags(), true);
    QCOMPARE(out.fetchRemoteId(), true);
    QCOMPARE(out.fetchGID(), true);
    QCOMPARE(out.fetchRelations(), true);
    QCOMPARE(out.fetchVirtualReferences(), true);
}

void ProtocolTest::testScopeContext_data()
{
    QTest::addColumn<qint64>("colId");
    QTest::addColumn<QString>("colRid");
    QTest::addColumn<qint64>("tagId");
    QTest::addColumn<QString>("tagRid");

    QTest::newRow("collection - id") << 42ll << QString()
                                     << 0ll << QString();
    QTest::newRow("collection - rid") << 0ll << QStringLiteral("rid")
                                      << 0ll << QString();
    QTest::newRow("collection - both") << 42ll << QStringLiteral("rid")
                                       << 0ll << QString();

    QTest::newRow("tag - id") << 0ll << QString()
                              << 42ll << QString();
    QTest::newRow("tag - rid") << 0ll << QString()
                               << 0ll << QStringLiteral("rid");
    QTest::newRow("tag - both") << 0ll << QString()
                                << 42ll << QStringLiteral("rid");

    QTest::newRow("both - id") << 42ll << QString()
                               << 10ll << QString();
    QTest::newRow("both - rid") << 0ll << QStringLiteral("colRid")
                                << 0ll << QStringLiteral("tagRid");
    QTest::newRow("col - id, tag - rid") << 42ll << QString()
                                         << 0ll << QStringLiteral("tagRid");
    QTest::newRow("col - rid, tag - id") << 0ll << QStringLiteral("colRid")
                                         << 42ll << QString();
    QTest::newRow("both - both") << 42ll << QStringLiteral("colRid")
                                 << 10ll << QStringLiteral("tagRid");
}

void ProtocolTest::testScopeContext()
{
    QFETCH(qint64, colId);
    QFETCH(QString, colRid);
    QFETCH(qint64, tagId);
    QFETCH(QString, tagRid);

    const bool hasColId = colId > 0;
    const bool hasColRid = !colRid.isEmpty();
    const bool hasTagId = tagId > 0;
    const bool hasTagRid = !tagRid.isEmpty();

    ScopeContext in;
    QVERIFY(in.isEmpty());
    if (hasColId) {
        in.setContext(ScopeContext::Collection, colId);
    }
    if (hasColRid) {
        in.setContext(ScopeContext::Collection, colRid);
    }
    if (hasTagId) {
        in.setContext(ScopeContext::Tag, tagId);
    }
    if (hasTagRid) {
        in.setContext(ScopeContext::Tag, tagRid);
    }

    QCOMPARE(in.hasContextId(ScopeContext::Any), false);
    QCOMPARE(in.hasContextRID(ScopeContext::Any), false);
    QEXPECT_FAIL("collection - both", "Cannot set both ID and RID context", Continue);
    QEXPECT_FAIL("both - both", "Cannot set both ID and RID context", Continue);
    QCOMPARE(in.hasContextId(ScopeContext::Collection), hasColId);
    QCOMPARE(in.hasContextRID(ScopeContext::Collection), hasColRid);
    QEXPECT_FAIL("both - both", "Cannot set both ID and RID context", Continue);
    QEXPECT_FAIL("tag - both", "Cannot set both ID and RID context", Continue);
    QCOMPARE(in.hasContextId(ScopeContext::Tag), hasTagId);
    QCOMPARE(in.hasContextRID(ScopeContext::Tag), hasTagRid);
    QVERIFY(!in.isEmpty());

    ScopeContext out = serializeAndDeserialize(in);
    QCOMPARE(out.isEmpty(), false);
    QEXPECT_FAIL("collection - both", "Cannot set both ID and RID context", Continue);
    QEXPECT_FAIL("both - both", "Cannot set both ID and RID context", Continue);
    QCOMPARE(out.hasContextId(ScopeContext::Collection), hasColId);
    QEXPECT_FAIL("collection - both", "Cannot set both ID and RID context", Continue);
    QEXPECT_FAIL("both - both", "Cannot set both ID and RID context", Continue);
    QCOMPARE(out.contextId(ScopeContext::Collection), colId);
    QCOMPARE(out.hasContextRID(ScopeContext::Collection), hasColRid);
    QCOMPARE(out.contextRID(ScopeContext::Collection), colRid);
    QEXPECT_FAIL("tag - both", "Cannot set both ID and RID context", Continue);
    QEXPECT_FAIL("both - both", "Cannot set both ID and RID context", Continue);
    QCOMPARE(out.hasContextId(ScopeContext::Tag), hasTagId);
    QEXPECT_FAIL("tag - both", "Cannot set both ID and RID context", Continue);
    QEXPECT_FAIL("both - both", "Cannot set both ID and RID context", Continue);
    QCOMPARE(out.contextId(ScopeContext::Tag), tagId);
    QCOMPARE(out.hasContextRID(ScopeContext::Tag), hasTagRid);
    QCOMPARE(out.contextRID(ScopeContext::Tag), tagRid);
    QCOMPARE(out, in);
    const bool notEqual = (out != in);
    QVERIFY(!notEqual);

    // Clearing "any" should not do anything
    out.clearContext(ScopeContext::Any);
    QEXPECT_FAIL("collection - both", "Cannot set both ID and RID context", Continue);
    QEXPECT_FAIL("both - both", "Cannot set both ID and RID context", Continue);
    QCOMPARE(out.hasContextId(ScopeContext::Collection), hasColId);
    QEXPECT_FAIL("collection - both", "Cannot set both ID and RID context", Continue);
    QEXPECT_FAIL("both - both", "Cannot set both ID and RID context", Continue);
    QCOMPARE(out.contextId(ScopeContext::Collection), colId);
    QCOMPARE(out.hasContextRID(ScopeContext::Collection), hasColRid);
    QCOMPARE(out.contextRID(ScopeContext::Collection), colRid);
    QEXPECT_FAIL("tag - both", "Cannot set both ID and RID context", Continue);
    QEXPECT_FAIL("both - both", "Cannot set both ID and RID context", Continue);
    QCOMPARE(out.hasContextId(ScopeContext::Tag), hasTagId);
    QEXPECT_FAIL("tag - both", "Cannot set both ID and RID context", Continue);
    QEXPECT_FAIL("both - both", "Cannot set both ID and RID context", Continue);
    QCOMPARE(out.contextId(ScopeContext::Tag), tagId);
    QCOMPARE(out.hasContextRID(ScopeContext::Tag), hasTagRid);
    QCOMPARE(out.contextRID(ScopeContext::Tag), tagRid);

    if (hasColId || hasColRid) {
        ScopeContext clear = out;
        clear.clearContext(ScopeContext::Collection);
        QCOMPARE(clear.hasContextId(ScopeContext::Collection), false);
        QCOMPARE(clear.hasContextRID(ScopeContext::Collection), false);
        QEXPECT_FAIL("both - both", "Cannot set both ID and RID context", Continue);
        QCOMPARE(clear.hasContextId(ScopeContext::Tag), hasTagId);
        QCOMPARE(clear.hasContextRID(ScopeContext::Tag), hasTagRid);
    }
    if (hasTagId || hasTagRid) {
        ScopeContext clear = out;
        clear.clearContext(ScopeContext::Tag);
        QEXPECT_FAIL("both - both", "Cannot set both ID and RID context", Continue);
        QCOMPARE(clear.hasContextId(ScopeContext::Collection), hasColId);
        QCOMPARE(clear.hasContextRID(ScopeContext::Collection), hasColRid);
        QCOMPARE(clear.hasContextId(ScopeContext::Tag), false);
        QCOMPARE(clear.hasContextRID(ScopeContext::Tag), false);
    }

    out.clearContext(ScopeContext::Collection);
    out.clearContext(ScopeContext::Tag);
    QVERIFY(out.isEmpty());
}


QTEST_MAIN(ProtocolTest)