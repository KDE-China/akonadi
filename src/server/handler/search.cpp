/***************************************************************************
 *   Copyright (C) 2009 by Tobias Koenig <tokoe@kde.org>                   *
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

#include "search.h"

#include "connection.h"
#include "fetchhelper.h"
#include "handlerhelper.h"
#include "searchhelper.h"
#include "search/searchrequest.h"
#include "search/searchmanager.h"

using namespace Akonadi;
using namespace Akonadi::Server;

bool Search::parseStream()
{
    Protocol::SearchCommand cmd;
    mInStream >> cmd;

    if (cmd.query().isEmpty()) {
        return failureResponse("No query specified");
    }

    QVector<qint64> collectionIds, collections;
    bool recursive = cmd.recursive();

    if (cmd.collections().isEmpty()) {
        collectionIds << 0;
        recursive = true;
    }

    if (recursive) {
        collections << SearchHelper::matchSubcollectionsByMimeType(collectionIds, cmd.mimeTypes());
    } else {
        collections = collectionIds;
    }

    akDebug() << "SEARCH:";
    akDebug() << "\tQuery:" << cmd.query();
    akDebug() << "\tMimeTypes:" << cmd.mimeTypes();
    akDebug() << "\tCollections:" << collections;
    akDebug() << "\tRemote:" << cmd.remote();
    akDebug() << "\tRecursive" << recursive;

    if (collections.isEmpty()) {
        return successResponse<Protocol::SearchResponse>();
    }

    mFetchScope = cmd.fetchScope();

    SearchRequest request(connection()->sessionId());
    request.setCollections(collections);
    request.setMimeTypes(cmd.mimeTypes());
    request.setQuery(cmd.query());
    request.setRemoteSearch(cmd.remote());
    connect(&request, SIGNAL(resultsAvailable(QSet<qint64>)),
            this, SLOT(slotResultsAvailable(QSet<qint64>)));
    request.exec();

    //akDebug() << "\tResult:" << uids;
    akDebug() << "\tResult:" << mAllResults.count() << "matches";

    return successResponse<Protocol::SearchResponse>();
}

void Search::slotResultsAvailable(const QSet<qint64> &results)
{
    QSet<qint64> newResults = results;
    newResults.subtract(mAllResults);
    mAllResults.unite(newResults);

    if (newResults.isEmpty()) {
        return;
    }

    ImapSet imapSet;
    imapSet.add(newResults);

    Scope scope;
    scope.setUidSet(imapSet);

    FetchHelper fetchHelper(connection(), scope, mFetchScope);
    fetchHelper.fetchItems();
}
