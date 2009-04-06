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

#include "aklist.h"

#include <QtCore/QDebug>

#include "storage/datastore.h"
#include "storage/entity.h"

#include "akonadiconnection.h"
#include "response.h"
#include "handlerhelper.h"
#include "imapstreamparser.h"

using namespace Akonadi;

AkList::AkList( bool onlySubscribed ):
    Handler(),
    mOnlySubscribed( onlySubscribed )
{}

AkList::~AkList() {}

bool AkList::listCollection(const Collection & root, int depth )
{
  // recursive listing of child collections
  bool childrenFound = false;
  if ( depth > 0 ) {
    Collection::List children = root.children();
    foreach ( const Collection &col, children ) {
      if ( listCollection( col, depth - 1 ) )
        childrenFound = true;
    }
  }

  // filter if this node isn't needed by it's children
  bool hidden = (mResource.isValid() && root.resourceId() != mResource.id()) || (mOnlySubscribed && !root.subscribed());
  if ( !childrenFound && hidden )
    return false;

  // write out collection details
  Collection dummy = root;
  DataStore *db = connection()->storageBackend();
  db->activeCachePolicy( dummy );
  const QByteArray b = HandlerHelper::collectionToByteArray( dummy, hidden );

  Response response;
  response.setUntagged();
  response.setString( b );
  emit responseAvailable( response );

  return true;
}

bool AkList::parseStream()
{
  bool ok = false;
  const qint64 baseCollection = m_streamParser->readNumber( &ok );
  if ( !ok )
    return failureResponse( "Invalid base collection" );

  int depth;
  const QByteArray tmp = m_streamParser->readString();
  if ( tmp.isEmpty() )
    return failureResponse( "Specify listing depth" );
  if ( tmp == "INF" )
    depth = INT_MAX;
  else
    depth = tmp.toInt();

  QList<QByteArray> filter = m_streamParser->readParenthesizedList();

  for ( int i = 0; i < filter.count() - 1; i += 2 ) {
    if ( filter.at( i ) == "RESOURCE" ) {
      mResource = Resource::retrieveByName( QString::fromLatin1( filter.at(i + 1) ) );
      if ( !mResource.isValid() )
        return failureResponse( "Unknown resource" );
    } else
      return failureResponse( "Invalid filter parameter" );
  }

  Collection::List collections;
  if ( baseCollection != 0 ) { // not root
    Collection col = Collection::retrieveById( baseCollection );
    if ( !col.isValid() )
      return failureResponse( "Collection " + QByteArray::number( baseCollection ) + " does not exist" );
    if ( depth == 0 )
      collections << col;
    else {
      collections << col.children();
      --depth;
    }
  } else {
    if ( depth != 0 ) {
      Collection::List list = Collection::retrieveFiltered( Collection::parentIdColumn(), 0 );
      collections << list;
    }
    --depth;
  }

  foreach ( const Collection &col, collections )
    listCollection( col, depth );

  Response response;
  response.setSuccess();
  response.setTag( tag() );
  response.setString( "List completed" );
  emit responseAvailable( response );
  deleteLater();
  return true;
}

#include "aklist.moc"
