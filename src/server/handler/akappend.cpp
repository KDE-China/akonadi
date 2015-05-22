/***************************************************************************
 *   Copyright (C) 2007 by Robert Zwerus <arzie@dds.nl>                    *
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

#include "akappend.h"

#include <private/imapparser_p.h>
#include <private/protocol_p.h>

#include "imapstreamparser.h"

#include "response.h"
#include "handlerhelper.h"

#include "akonadi.h"
#include "connection.h"
#include "preprocessormanager.h"
#include "storage/datastore.h"
#include "storage/entity.h"
#include "storage/transaction.h"
#include "storage/parttypehelper.h"
#include "storage/dbconfig.h"
#include "storage/partstreamer.h"
#include "storage/parthelper.h"
#include "storage/selectquerybuilder.h"

#include <QtCore/QDebug>

using namespace Akonadi;
using namespace Akonadi::Server;

static QVector<QByteArray> localFlagsToPreserve = QVector<QByteArray>() << "$ATTACHMENT"
                                                                        << "$INVITATION"
                                                                        << "$ENCRYPTED"
                                                                        << "$SIGNED"
                                                                        << "$WATCHED";

bool AkAppend::buildPimItem(Protocol::CreateItemCommand &cmd, PimItem &item,
                            Collection &parentCol)
{
    MimeType mimeType = MimeType::retrieveByName(cmd.mimeType());
    if (!mimeType.isValid()) {
        MimeType m(cmd.mimeType());
        if (!m.insert()) {
            return failureResponse<Protocol::CreateItemResponse>(
                QStringLiteral("Unable to create mimetype '") % cmd.mimeType() % QStringLiteral("'."));
        }
        mimeType = m;
    }

    parentCol = HandlerHelper::collectionFromScope(cmd.collection());
    if (!parentCol.isValid()) {
        return failureResponse<Protocol::CreateItemResponse>(
            QStringLiteral("Invalid parent collection"));
    }

    item.setRev(0);
    item.setSize(cmd.itemSize());
    item.setMimeTypeId(mimeType.id());
    item.setCollectionId(parentCol.id());
    item.setDatetime(cmd.dateTime());
    if (cmd.remoteId().isEmpty()) {
        // from application
        item.setDirty(true);
    } else {
        // from resource
        item.setRemoteId(cmd.remoteId());
        item.setDirty(false);
    }
    item.setRemoteRevision(cmd.remoteRevision());
    item.setGid(cmd.gid());
    item.setAtime(QDateTime::currentDateTime());

    return true;
}

bool AkAppend::insertItem(Protocol::CreateItemCommand &cmd, PimItem &item,
                          const Collection &parentCol)
{
    if (!item.insert()) {
        return failureResponse<Protocol::CreateItemResponse>(
            QStringLiteral("Failed to append item"));
    }

    // set message flags
    // This will hit an entry in cache inserted there in buildPimItem()
    const Flag::List flagList = HandlerHelper::resolveFlags(cmd.flags());
    bool flagsChanged = false;
    if (!DataStore::self()->appendItemsFlags(PimItem::List() << item, flagList, &flagsChanged, false, parentCol, true)) {
        return failureResponse("Unable to append item flags.");
    }

    const Tag::List tagList = HandlerHelper::resolveTags(cmd.tags(), connection()->context());
    bool tagsChanged = false;
    if (!DataStore::self()->appendItemsTags(PimItem::List() << item, tagList, &tagsChanged, false, parentCol, true)) {
        return failureResponse("Unable to append item tags.");
    }

    // Handle individual parts
    qint64 partSizes = 0;
    PartStreamer streamer(connection(), item, this);

    for (Protocol::PartMetaData part : cmd.parts()) {
        if (!streamer.stream(true, part)) {
            return failureResponse<Protocol::CreateItemResponse>(streamer.error());
        }
        partSizes += part.size();
    }

    // TODO: Try to avoid this addition query
    if (partSizes > item.size()) {
        item.setSize(partSizes);
        item.update();
    }

    // Preprocessing
    if (PreprocessorManager::instance()->isActive()) {
        Part hiddenAttribute;
        hiddenAttribute.setPimItemId(item.id());
        hiddenAttribute.setPartType(PartTypeHelper::fromFqName(QString::fromLatin1(AKONADI_ATTRIBUTE_HIDDEN)));
        hiddenAttribute.setData(QByteArray());
        hiddenAttribute.setDatasize(0);
        // TODO: Handle errors? Technically, this is not a critical issue as no data are lost
        PartHelper::insert(&hiddenAttribute);
    }

    notify(item, item.collection());
    mOutStream << HandlerHelper::itemFetchResponse(item);

    return true;
}

bool AkAppend::mergeItem(const Protocol::CreateItemCommand &cmd,
                         PimItem &newItem, PimItem &currentItem,
                         const Collection &parentCol)
{
    QSet<QByteArray> changedParts;

    if (newItem.rev() > 0) {
        currentItem.setRev(newItem.rev());
    }
    if (!newItem.remoteId().isEmpty() && currentItem.remoteId() != newItem.remoteId()) {
        currentItem.setRemoteId(newItem.remoteId());
        changedParts.insert(AKONADI_PARAM_REMOTEID);
    }
    if (!newItem.remoteRevision().isEmpty() && currentItem.remoteRevision() != newItem.remoteRevision()) {
        currentItem.setRemoteRevision(newItem.remoteRevision());
        changedParts.insert(AKONADI_PARAM_REMOTEREVISION);
    }
    if (!newItem.gid().isEmpty() && currentItem.gid() != newItem.gid()) {
        currentItem.setGid(newItem.gid());
        changedParts.insert(AKONADI_PARAM_GID);
    }
    if (newItem.datetime().isValid()) {
        currentItem.setDatetime(newItem.datetime());
    }
    currentItem.setAtime(QDateTime::currentDateTime());

    // Only mark dirty when merged from application
    currentItem.setDirty(!connection()->context()->resource().isValid());

    const Collection col = Collection::retrieveById(newItem.collectionId());
    if (cmd.flags().isEmpty()) {
        bool flagsAdded = false, flagsRemoved = false;
        if (!cmd.addedFlags().isEmpty()) {
            const Flag::List addedFlags = HandlerHelper::resolveFlags(cmd.addedFlags());
            DataStore::self()->appendItemsFlags(PimItem::List() << currentItem, addedFlags,
                                                &flagsAdded, true, col, true);
        }
        if (!cmd.removedFlags().isEmpty()) {
            const Flag::List removedFlags = HandlerHelper::resolveFlags(cmd.removedFlags());
            DataStore::self()->removeItemsFlags(PimItem::List() << currentItem, removedFlags,
                                                &flagsRemoved, col, true);
        }
        if (flagsAdded || flagsRemoved) {
            changedParts.insert(AKONADI_PARAM_FLAGS);
        }
    } else if (!cmd.flags().isEmpty()) {
        bool flagsChanged = false;
        QVector<QByteArray> flagNames = cmd.flags();

        // Make sure we don't overwrite some local-only flags that can't come
        // through from Resource during ItemSync, like $ATTACHMENT, because the
        // resource is not aware of them (they are usually assigned by client
        // upon inspecting the payload)
        for (const QByteArray &preserve : localFlagsToPreserve) {
            flagNames.removeOne(preserve);
        }
        const Flag::List flags = HandlerHelper::resolveFlags(flagNames);
        DataStore::self()->setItemsFlags(PimItem::List() << currentItem, flags,
                                         &flagsChanged, col, true);
        if (flagsChanged) {
            changedParts.insert(AKONADI_PARAM_FLAGS);
        }
    }

    if (cmd.tags().isEmpty()) {
        bool tagsAdded = false, tagsRemoved = false;
        if (!cmd.addedTags().isEmpty()) {
            const Tag::List addedTags = HandlerHelper::tagsFromScope(cmd.addedTags(), connection()->context());
            DataStore::self()->appendItemsTags(PimItem::List() << currentItem, addedTags,
                                               &tagsAdded, true, col, true);
        }
        if (!cmd.removedTags().isEmpty()) {
            const Tag::List removedTags = HandlerHelper::tagsFromScope(cmd.removedTags(), connection()->context());
            DataStore::self()->removeItemsTags(PimItem::List() << currentItem, removedTags,
                                               &tagsRemoved, true);
        }

        if (tagsAdded || tagsRemoved) {
            changedParts.insert(AKONADI_PARAM_TAGS);
        }
    } else if (!cmd.tags().isEmpty()) {
        bool tagsChanged = false;
        const Tag::List tags = HandlerHelper::tagsFromScope(cmd.tags(), connection()->context());
        DataStore::self()->setItemsTags(PimItem::List() << currentItem, tags,
                                        &tagsChanged, true);
        if (tagsChanged) {
            changedParts.insert(AKONADI_PARAM_TAGS);
        }
    }

    const Part::List existingParts = Part::retrieveFiltered(Part::pimItemIdColumn(), currentItem.id());
    QMap<QByteArray, qint64> partsSizes;
    for (const Part &part : existingParts ) {
        partsSizes.insert(part.partType().name().toLatin1(), part.datasize());
    }

    if (!cmd.removedParts().isEmpty()) {
        DataStore::self()->removeItemParts(currentItem, cmd.removedParts());
        changedParts << cmd.removedParts();
        for (const QByteArray &removedPart : cmd.removedParts()) {
            partSizes.remove(removedPart);
        }
    }

    PartStreamer streamer(connection(), currentItem);

    for (Protocol::PartMetaData part : cmd.parts()) {
        bool changed = false;
        if (!streamer.stream(true, part, &changed)) {
            return failureResponse<Protocol::CreateItemResponse>(streamer.error());
        }

        if (changed) {
            changedParts << part.name();
            partsSizes.insert(part.name(), part.size());
        }
    }

    const qint64 size = std::accumulate(partsSizes.begin(), partsSizes.end(), 0);
    currentItem.setSize(size);

    // Store all changes
    if (!currentItem.update()) {
        return failureResponse<Protocol::CreateItemResponse>(
            QStringLiteral("Failed to store merged item"));
    }


    notify(currentItem, currentItem.collection(), changedParts);
    if (!(cmd.mergeMode() & Protocol::CreateItemCommand::Silent)) {
        mOutStream << HandlerHelper::itemFetchResponse(currentItem);
    }

    return true;
}

bool AkAppend::notify(const PimItem &item, const Collection &collection)
{
    DataStore::self()->notificationCollector()->itemAdded(item, collection);

    if (PreprocessorManager::instance()->isActive()) {
        // enqueue the item for preprocessing
        PreprocessorManager::instance()->beginHandleItem(item, DataStore::self());
    }
    return true;
}

bool AkAppend::notify(const PimItem &item, const Collection &collection,
                      const QSet<QByteArray> &changedParts)
{
    if (!changedParts.isEmpty()) {
        DataStore::self()->notificationCollector()->itemChanged(item, changedParts, collection);
    }
    return true;
}


bool AkAppend::parseStream()
{
    Protocol::CreateItemCommand cmd;
    cmd << mInStream;

    // FIXME: The streaming/reading of all item parts can hold the transaction for
    // unnecessary long time -> should we wrap the PimItem into one transaction
    // and try to insert Parts independently? In case we fail to insert a part,
    // it's not a problem as it can be re-fetched at any time, except for attributes.
    DataStore *db = DataStore::self();
    Transaction transaction(db);

    PimItem item;
    Collection parentCol;
    if (!buildPimItem(cmd, item, parentCol)) {
        return false;
    }

    if (cmd.mergeMode() == Protocol::CreateItemCommand::None) {
        if (!inserItem(cmd, item, parentCol)) {
            return false;
        }
        if (!transaction.commit()) {
            return failureResponse<Protocol::CreateItemResponse>(
                QStringLiteral("Failed to commit transaction"));
        }
    } else {
        // Merging is always restricted to the same collection and mimetype
        SelectQueryBuilder<PimItem> qb;
        qb.addValueCondition(PimItem::collectionIdColumn(), Query::Equals, parentCol.id());
        qb.addValueCondition(PimItem::mimeTypeIdColumn(), Query::Equals, item.mimeTypeId());
        if (cmd.mergeMode() & Protocol::CreateItemCommand::GID) {
            qb.addValueCondition(PimItem::gidColumn(), Query::Equals, item.gid());
        if (cmd.mergeMode() & Protocol::CreateItemCommand::RemoteID) {
            qb.addValueCondition(PimItem::remoteIdColumn(), Query::Equals, item.remoteId());
        }

        if (!qb.exec()) {
            return failureResponse<Protocol::CreateItemResponse>(
                QStringLiteral("Failed to query database for item"));
        }

        const QVector<PimItem> result = qb.result();
        if (result.count() == 0) {
            // No item with such GID/RID exists, so call AkAppend::insert() and behave
            // like if this was a new item
            if (!inserItem(cmd, item, parentCol)) {
                return false;
            }
            if (!transaction.commit()) {
                return failureResponse<Protocol::CreateItemResponse>(
                    QStringLiteral("Failed to commit transaction"));
            }
        } else if (result.count() == 1) {
            // Item with matching GID/RID combination exists, so merge this item into it
            // and send itemChanged()
            PimItem existingItem = result.at(0);

            if (!mergeItem(cmd, item, existingItem, parentCol)) {
                return false;
            }
            if (!transaction.commit()) {
                return failureResponse<Protocol::CreateItemResponse>(
                    QStringLiteral("Failed to commit transaction"));
            }
        } else {
            akDebug() << "Multiple merge candidates:";
            Q_FOREACH (const PimItem &item, result) {
                akDebug() << "\t ID: " << item.id() << ", RID:" << item.remoteId() << ", GID:" << item.gid();
            }
            // Nor GID or RID are guaranteed to be unique, so make sure we don't merge
            // something we don't want
            return failureResponse<Protocol::CreateItemResponse>(
                QStringLiteral("Multiple merge candidates, aborting"));
        }
    }

    mOutStream << Protocol::CreateItemResponse();
    return true;
}
