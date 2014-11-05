/*
    Copyright (c) 2014 Daniel Vrátil <dvratil@redhat.com>

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

#include "tagremove.h"
#include "storage/querybuilder.h"
#include "storage/selectquerybuilder.h"
#include "storage/queryhelper.h"
#include "storage/datastore.h"

using namespace Akonadi;
using namespace Akonadi::Server;

TagRemove::TagRemove( Scope::SelectionScope scope )
  : Handler()
  , mScope( scope )
{
}

TagRemove::~TagRemove()
{
}

bool TagRemove::parseStream()
{
  if ( mScope.scope() != Scope::Uid ) {
    throw HandlerException( "Only UID-based TAGREMOVE is supported" );
  }

  mScope.parseScope( m_streamParser );

  SelectQueryBuilder<Tag> tagQuery;
  QueryHelper::setToQuery( mScope.uidSet(), Tag::idFullColumnName(), tagQuery );
  if ( !tagQuery.exec() ) {
    throw HandlerException( "Failed to obtain tags" );
  }
  const Tag::List tags = tagQuery.result();


  if (!DataStore::self()->removeTags(tags)) {
      throw HandlerException( "Failed to remove tags" );
  }

  return successResponse( "TAGREMOVE complete" );
}

