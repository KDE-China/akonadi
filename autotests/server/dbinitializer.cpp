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
#include "dbinitializer.h"

#include <shared/akdebug.h>
#include <storage/querybuilder.h>
#include <storage/datastore.h>

using namespace Akonadi;
using namespace Akonadi::Server;

DbInitializer::~DbInitializer()
{
    cleanup();
}

Resource DbInitializer::createResource(const char *name)
{
    Resource res;
    qint64 id = -1;
    res.setName(QLatin1String(name));
    const bool ret = res.insert(&id);
    Q_ASSERT(ret);
    Q_UNUSED(ret);
    mResource = res;
    return res;
}

Collection DbInitializer::createCollection(const char *name, const Collection &parent)
{
    Collection col;
    if (parent.isValid()) {
        col.setParent(parent);
    }
    col.setName(QLatin1String(name));
    col.setRemoteId(QLatin1String(name));
    col.setResource(mResource);
    const bool ret = col.insert();
    Q_ASSERT(ret);
    Q_UNUSED(ret);
    return col;
}

PimItem DbInitializer::createItem(const char *name, const Collection &parent)
{
    PimItem item;
    MimeType mimeType = MimeType::retrieveByName(QLatin1String("test"));
    if (!mimeType.isValid()) {
        MimeType mt;
        mt.setName(QLatin1String("test"));
        mt.insert();
        mimeType = mt;
    }
    item.setMimeType(mimeType);
    item.setCollection(parent);
    item.setRemoteId(QLatin1String(name));
    const bool ret = item.insert() ;
    Q_ASSERT(ret);
    Q_UNUSED(ret);
    return item;
}

QByteArray DbInitializer::toByteArray(bool enabled)
{
    if (enabled) {
        return "TRUE";
    }
    return "FALSE";
}

QByteArray DbInitializer::toByteArray(Akonadi::Tristate tristate)
{

    switch (tristate) {
    case Akonadi::Tristate::True:
        return "TRUE";
    case Akonadi::Tristate::False:
        return "FALSE";
    case Akonadi::Tristate::Undefined:
    default:
        break;
    }
    return "DEFAULT";
}

Akonadi::Protocol::FetchCollectionsResponse DbInitializer::listResponse(const Collection &col,
                                                                        bool ancestors,
                                                                        bool mimetypes,
                                                                        const QStringList &ancestorFetchScope)
{
    Akonadi::Protocol::FetchCollectionsResponse resp(col.id());
    resp.setParentId(col.parentId());
    resp.setName(col.name());
    if (mimetypes) {
        QStringList mts;
        for (const Akonadi::Server::MimeType &mt : col.mimeTypes()) {
            mts << mt.name();
        }
        resp.setMimeTypes(mts);
    }
    resp.setRemoteId(col.remoteId());
    resp.setRemoteRevision(col.remoteRevision());
    resp.setResource(col.resource().name());
    resp.setIsVirtual(col.isVirtual());
    Akonadi::Protocol::CachePolicy cp;
    cp.setInherit(true);
    cp.setLocalParts({ QLatin1String("ALL") });
    resp.setCachePolicy(cp);
    if (ancestors) {
        QVector<Akonadi::Protocol::Ancestor> ancs;
        Collection parent = col.parent();
        while (parent.isValid()) {
            Akonadi::Protocol::Ancestor anc;
            anc.setId(parent.id());
            anc.setRemoteId(parent.remoteId());
            anc.setName(parent.name());
            if (!ancestorFetchScope.isEmpty()) {
                anc.setRemoteId(parent.remoteId());
                Akonadi::Protocol::Attributes attrs;
                Q_FOREACH(const CollectionAttribute &attr, parent.attributes()) {
                    if (ancestorFetchScope.contains(QString::fromLatin1(attr.type()))) {
                        attrs.insert(attr.type(), attr.value());
                    }
                }
                anc.setAttributes(attrs);
            }
            parent = parent.parent();
            ancs.push_back(anc);
        }
        // Root
        ancs.push_back(Akonadi::Protocol::Ancestor(0));
        resp.setAncestors(ancs);
    }
    resp.setReferenced(col.referenced());
    resp.setEnabled(col.enabled());
    resp.setDisplayPref(col.displayPref());
    resp.setSyncPref(col.syncPref());
    resp.setIndexPref(col.indexPref());

    Akonadi::Protocol::Attributes attrs;
    Q_FOREACH(const CollectionAttribute &attr, col.attributes()) {
        attrs.insert(attr.type(), attr.value());
    }
    resp.setAttributes(attrs);
    return resp;
}

Collection DbInitializer::collection(const char *name)
{
    return Collection::retrieveByName(QLatin1String(name));
}

void DbInitializer::cleanup()
{
    Q_FOREACH (Collection col, mResource.collections()) {
        if (!col.isVirtual()) {
            col.remove();
        }
    }
    mResource.remove();

    if (DataStore::self()->database().isOpen()) {
        {
            QueryBuilder qb( Relation::tableName(), QueryBuilder::Delete );
            qb.exec();
        }
        {
            QueryBuilder qb(Tag::tableName(), QueryBuilder::Delete);
            qb.exec();
        }
        {
            QueryBuilder qb(TagType::tableName(), QueryBuilder::Delete);
            qb.exec();
        }
    }

    Q_FOREACH(PimItem item, PimItem::retrieveAll()) {
        item.remove();
    }
}