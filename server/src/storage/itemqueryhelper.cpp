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

#include "itemqueryhelper.h"

#include "akonadiconnection.h"
#include "entities.h"
#include "storage/querybuilder.h"
#include "libs/imapset_p.h"
#include "handler/scope.h"
#include "handler.h"
#include "storage/queryhelper.h"
#include "collectionqueryhelper.h"

using namespace Akonadi::Server;

void ItemQueryHelper::itemSetToQuery( const ImapSet &set, QueryBuilder &qb, const Collection &collection )
{
  QueryHelper::setToQuery( set, PimItem::idFullColumnName(), qb );
  if ( collection.isValid() ) {
    if ( collection.isVirtual() || collection.resource().isVirtual() ) {
      qb.addJoin( QueryBuilder::InnerJoin, CollectionPimItemRelation::tableName(),
                  CollectionPimItemRelation::rightFullColumnName(), PimItem::idFullColumnName() );
      qb.addValueCondition( CollectionPimItemRelation::leftFullColumnName(), Query::Equals, collection.id() );
    } else {
      qb.addValueCondition( PimItem::collectionIdColumn(), Query::Equals, collection.id() );
    }
  }
}

void ItemQueryHelper::itemSetToQuery( const ImapSet &set, bool isUid, AkonadiConnection *connection, QueryBuilder &qb )
{
  if ( !isUid && connection->selectedCollectionId() >= 0 ) {
    itemSetToQuery( set, qb, connection->selectedCollection() );
  } else {
    itemSetToQuery( set, qb );
  }
}

void ItemQueryHelper::remoteIdToQuery( const QStringList &rids, AkonadiConnection *connection, QueryBuilder &qb )
{
  if ( rids.size() == 1 ) {
    qb.addValueCondition( PimItem::remoteIdFullColumnName(), Query::Equals, rids.first() );
  } else {
    qb.addValueCondition( PimItem::remoteIdFullColumnName(), Query::In, rids );
  }

  if ( connection->resourceContext().isValid() ) {
    qb.addJoin( QueryBuilder::InnerJoin, Collection::tableName(),
                PimItem::collectionIdFullColumnName(), Collection::idFullColumnName() );
    qb.addValueCondition( Collection::resourceIdFullColumnName(), Query::Equals, connection->resourceContext().id() );
  } else if ( connection->selectedCollectionId() > 0 ) {
    qb.addValueCondition( PimItem::collectionIdFullColumnName(), Query::Equals, connection->selectedCollectionId() );
  }
}

void ItemQueryHelper::gidToQuery( const QStringList &gids, QueryBuilder &qb )
{
  if ( gids.size() == 1 ) {
    qb.addValueCondition( PimItem::gidFullColumnName(), Query::Equals, gids.first() );
  } else {
    qb.addValueCondition( PimItem::gidFullColumnName(), Query::In, gids );
  }

}

void ItemQueryHelper::scopeToQuery( const Scope &scope, AkonadiConnection *connection, QueryBuilder &qb )
{
  if ( scope.scope() == Scope::None || scope.scope() == Scope::Uid ) {
    itemSetToQuery( scope.uidSet(), scope.scope() == Scope::Uid, connection, qb );
    return;
  }

  if ( scope.scope() == Scope::Gid ) {
    ItemQueryHelper::gidToQuery( scope.gidSet(), qb );
    return;
  }

  if ( connection->selectedCollectionId() <= 0 && !connection->resourceContext().isValid() ) {
      throw HandlerException( "Operations based on remote identifiers require a resource or collection context" );
  }

  if ( scope.scope() == Scope::Rid ) {
    ItemQueryHelper::remoteIdToQuery( scope.ridSet(), connection, qb );
    return;
  } else if ( scope.scope() == Scope::HierarchicalRid ) {
    QStringList ridChain = scope.ridChain();
    const QString itemRid = ridChain.takeFirst();
    const Collection parentCol = CollectionQueryHelper::resolveHierarchicalRID( ridChain, connection->resourceContext().id() );
    const Collection::Id oldSelection = connection->selectedCollectionId();
    remoteIdToQuery( QStringList() << itemRid, connection, qb );
    connection->setSelectedCollection( oldSelection );
    return;
  }

  throw HandlerException( "Dude, WTF?!?" );
}
