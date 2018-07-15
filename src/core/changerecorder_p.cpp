/*
    Copyright (c) 2007 Volker Krause <vkrause@kde.org>

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

#include "changerecorder_p.h"
#include "akonadicore_debug.h"


#include <QFile>
#include <QDir>
#include <QSettings>
#include <QFileInfo>
#include <QDataStream>

using namespace Akonadi;

ChangeRecorderPrivate::ChangeRecorderPrivate(ChangeNotificationDependenciesFactory *dependenciesFactory_,
        ChangeRecorder *parent)
    : MonitorPrivate(dependenciesFactory_, parent)
    , settings(nullptr)
    , enableChangeRecording(true)
    , m_lastKnownNotificationsCount(0)
    , m_startOffset(0)
    , m_needFullSave(true)
{
}

int ChangeRecorderPrivate::pipelineSize() const
{
    if (enableChangeRecording) {
        return 0; // we fill the pipeline ourselves when using change recording
    }
    return MonitorPrivate::pipelineSize();
}

void ChangeRecorderPrivate::slotNotify(const Protocol::ChangeNotificationPtr &msg)
{
    Q_Q(ChangeRecorder);
    const int oldChanges = pendingNotifications.size();
    // with change recording disabled this will automatically take care of dispatching notification messages and saving
    MonitorPrivate::slotNotify(msg);
    if (enableChangeRecording && pendingNotifications.size() != oldChanges) {
        emit q->changesAdded();
    }
}

// The QSettings object isn't actually used anymore, except for migrating old data
// and it gives us the base of the filename to use. This is all historical.
QString ChangeRecorderPrivate::notificationsFileName() const
{
    return settings->fileName() + QStringLiteral("_changes.dat");
}

void ChangeRecorderPrivate::loadNotifications()
{
    pendingNotifications.clear();
    Q_ASSERT(pipeline.isEmpty());
    pipeline.clear();

    const QString changesFileName = notificationsFileName();

    /**
     * In an older version we recorded changes inside the settings object, however
     * for performance reasons we changed that to store them in a separated file.
     * If this file doesn't exists, it means we run the new version the first time,
     * so we have to read in the legacy list of changes first.
     */
    if (!QFile::exists(changesFileName)) {
        QStringList list;
        settings->beginGroup(QStringLiteral("ChangeRecorder"));
        const int size = settings->beginReadArray(QStringLiteral("change"));

        for (int i = 0; i < size; ++i) {
            settings->setArrayIndex(i);
            Protocol::ChangeNotificationPtr msg;

            switch (static_cast<LegacyType>(settings->value(QStringLiteral("type")).toInt())) {
            case Item:
                msg = loadItemNotification(settings);
                break;
            case Collection:
                msg = loadCollectionNotification(settings);
                break;
            case Tag:
            case Relation:
            case InvalidType:
                qWarning() << "Unexpected notification type in legacy store";
                continue;
            }
            if (msg->isValid()) {
                pendingNotifications << msg;
            }
        }

        settings->endArray();

        // save notifications to the new file...
        saveNotifications();

        // ...delete the legacy list...
        settings->remove(QString());
        settings->endGroup();

        // ...and continue as usually
    }

    QFile file(changesFileName);
    if (file.open(QIODevice::ReadOnly)) {
        m_needFullSave = false;
        pendingNotifications = loadFrom(&file, m_needFullSave);
    } else {
        m_needFullSave = true;
    }
    notificationsLoaded();
}

static const quint64 s_currentVersion = Q_UINT64_C(0x000700000000);
static const quint64 s_versionMask    = Q_UINT64_C(0xFFFF00000000);
static const quint64 s_sizeMask       = Q_UINT64_C(0x0000FFFFFFFF);

QQueue<Protocol::ChangeNotificationPtr> ChangeRecorderPrivate::loadFrom(QFile *device, bool &needsFullSave) const
{
    QDataStream stream(device);
    stream.setVersion(QDataStream::Qt_4_6);

    QByteArray sessionId;
    int type;

    QQueue<Protocol::ChangeNotificationPtr> list;

    quint64 sizeAndVersion;
    stream >> sizeAndVersion;

    const quint64 size = sizeAndVersion & s_sizeMask;
    const quint64 version = (sizeAndVersion & s_versionMask) >> 32;

    quint64 startOffset = 0;
    if (version >= 1) {
        stream >> startOffset;
    }

    // If we skip the first N items, then we'll need to rewrite the file on saving.
    // Also, if the file is old, it needs to be rewritten.
    needsFullSave = startOffset > 0 || version == 0;

    for (quint64 i = 0; i < size && !stream.atEnd(); ++i) {
        Protocol::ChangeNotificationPtr msg;
        stream >> sessionId;
        stream >> type;

        if (stream.status() != QDataStream::Ok) {
            qCWarning(AKONADICORE_LOG) << "Error reading saved notifications! Aborting. Corrupt file:" << device->fileName();
            break;
        }

        switch (static_cast<LegacyType>(type)) {
        case Item:
            msg = loadItemNotification(stream, version);
            break;
        case Collection:
            msg = loadCollectionNotification(stream, version);
            break;
        case Tag:
            msg = loadTagNotification(stream, version);
            break;
        case Relation:
            msg = loadRelationNotification(stream, version);
            break;
        default:
            qCWarning(AKONADICORE_LOG) << "Unknown notification type";
            break;
        }

        if (i < startOffset) {
            continue;
        }

        if (msg && msg->isValid()) {
            msg->setSessionId(sessionId);
            list << msg;
        }
    }

    return list;
}

QString ChangeRecorderPrivate::dumpNotificationListToString() const
{
    if (!settings) {
        return QStringLiteral("No settings set in ChangeRecorder yet.");
    }
    const QString changesFileName = notificationsFileName();
    QFile file(changesFileName);

    if (!file.open(QIODevice::ReadOnly)) {
        return QLatin1String("Error reading ") + changesFileName;
    }

    QString result;
    bool dummy;
    const QQueue<Protocol::ChangeNotificationPtr> notifications = loadFrom(&file, dummy);
    for (const Protocol::ChangeNotificationPtr &n : notifications) {
        result += Protocol::debugString(n) + QLatin1Char('\n');
    }
    return result;
}

void ChangeRecorderPrivate::addToStream(QDataStream &stream, const Protocol::ChangeNotificationPtr &msg)
{
    // We deliberately don't use Factory::serialize(), because the internal
    // serialization format could change at any point

    stream << msg->sessionId();
    stream << int(mapToLegacyType(msg->type()));
    switch (msg->type()) {
    case Protocol::Command::ItemChangeNotification:
        saveItemNotification(stream, Protocol::cmdCast<Protocol::ItemChangeNotification>(msg));
        break;
    case Protocol::Command::CollectionChangeNotification:
        saveCollectionNotification(stream, Protocol::cmdCast<Protocol::CollectionChangeNotification>(msg));
        break;
    case Protocol::Command::TagChangeNotification:
        saveTagNotification(stream, Protocol::cmdCast<Protocol::TagChangeNotification>(msg));
        break;
    case Protocol::Command::RelationChangeNotification:
        saveRelationNotification(stream, Protocol::cmdCast<Protocol::RelationChangeNotification>(msg));
        break;
    default:
        qCWarning(AKONADICORE_LOG) << "Unexpected type?";
        return;
    }
}

void ChangeRecorderPrivate::writeStartOffset()
{
    if (!settings) {
        return;
    }

    QFile file(notificationsFileName());
    if (!file.open(QIODevice::ReadWrite)) {
        qCWarning(AKONADICORE_LOG) << "Could not update notifications in file" << file.fileName();
        return;
    }

    // Skip "countAndVersion"
    file.seek(8);

    //qCDebug(AKONADICORE_LOG) << "Writing start offset=" << m_startOffset;

    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_4_6);
    stream << static_cast<quint64>(m_startOffset);

    // Everything else stays unchanged
}

void ChangeRecorderPrivate::saveNotifications()
{
    if (!settings) {
        return;
    }

    QFile file(notificationsFileName());
    QFileInfo info(file);
    if (!QFile::exists(info.absolutePath())) {
        QDir dir;
        dir.mkpath(info.absolutePath());
    }
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(AKONADICORE_LOG) << "Could not save notifications to file" << file.fileName();
        return;
    }
    saveTo(&file);
    m_needFullSave = false;
    m_startOffset = 0;
}

void ChangeRecorderPrivate::saveTo(QIODevice *device)
{
    // Version 0 of this file format was writing a quint64 count, followed by the notifications.
    // Version 1 bundles a version number into that quint64, to be able to detect a version number at load time.

    const quint64 countAndVersion = static_cast<quint64>(pendingNotifications.count()) | s_currentVersion;

    QDataStream stream(device);
    stream.setVersion(QDataStream::Qt_4_6);

    stream << countAndVersion;
    stream << quint64(0); // no start offset

    //qCDebug(AKONADICORE_LOG) << "Saving" << pendingNotifications.count() << "notifications (full save)";

    for (int i = 0; i < pendingNotifications.count(); ++i) {
        const Protocol::ChangeNotificationPtr msg = pendingNotifications.at(i);
        addToStream(stream, msg);
    }
}

void ChangeRecorderPrivate::notificationsEnqueued(int count)
{
    // Just to ensure the contract is kept, and these two methods are always properly called.
    if (enableChangeRecording) {
        m_lastKnownNotificationsCount += count;
        if (m_lastKnownNotificationsCount != pendingNotifications.count()) {
            qCWarning(AKONADICORE_LOG) << this << "The number of pending notifications changed without telling us! Expected"
                                       << m_lastKnownNotificationsCount << "but got" << pendingNotifications.count()
                                       << "Caller just added" << count;
            Q_ASSERT(pendingNotifications.count() == m_lastKnownNotificationsCount);
        }

        saveNotifications();
    }
}

void ChangeRecorderPrivate::dequeueNotification()
{
    if (pendingNotifications.isEmpty()) {
        return;
    }

    pendingNotifications.dequeue();
    if (enableChangeRecording) {

        Q_ASSERT(pendingNotifications.count() == m_lastKnownNotificationsCount - 1);
        --m_lastKnownNotificationsCount;

        if (m_needFullSave || pendingNotifications.isEmpty()) {
            saveNotifications();
        } else {
            ++m_startOffset;
            writeStartOffset();
        }
    }
}

void ChangeRecorderPrivate::notificationsErased()
{
    if (enableChangeRecording) {
        m_lastKnownNotificationsCount = pendingNotifications.count();
        m_needFullSave = true;
        saveNotifications();
    }
}

void ChangeRecorderPrivate::notificationsLoaded()
{
    m_lastKnownNotificationsCount = pendingNotifications.count();
    m_startOffset = 0;
}

bool ChangeRecorderPrivate::emitNotification(const Protocol::ChangeNotificationPtr &msg)
{
    const bool someoneWasListening = MonitorPrivate::emitNotification(msg);
    if (!someoneWasListening && enableChangeRecording) {
        //If no signal was emitted (e.g. because no one was connected to it), no one is going to call changeProcessed, so we help ourselves.
        dequeueNotification();
        QMetaObject::invokeMethod(q_ptr, "replayNext", Qt::QueuedConnection);
    }
    return someoneWasListening;
}

Protocol::ChangeNotificationPtr ChangeRecorderPrivate::loadItemNotification(QSettings *settings) const
{
    auto msg = Protocol::ItemChangeNotificationPtr::create();
    msg->setSessionId(settings->value(QStringLiteral("sessionId")).toByteArray());
    msg->setOperation(mapItemOperation(static_cast<LegacyOp>(settings->value(QStringLiteral("op")).toInt())));
    Protocol::FetchItemsResponse item;
    item.setId(settings->value(QStringLiteral("uid")).toLongLong());
    item.setRemoteId(settings->value(QStringLiteral("rid")).toString());
    item.setMimeType(settings->value(QStringLiteral("mimeType")).toString());
    msg->setItems({std::move(item)});
    msg->addMetadata("FETCH_ITEM");
    msg->setResource(settings->value(QStringLiteral("resource")).toByteArray());
    msg->setParentCollection(settings->value(QStringLiteral("parentCol")).toLongLong());
    msg->setParentDestCollection(settings->value(QStringLiteral("parentDestCol")).toLongLong());
    const QStringList list = settings->value(QStringLiteral("itemParts")).toStringList();
    QSet<QByteArray> itemParts;
    for (const QString &entry : list) {
        itemParts.insert(entry.toLatin1());
    }
    msg->setItemParts(itemParts);
    return msg;
}

Protocol::ChangeNotificationPtr ChangeRecorderPrivate::loadCollectionNotification(QSettings *settings) const
{
    auto msg = Protocol::CollectionChangeNotificationPtr::create();
    msg->setSessionId(settings->value(QStringLiteral("sessionId")).toByteArray());
    msg->setOperation(mapCollectionOperation(static_cast<LegacyOp>(settings->value(QStringLiteral("op")).toInt())));
    Protocol::FetchCollectionsResponse collection;
    collection.setId(settings->value(QStringLiteral("uid")).toLongLong());
    collection.setRemoteId(settings->value(QStringLiteral("rid")).toString());
    msg->setCollection(std::move(collection));
    msg->addMetadata("FETCH_COLLECTION");
    msg->setResource(settings->value(QStringLiteral("resource")).toByteArray());
    msg->setParentCollection(settings->value(QStringLiteral("parentCol")).toLongLong());
    msg->setParentDestCollection(settings->value(QStringLiteral("parentDestCol")).toLongLong());
    const QStringList list = settings->value(QStringLiteral("itemParts")).toStringList();
    QSet<QByteArray> changedParts;
    for (const QString &entry : list) {
        changedParts.insert(entry.toLatin1());
    }
    msg->setChangedParts(changedParts);
    return msg;
}

QSet<Protocol::ItemChangeNotification::Relation> ChangeRecorderPrivate::extractRelations(QSet<QByteArray> &flags) const
{
    QSet<Protocol::ItemChangeNotification::Relation> relations;
    auto iter = flags.begin();
    while (iter != flags.end()) {
        if (iter->startsWith("RELATION")) {
            const QByteArrayList parts = iter->split(' ');
            Q_ASSERT(parts.size() == 4);
            Protocol::ItemChangeNotification::Relation relation;
            relation.type = QString::fromLatin1(parts[1]);
            relation.leftId = parts[2].toLongLong();
            relation.rightId = parts[3].toLongLong();
            relations.insert(relation);
            iter = flags.erase(iter);
        } else {
            ++iter;
        }
    }

    return relations;
}

Protocol::ChangeNotificationPtr ChangeRecorderPrivate::loadItemNotification(QDataStream &stream, quint64 version) const
{
    QByteArray resource, destinationResource;
    int operation, entityCnt;
    qint64 uid, parentCollection, parentDestCollection;
    QString remoteId, mimeType, remoteRevision;
    QSet<QByteArray> itemParts, addedFlags, removedFlags;
    QSet<qint64> addedTags, removedTags;
    QVector<Protocol::FetchItemsResponse> items;

    auto msg = Protocol::ItemChangeNotificationPtr::create();

    if (version == 1) {
        stream >> operation;
        stream >> uid;
        stream >> remoteId;
        stream >> resource;
        stream >> parentCollection;
        stream >> parentDestCollection;
        stream >> mimeType;
        stream >> itemParts;

        Protocol::FetchItemsResponse item;
        item.setId(uid);
        item.setRemoteId(remoteId);
        item.setMimeType(mimeType);
        items.push_back(std::move(item));
        msg->addMetadata("FETCH_ITEM");
    } else if (version >= 2) {
        stream >> operation;
        stream >> entityCnt;
        if (version >= 7) {
            QByteArray ba;
            qint64 i64;
            int i;
            QDateTime dt;
            QString str;
            QVector<QByteArray> bav;
            QVector<qint64> i64v;
            QMap<QByteArray, QByteArray> babaMap;
            int cnt;
            for (int j = 0; j < entityCnt; ++j) {
                Protocol::FetchItemsResponse item;
                stream >> i64;
                item.setId(i64);
                stream >> i;
                item.setRevision(i);
                stream >> i64;
                item.setParentId(i64);
                stream >> str;
                item.setRemoteId(str);
                stream >> str;
                item.setRemoteRevision(str);
                stream >> str;
                item.setGid(str);
                stream >> i64;
                item.setSize(i64);
                stream >> str;
                item.setMimeType(str);
                stream >> dt;
                item.setMTime(dt);
                stream >> bav;
                item.setFlags(bav);
                stream >> cnt;
                QVector<Protocol::FetchTagsResponse> tags;
                for (int k = 0; k < cnt; ++k) {
                    Protocol::FetchTagsResponse tag;
                    stream >> i64;
                    tag.setId(i64);
                    stream >> i64;
                    tag.setParentId(i64);
                    stream >> ba;
                    tag.setGid(ba);
                    stream >> ba;
                    tag.setType(ba);
                    stream >> ba;
                    tag.setRemoteId(ba);
                    stream >> babaMap;
                    tag.setAttributes(babaMap);
                    tags << tag;
                }
                item.setTags(tags);
                stream >> i64v;
                item.setVirtualReferences(i64v);
                stream >> cnt;
                QVector<Protocol::FetchRelationsResponse> relations;
                for (int k = 0; k < cnt; ++k) {
                    Protocol::FetchRelationsResponse relation;
                    stream >> i64;
                    relation.setLeft(i64);
                    stream >> ba;
                    relation.setLeftMimeType(ba);
                    stream >> i64;
                    relation.setRight(i64);
                    stream >> ba;
                    relation.setRightMimeType(ba);
                    stream >> ba;
                    relation.setType(ba);
                    stream >> ba;
                    relation.setRemoteId(ba);
                    relations << relation;
                }
                item.setRelations(relations);
                stream >> cnt;
                QVector<Protocol::Ancestor> ancestors;
                for (int k = 0; k < cnt; ++k) {
                    Protocol::Ancestor ancestor;
                    stream >> i64;
                    ancestor.setId(i64);
                    stream >> str;
                    ancestor.setRemoteId(str);
                    stream >> str;
                    ancestor.setName(str);
                    stream >> babaMap;
                    ancestor.setAttributes(babaMap);
                    ancestors << ancestor;
                }
                item.setAncestors(ancestors);
                stream >> cnt;
                QVector<Protocol::StreamPayloadResponse> parts;
                for (int k = 0; k < cnt; ++k) {
                    Protocol::StreamPayloadResponse part;
                    stream >> ba;
                    part.setPayloadName(ba);
                    Protocol::PartMetaData metaData;
                    stream >> ba;
                    metaData.setName(ba);
                    stream >> i64;
                    metaData.setSize(i64);
                    stream >> i;
                    metaData.setVersion(i);
                    stream >> i;
                    metaData.setStorageType(static_cast<Protocol::PartMetaData::StorageType>(i));
                    part.setMetaData(metaData);
                    stream >> ba;
                    part.setData(ba);
                    parts << part;
                }
                item.setParts(parts);
                stream >> bav;
                item.setCachedParts(bav);
                items.push_back(std::move(item));
            }
        } else {
            for (int j = 0; j < entityCnt; ++j) {
                stream >> uid;
                stream >> remoteId;
                stream >> remoteRevision;
                stream >> mimeType;
                if (stream.status() != QDataStream::Ok) {
                    qCWarning(AKONADICORE_LOG) << "Error reading saved notifications! Aborting";
                    return msg;
                }
                Protocol::FetchItemsResponse item;
                item.setId(uid);
                item.setRemoteId(remoteId);
                item.setRemoteRevision(remoteRevision);
                item.setMimeType(mimeType);
                items.push_back(std::move(item));
            }
            msg->addMetadata("FETCH_ITEM");
        }
        stream >> resource;
        stream >> destinationResource;
        stream >> parentCollection;
        stream >> parentDestCollection;
        stream >> itemParts;
        stream >> addedFlags;
        stream >> removedFlags;
        if (version >= 3) {
            stream >> addedTags;
            stream >> removedTags;
        }
    } else {
        qCWarning(AKONADICORE_LOG) << "Error version is not correct here" << version;
        return msg;
    }
    if (version >= 5) {
        msg->setOperation(static_cast<Protocol::ItemChangeNotification::Operation>(operation));
    } else {
        msg->setOperation(mapItemOperation(static_cast<LegacyOp>(operation)));
    }
    msg->setItems(items);
    msg->setResource(resource);
    msg->setDestinationResource(destinationResource);
    msg->setParentCollection(parentCollection);
    msg->setParentDestCollection(parentDestCollection);
    msg->setItemParts(itemParts);
    msg->setAddedRelations(extractRelations(addedFlags));
    msg->setAddedFlags(addedFlags);
    msg->setRemovedRelations(extractRelations(removedFlags));
    msg->setRemovedFlags(removedFlags);
    msg->setAddedTags(addedTags);
    msg->setRemovedTags(removedTags);
    return msg;
}

QSet<QByteArray> ChangeRecorderPrivate::encodeRelations(const QSet<Protocol::ItemChangeNotification::Relation> &relations) const
{
    QSet<QByteArray> rv;
    for (const auto &rel : relations) {
        rv.insert("RELATION " + rel.type.toLatin1() + ' ' + QByteArray::number(rel.leftId) + ' ' + QByteArray::number(rel.rightId));
    }
    return rv;
}

void ChangeRecorderPrivate::saveItemNotification(QDataStream &stream, const Protocol::ItemChangeNotification &msg)
{
    // Version 7

    stream << int(msg.operation());
    const auto &items = msg.items();
    stream << items.count();
    for (const auto &item : items) {
        stream << item.id()
               << item.revision()
               << item.parentId()
               << item.remoteId()
               << item.remoteRevision()
               << item.gid()
               << item.size()
               << item.mimeType()
               << item.mTime()
               << item.flags();
        const auto tags = item.tags();
        stream << tags.count();
        for (const auto &tag : tags) {
            stream << tag.id()
                   << tag.parentId()
                   << tag.gid()
                   << tag.type()
                   << tag.remoteId()
                   << tag.attributes();
        }
        stream << item.virtualReferences();
        const auto relations = item.relations();
        stream << relations.count();
        for (const auto &relation : relations) {
            stream << relation.left()
                   << relation.leftMimeType()
                   << relation.right()
                   << relation.rightMimeType()
                   << relation.type()
                   << relation.remoteId();
        }
        const auto ancestors = item.ancestors();
        stream << ancestors.count();
        for (const auto &ancestor : ancestors) {
            stream << ancestor.id()
                   << ancestor.remoteId()
                   << ancestor.name()
                   << ancestor.attributes();
        }
        const auto parts = item.parts();
        stream << parts.count();
        for (const auto &part : parts) {
            const auto metaData = part.metaData();
            stream << part.payloadName()
                   << metaData.name()
                   << metaData.size()
                   << metaData.version()
                   << static_cast<int>(metaData.storageType())
                   << part.data();
        }
        stream << item.cachedParts();
    }
    stream << msg.resource();
    stream << msg.destinationResource();
    stream << quint64(msg.parentCollection());
    stream << quint64(msg.parentDestCollection());
    stream << msg.itemParts();
    stream << msg.addedFlags() + encodeRelations(msg.addedRelations());
    stream << msg.removedFlags() + encodeRelations(msg.removedRelations());
    stream << msg.addedTags();
    stream << msg.removedTags();
}

Protocol::ChangeNotificationPtr ChangeRecorderPrivate::loadCollectionNotification(QDataStream &stream, quint64 version) const
{
    QByteArray resource, destinationResource;
    int operation, entityCnt;
    quint64 uid, parentCollection, parentDestCollection;
    QString remoteId, remoteRevision, dummyString;
    QSet<QByteArray> changedParts, dummyBa;
    QSet<qint64> dummyIv;

    auto msg = Protocol::CollectionChangeNotificationPtr::create();

    if (version == 1) {
        stream >> operation;
        stream >> uid;
        stream >> remoteId;
        stream >> resource;
        stream >> parentCollection;
        stream >> parentDestCollection;
        stream >> dummyString;
        stream >> changedParts;

        Protocol::FetchCollectionsResponse collection;
        collection.setId(uid);
        collection.setRemoteId(remoteId);
        msg->setCollection(std::move(collection));
        msg->addMetadata("FETCH_COLLECTION");
    } else if (version >= 2) {
        stream >> operation;
        stream >> entityCnt;
        if (version >= 7) {
            QString str;
            QStringList stringList;
            qint64 i64;
            QVector<qint64> vb;
            QMap<QByteArray, QByteArray> attrs;
            bool b;
            int i;
            Tristate tristate;
            Protocol::FetchCollectionsResponse collection;
            stream >> uid;
            collection.setId(uid);
            stream >> uid;
            collection.setParentId(uid);
            stream >> str;
            collection.setName(str);
            stream >> stringList;
            collection.setMimeTypes(stringList);
            stream >> str;
            collection.setRemoteId(str);
            stream >> str;
            collection.setRemoteRevision(str);
            stream >> str;
            collection.setResource(str);

            Protocol::FetchCollectionStatsResponse stats;
            stream >> i64;
            stats.setCount(i64);
            stream >> i64;
            stats.setUnseen(i64);
            stream >> i64;
            stats.setSize(i64);
            collection.setStatistics(stats);

            stream >> str;
            collection.setSearchQuery(str);
            stream >> vb;
            collection.setSearchCollections(vb);
            stream >> entityCnt;
            QVector<Protocol::Ancestor> ancestors;
            for (int i = 0; i < entityCnt; ++i) {
                Protocol::Ancestor ancestor;
                stream >> i64;
                ancestor.setId(i64);
                stream >> str;
                ancestor.setRemoteId(str);
                stream >> str;
                ancestor.setName(str);
                stream >> attrs;
                ancestor.setAttributes(attrs);
                ancestors.push_back(ancestor);

                if (stream.status() != QDataStream::Ok) {
                    qCWarning(AKONADICORE_LOG) << "Erorr reading saved notifications! Aborting";
                    return msg;
                }
            }
            collection.setAncestors(ancestors);

            Protocol::CachePolicy cachePolicy;
            stream >> b;
            cachePolicy.setInherit(b);
            stream >> i;
            cachePolicy.setCheckInterval(i);
            stream >> i;
            cachePolicy.setCacheTimeout(i);
            stream >> b;
            cachePolicy.setSyncOnDemand(b);
            stream >> stringList;
            cachePolicy.setLocalParts(stringList);
            collection.setCachePolicy(cachePolicy);

            stream >> attrs;
            collection.setAttributes(attrs);
            stream >> b;
            collection.setEnabled(b);
            stream >> reinterpret_cast<qint8&>(tristate);
            collection.setDisplayPref(tristate);
            stream >> reinterpret_cast<qint8&>(tristate);
            collection.setSyncPref(tristate);
            stream >> reinterpret_cast<qint8&>(tristate);
            collection.setIndexPref(tristate);
            stream >> b;
            collection.setReferenced(b);
            stream >> b;
            collection.setIsVirtual(b);

            msg->setCollection(std::move(collection));
        } else {
            for (int j = 0; j < entityCnt; ++j) {
                stream >> uid;
                stream >> remoteId;
                stream >> remoteRevision;
                stream >> dummyString;
                if (stream.status() != QDataStream::Ok) {
                    qCWarning(AKONADICORE_LOG) << "Error reading saved notifications! Aborting";
                    return msg;
                }
                Protocol::FetchCollectionsResponse collection;
                collection.setId(uid);
                collection.setRemoteId(remoteId);
                collection.setRemoteRevision(remoteRevision);
                msg->setCollection(std::move(collection));
                msg->addMetadata("FETCH_COLLECTION");
            }
        }
        stream >> resource;
        stream >> destinationResource;
        stream >> parentCollection;
        stream >> parentDestCollection;
        stream >> changedParts;
        stream >> dummyBa;
        stream >> dummyBa;
        if (version >= 3) {
            stream >> dummyIv;
            stream >> dummyIv;
        }
    } else {
        qCWarning(AKONADICORE_LOG) << "Error version is not correct here" << version;
        return msg;
     }

    if (version >= 5) {
        msg->setOperation(static_cast<Protocol::CollectionChangeNotification::Operation>(operation));
    } else {
        msg->setOperation(mapCollectionOperation(static_cast<LegacyOp>(operation)));
    }
    msg->setResource(resource);
    msg->setDestinationResource(destinationResource);
    msg->setParentCollection(parentCollection);
    msg->setParentDestCollection(parentDestCollection);
    msg->setChangedParts(changedParts);
    return msg;
}

void Akonadi::ChangeRecorderPrivate::saveCollectionNotification(QDataStream &stream, const Protocol::CollectionChangeNotification &msg)
{
    // Version 7

    const auto &col = msg.collection();

    stream << int(msg.operation());
    stream << int(1);
    stream << col.id();
    stream << col.parentId();
    stream << col.name();
    stream << col.mimeTypes();
    stream << col.remoteId();
    stream << col.remoteRevision();
    stream << col.resource();
    const auto stats = col.statistics();
    stream << stats.count();
    stream << stats.unseen();
    stream << stats.size();
    stream << col.searchQuery();
    stream << col.searchCollections();
    const auto ancestors = col.ancestors();
    stream << ancestors.count();
    for (const auto &ancestor : ancestors) {
        stream << ancestor.id()
               << ancestor.remoteId()
               << ancestor.name()
               << ancestor.attributes();
    }
    const auto cachePolicy = col.cachePolicy();
    stream << cachePolicy.inherit();
    stream << cachePolicy.checkInterval();
    stream << cachePolicy.cacheTimeout();
    stream << cachePolicy.syncOnDemand();
    stream << cachePolicy.localParts();
    stream << col.attributes();
    stream << col.enabled();
    stream << static_cast<qint8>(col.displayPref());
    stream << static_cast<qint8>(col.syncPref());
    stream << static_cast<qint8>(col.indexPref());
    stream << col.referenced();
    stream << col.isVirtual();


    stream << msg.resource();
    stream << msg.destinationResource();
    stream << quint64(msg.parentCollection());
    stream << quint64(msg.parentDestCollection());
    stream << msg.changedParts();
    stream << QSet<QByteArray>();
    stream << QSet<QByteArray>();
    stream << QSet<qint64>();
    stream << QSet<qint64>();
}

Protocol::ChangeNotificationPtr ChangeRecorderPrivate::loadTagNotification(QDataStream &stream, quint64 version) const
{
    QByteArray resource, dummyBa;
    int operation, entityCnt;
    quint64 uid, dummyI;
    QString remoteId, dummyString;
    QSet<QByteArray> dummyBaV;
    QSet<qint64> dummyIv;

    auto msg = Protocol::TagChangeNotificationPtr::create();

    if (version == 1) {
        stream >> operation;
        stream >> uid;
        stream >> remoteId;
        stream >> dummyBa;
        stream >> dummyI;
        stream >> dummyI;
        stream >> dummyString;
        stream >> dummyBaV;

        Protocol::FetchTagsResponse tag;
        tag.setId(uid);
        tag.setRemoteId(remoteId.toLatin1());
        msg->setTag(std::move(tag));
        msg->addMetadata("FETCH_TAG");
    } else if (version >= 2) {
        stream >> operation;
        stream >> entityCnt;
        if (version >= 7) {
            QByteArray ba;
            QMap<QByteArray, QByteArray> attrs;

            Protocol::FetchTagsResponse tag;

            stream >> uid;
            tag.setId(uid);
            stream >> ba;
            tag.setParentId(uid);
            stream >> attrs;
            tag.setGid(ba);
            stream >> ba;
            tag.setType(ba);
            stream >> uid;
            tag.setRemoteId(ba);
            stream >> ba;
            tag.setAttributes(attrs);
            msg->setTag(std::move(tag));

            stream >> resource;
        } else {
            for (int j = 0; j < entityCnt; ++j) {
                stream >> uid;
                stream >> remoteId;
                stream >> dummyString;
                stream >> dummyString;
                if (stream.status() != QDataStream::Ok) {
                    qCWarning(AKONADICORE_LOG) << "Error reading saved notifications! Aborting";
                    return msg;
                }
                Protocol::FetchTagsResponse tag;
                tag.setId(uid);
                tag.setRemoteId(remoteId.toLatin1());
                msg->setTag(std::move(tag));
                msg->addMetadata("FETCH_TAG");
            }
            stream >> resource;
            stream >> dummyBa;
            stream >> dummyI;
            stream >> dummyI;
            stream >> dummyBaV;
            stream >> dummyBaV;
            stream >> dummyBaV;
            if (version >= 3) {
                stream >> dummyIv;
                stream >> dummyIv;
            }
        }
        if (version >= 5) {
            msg->setOperation(static_cast<Protocol::TagChangeNotification::Operation>(operation));
        } else {
            msg->setOperation(mapTagOperation(static_cast<LegacyOp>(operation)));
        }
    }
    msg->setResource(resource);
    return msg;
}

void Akonadi::ChangeRecorderPrivate::saveTagNotification(QDataStream &stream, const Protocol::TagChangeNotification &msg)
{
    const auto &tag = msg.tag();
    stream << int(msg.operation());
    stream << int(1);
    stream << tag.id();
    stream << tag.parentId();
    stream << tag.gid();
    stream << tag.type();
    stream << tag.remoteId();
    stream << tag.attributes();
    stream << msg.resource();
}

Protocol::ChangeNotificationPtr ChangeRecorderPrivate::loadRelationNotification(QDataStream &stream, quint64 version) const
{
    QByteArray dummyBa;
    int operation, entityCnt;
    quint64 dummyI;
    QString dummyString;
    QSet<QByteArray> itemParts, dummyBaV;
    QSet<qint64> dummyIv;

    auto msg = Protocol::RelationChangeNotificationPtr::create();

    if (version == 1) {
        qCWarning(AKONADICORE_LOG) << "Invalid version of relation notification";
        return msg;
    } else if (version >= 2) {
        stream >> operation;
        stream >> entityCnt;
        if (version >= 7) {
            Protocol::FetchRelationsResponse relation;
            qint64 i64;
            QByteArray ba;
            stream >> i64;
            relation.setLeft(i64);
            stream >> ba;
            relation.setLeftMimeType(ba);
            stream >> i64;
            relation.setRight(i64);
            stream >>ba;
            relation.setRightMimeType(ba);
            stream >> ba;
            relation.setRemoteId(ba);
            stream >> ba;
            relation.setType(ba);

            msg->setRelation(std::move(relation));

        } else {
            for (int j = 0; j < entityCnt; ++j) {
                stream >> dummyI;
                stream >> dummyString;
                stream >> dummyString;
                stream >> dummyString;
                if (stream.status() != QDataStream::Ok) {
                    qCWarning(AKONADICORE_LOG) << "Error reading saved notifications! Aborting";
                    return msg;
                }
            }
            stream >> dummyBa;
            if (version == 5) {
                // there was a bug in version 5 serializer that serialized this
                // field as qint64 (8 bytes) instead of empty QByteArray (which is
                // 4 bytes)
                stream >> dummyI;
            } else {
                stream >> dummyBa;
            }
            stream >> dummyI;
            stream >> dummyI;
            stream >> itemParts;
            stream >> dummyBaV;
            stream >> dummyBaV;
            if (version >= 3) {
                stream >> dummyIv;
                stream >> dummyIv;
            }

            Protocol::FetchRelationsResponse relation;
            for (const QByteArray &part : qAsConst(itemParts)) {
                const QByteArrayList p = part.split(' ');
                if (p.size() < 2) {
                    continue;
                }
                if (p[0] == "LEFT") {
                    relation.setLeft(p[1].toLongLong());
                } else if (p[0] == "RIGHT") {
                    relation.setRight(p[1].toLongLong());
                } else if (p[0] == "RID") {
                    relation.setRemoteId(p[1]);
                } else if (p[0] == "TYPE") {
                    relation.setType(p[1]);
                }
            }
            msg->setRelation(std::move(relation));
        }
        if (version >= 5) {
            msg->setOperation(static_cast<Protocol::RelationChangeNotification::Operation>(operation));
        } else {
            msg->setOperation(mapRelationOperation(static_cast<LegacyOp>(operation)));
        }
    }

    return msg;
}

void Akonadi::ChangeRecorderPrivate::saveRelationNotification(QDataStream &stream, const Protocol::RelationChangeNotification &msg)
{
    const auto &rel = msg.relation();
    stream << int(msg.operation());
    stream << int(0);
    stream << rel.left();
    stream << rel.leftMimeType();
    stream << rel.right();
    stream << rel.rightMimeType();
    stream << rel.remoteId();
    stream << rel.type();
}

Protocol::ItemChangeNotification::Operation ChangeRecorderPrivate::mapItemOperation(LegacyOp op) const
{
    switch (op) {
    case Add:
        return Protocol::ItemChangeNotification::Add;
    case Modify:
        return Protocol::ItemChangeNotification::Modify;
    case Move:
        return Protocol::ItemChangeNotification::Move;
    case Remove:
        return Protocol::ItemChangeNotification::Remove;
    case Link:
        return Protocol::ItemChangeNotification::Link;
    case Unlink:
        return Protocol::ItemChangeNotification::Unlink;
    case ModifyFlags:
        return Protocol::ItemChangeNotification::ModifyFlags;
    case ModifyTags:
        return Protocol::ItemChangeNotification::ModifyTags;
    case ModifyRelations:
        return Protocol::ItemChangeNotification::ModifyRelations;
    default:
        qWarning() << "Unexpected operation type in item notification";
        return Protocol::ItemChangeNotification::InvalidOp;
    }
}

Protocol::CollectionChangeNotification::Operation ChangeRecorderPrivate::mapCollectionOperation(LegacyOp op) const
{
    switch (op) {
    case Add:
        return Protocol::CollectionChangeNotification::Add;
    case Modify:
        return Protocol::CollectionChangeNotification::Modify;
    case Move:
        return Protocol::CollectionChangeNotification::Move;
    case Remove:
        return Protocol::CollectionChangeNotification::Remove;
    case Subscribe:
        return Protocol::CollectionChangeNotification::Subscribe;
    case Unsubscribe:
        return Protocol::CollectionChangeNotification::Unsubscribe;
    default:
        qCWarning(AKONADICORE_LOG) << "Unexpected operation type in collection notification";
        return Protocol::CollectionChangeNotification::InvalidOp;
    }
}

Protocol::TagChangeNotification::Operation ChangeRecorderPrivate::mapTagOperation(LegacyOp op) const
{
    switch (op) {
    case Add:
        return Protocol::TagChangeNotification::Add;
    case Modify:
        return Protocol::TagChangeNotification::Modify;
    case Remove:
        return Protocol::TagChangeNotification::Remove;
    default:
        qCWarning(AKONADICORE_LOG) << "Unexpected operation type in tag notification";
        return Protocol::TagChangeNotification::InvalidOp;
    }
}

Protocol::RelationChangeNotification::Operation ChangeRecorderPrivate::mapRelationOperation(LegacyOp op) const
{
    switch (op) {
    case Add:
        return Protocol::RelationChangeNotification::Add;
    case Remove:
        return Protocol::RelationChangeNotification::Remove;
    default:
        qCWarning(AKONADICORE_LOG) << "Unexpected operation type in relation notification";
        return Protocol::RelationChangeNotification::InvalidOp;
    }
}

ChangeRecorderPrivate::LegacyType ChangeRecorderPrivate::mapToLegacyType(Protocol::Command::Type type) const
{
    switch (type) {
    case Protocol::Command::ItemChangeNotification:
        return Item;
    case Protocol::Command::CollectionChangeNotification:
        return Collection;
    case Protocol::Command::TagChangeNotification:
        return Tag;
    case Protocol::Command::RelationChangeNotification:
        return Relation;
    default:
        qCWarning(AKONADICORE_LOG) << "Unexpected notification type";
        return InvalidType;
    }
}
