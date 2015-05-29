/*
    Copyright (c) 2009 Volker Krause <vkrause@kde.org>

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

#include "move.h"

#include "connection.h"
#include "entities.h"
#include "imapstreamparser.h"
#include "handlerhelper.h"
#include "cachecleaner.h"
#include "storage/datastore.h"
#include "storage/itemretriever.h"
#include "storage/itemqueryhelper.h"
#include "storage/selectquerybuilder.h"
#include "storage/transaction.h"
#include "storage/collectionqueryhelper.h"

#include <private/protocol_p.h>

using namespace Akonadi;
using namespace Akonadi::Server;

bool Move::parseStream()
{
    Protocol::MoveItemsCommand cmd;
    mInStream >> cmd;

    const Collection destination = HandlerHelper::collectionFromScope(cmd.destination(), connection());
    if (destination.isVirtual()) {
        return failureResponse("Moving items into virtual collection is not allowed");
    }

    if (cmd.scope().scope() == Scope::Rid) {
        if (cmd.scope().ridContext() <= 0) {
            return failureResponse("RID move requires valid source collection");
        }

        connection()->context()->setCollection(Collection::retrieveById(cmd.scope().ridContext()));
    }

    CacheCleanerInhibitor inhibitor;

    // make sure all the items we want to move are in the cache
    ItemRetriever retriever(connection());
    retriever.setScope(cmd.scope());
    retriever.setRetrieveFullPayload(true);
    if (!retriever.exec()) {
        return failureResponse(retriever.lastError());
    }

    DataStore *store = connection()->storageBackend();
    Transaction transaction(store);

    SelectQueryBuilder<PimItem> qb;
    ItemQueryHelper::scopeToQuery(cmd.scope(), connection()->context(), qb);
    qb.addValueCondition(PimItem::collectionIdFullColumnName(), Query::NotEquals, destination.id());

    const QDateTime mtime = QDateTime::currentDateTime();

    if (qb.exec()) {
        const QVector<PimItem> items = qb.result();
        if (items.isEmpty()) {
            return failureResponse("No items found");
        }

        // Split the list by source collection
        QMap<Entity::Id /* collection */, PimItem> toMove;
        QMap<Entity::Id /* collection */, Collection> sources;
        Q_FOREACH (/*sic!*/ PimItem item, items) {
            const Collection source = items.first().collection();
            if (!source.isValid()) {
                return failureResponse("Item without collection found!?");
            }
            if (!sources.contains(source.id())) {
                sources.insert(source.id(), source);
            }

            if (!item.isValid()) {
                return failureResponse("Invalid item in result set!?");
            }
            Q_ASSERT(item.collectionId() != destination.id());

            item.setCollectionId(destination.id());
            item.setAtime(mtime);
            item.setDatetime(mtime);
            // if the resource moved itself, we assume it did so because the change happend in the backend
            if (connection()->context()->resource().id() != destination.resourceId()) {
                item.setDirty(true);
            }

            toMove.insertMulti(source.id(), item);
        }

        // Emit notification for each source collection separately
        for (const Entity::Id &sourceId : toMove.uniqueKeys()) {
            const PimItem::List &itemsToMove = toMove.values(sourceId).toVector();
            const Collection &source = sources.value(sourceId);
            store->notificationCollector()->itemsMoved(itemsToMove, source, destination);

            for (PimItem &moved : toMove.values(sourceId)) {
                // reset RID on inter-resource moves, but only after generating the change notification
                // so that this still contains the old one for the source resource
                const bool isInterResourceMove = moved.collection().resource().id() != source.resource().id();
                if (isInterResourceMove) {
                    moved.setRemoteId(QString());
                }

                // FIXME Could we aggregate the changes to a single SQL query?
                if (!moved.update()) {
                    return failureResponse("Unable to update item");
                }
            }
        }
    } else {
        return failureResponse("Unable to execute query");
    }

    if (!transaction.commit()) {
        return failureResponse("Unable to commit transaction.");
    }

    return successResponse<Protocol::MoveItemsResponse>();
}
