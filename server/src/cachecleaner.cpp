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

#include "cachecleaner.h"
#include <akdebug.h>
#include "storage/parthelper.h"
#include "storage/datastore.h"
#include "storage/selectquerybuilder.h"

#include <libs/protocol_p.h>

#include <QDebug>
#include <QTimer>

using namespace Akonadi;

CacheCleaner::CacheCleaner( QObject *parent ) :
    QThread( parent )
{
  mTime = 60;
  mLoops = 0;
}

CacheCleaner::~CacheCleaner()
{
}

void CacheCleaner::run()
{
  DataStore::self();
  QTimer::singleShot( mTime * 1000, this, SLOT(cleanCache()) );
  exec();
  DataStore::self()->close();
}

void CacheCleaner::cleanCache()
{
  qint64 loopsWithExpiredItem = 0;
  // cycle over all collection
  const QVector<Collection> collections = Collection::retrieveAll();
  Q_FOREACH ( /*sic!*/ Collection collection, collections ) {
    // determine active cache policy
    DataStore::self()->activeCachePolicy( collection );

    // check if there is something to expire at all
    if ( collection.cachePolicyLocalParts() == QLatin1String( "ALL" ) || collection.cachePolicyCacheTimeout() < 0
       || !collection.subscribed() || !collection.resourceId() ) {
      continue;
    }
    const int expireTime = qMax( 5, collection.cachePolicyCacheTimeout() );

    // find all expired item parts
    SelectQueryBuilder<Part> qb;
    qb.addJoin( QueryBuilder::InnerJoin, PimItem::tableName(), Part::pimItemIdColumn(), PimItem::idFullColumnName() );
    qb.addValueCondition( PimItem::collectionIdFullColumnName(), Query::Equals, collection.id() );
    qb.addValueCondition( PimItem::atimeFullColumnName(), Query::Less, QDateTime::currentDateTime().addSecs( -60 * expireTime ) );
    qb.addValueCondition( Part::dataFullColumnName(), Query::IsNot, QVariant() );
    qb.addValueCondition( QString::fromLatin1( "substr( %1, 1, 4 )" ).arg( Part::nameFullColumnName() ), Query::Equals, QLatin1String( AKONADI_PARAM_PLD ) );
    qb.addValueCondition( PimItem::dirtyFullColumnName(), Query::Equals, false );
    QStringList localParts;
    Q_FOREACH ( const QString &partName, collection.cachePolicyLocalParts().split( QLatin1String( " " ) ) ) {
      if ( partName.startsWith( QLatin1String( AKONADI_PARAM_PLD ) ) ) {
        localParts.append( partName );
      } else {
        localParts.append( QLatin1String( AKONADI_PARAM_PLD ) + partName );
      }
    }
    if ( !collection.cachePolicyLocalParts().isEmpty() ) {
      qb.addValueCondition( Part::nameFullColumnName(), Query::NotIn, localParts );
    }
    if ( !qb.exec() ) {
      continue;
    }
    const Part::List parts = qb.result();
    if ( parts.isEmpty() ) {
      continue;
    }
    akDebug() << "found" << parts.count() << "item parts to expire in collection" << collection.name();

    // clear data field
    Q_FOREACH ( Part part, parts ) {
      if ( !PartHelper::truncate( part ) ) {
        akDebug() << "failed to update item part" << part.id();
      }
    }
    loopsWithExpiredItem++;
  }

  /* if we have item parts to expire in collection the mTime is
   * decreased of 60 and if there are lot of collection need to be clean
   * mTime is 60 otherwise we increment mTime in 60
   */

  if ( mLoops < loopsWithExpiredItem) {
    if ( (mTime > 60) && (loopsWithExpiredItem - mLoops) < 50 ) {
      mTime -= 60;
    } else {
      mTime = 60;
    }
  } else {
    if ( mTime < 600 ) {
      mTime += 60;
    }
  }

  // measured arithmetic between mLoops and loopsWithExpiredItem
  mLoops = (loopsWithExpiredItem + mLoops) >> 2;

  QTimer::singleShot( mTime * 1000, this, SLOT(cleanCache()) );
}
