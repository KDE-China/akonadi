/***************************************************************************
 *   Copyright (C) 2006 by Andreas Gungl <a.gungl@gmx.de>                  *
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

#include "datastore.h"

#include "dbconfig.h"
#include "dbinitializer.h"
#include "dbupdater.h"
#include "notificationmanager.h"
#include "tracer.h"
#include "transaction.h"
#include "selectquerybuilder.h"
#include "handlerhelper.h"
#include "countquerybuilder.h"
#include "xdgbasedirs_p.h"
#include "akdebug.h"
#include "parthelper.h"
#include "libs/protocol_p.h"
#include "handler.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QEventLoop>
#include <QtCore/QSettings>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QThread>
#include <QtCore/QThreadStorage>
#include <QtCore/QUuid>
#include <QtCore/QVariant>
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlDriver>
#include <QtSql/QSqlError>
#include <QtSql/QSqlField>
#include <QtSql/QSqlQuery>
#include "collectionqueryhelper.h"

using namespace Akonadi;

static QMutex sTransactionMutex;
#define TRANSACTION_MUTEX_LOCK if ( m_database.driverName().startsWith( QLatin1String( "QSQLITE" ) ) ) sTransactionMutex.lock()
#define TRANSACTION_MUTEX_UNLOCK if ( m_database.driverName().startsWith( QLatin1String( "QSQLITE" ) ) ) sTransactionMutex.unlock()

/***************************************************************************
 *   DataStore                                                           *
 ***************************************************************************/
DataStore::DataStore() :
  QObject(),
  m_dbOpened( false ),
  m_transactionLevel( 0 ),
  mNotificationCollector( new NotificationCollector( this ) )
{
  open();

  m_transactionLevel = 0;
  NotificationManager::self()->connectNotificationCollector( mNotificationCollector );
}

DataStore::~DataStore()
{
  close();
}

void DataStore::open()
{
  m_connectionName = QUuid::createUuid().toString() + QString::number( reinterpret_cast<qulonglong>( QThread::currentThread() ) );
  Q_ASSERT( !QSqlDatabase::contains( m_connectionName ) );

  m_database = QSqlDatabase::addDatabase( DbConfig::configuredDatabase()->driverName(), m_connectionName );
  DbConfig::configuredDatabase()->apply( m_database );

  if ( !m_database.isValid() ) {
    m_dbOpened = false;
    return;
  }
  m_dbOpened = m_database.open();

  if ( !m_dbOpened )
    debugLastDbError( "Cannot open database." );
  else
    akDebug() << "Database" << m_database.databaseName() << "opened using driver" << m_database.driverName();

  DbConfig::configuredDatabase()->initSession( m_database );
}

void Akonadi::DataStore::close()
{
  if ( !m_dbOpened )
    return;

  if ( inTransaction() ) {
    // By setting m_transactionLevel to '1' here, we skip all nested transactions
    // and rollback the outermost transaction.
    m_transactionLevel = 1;
    rollbackTransaction();
  }

  m_database.close();
  m_database = QSqlDatabase();
  QSqlDatabase::removeDatabase( m_connectionName );

  m_dbOpened = false;
}

bool Akonadi::DataStore::init()
{
  QString dbtemplate = QLatin1String(":akonadidb.xml");
#ifdef Q_OS_WINCE
  dbtemplate = QLatin1String(":akonadidb-mobile.xml");
#endif

  DbInitializer::Ptr initializer = DbInitializer::createInstance( m_database, dbtemplate );
  if (! initializer->run() ) {
    akError() << initializer->errorMsg();
    return false;
  }

  DbUpdater updater( m_database, QLatin1String( ":dbupdate.xml" ) );
  if ( !updater.run() )
    return false;

  // enable caching for some tables
  MimeType::enableCache( true );
  Flag::enableCache( true );
  Resource::enableCache( true );
  Collection::enableCache( true );

  return true;
}

QThreadStorage<DataStore*> instances;

DataStore * Akonadi::DataStore::self()
{
  if ( !instances.hasLocalData() )
    instances.setLocalData( new DataStore() );
  return instances.localData();
}


/* --- ItemFlags ----------------------------------------------------- */

bool DataStore::setItemFlags( const PimItem &item, const QVector<Flag> &flags )
{
  // first delete all old flags of this pim item
  if ( !item.clearFlags() )
    return false;

  // then add the new flags
  for ( int i = 0; i < flags.count(); ++i ) {
    if ( !item.addFlag( flags[i] ) )
      return false;
  }

  mNotificationCollector->itemChanged( item, QSet<QByteArray>() << "FLAGS" );
  return true;
}

bool DataStore::appendItemFlags( const PimItem &item, const QVector<Flag> &flags,
                                 bool& flagsChanged, bool checkIfExists,
                                 const Collection &col )
{
  flagsChanged = false;
  if ( !item.isValid() )
    return false;
  if ( flags.isEmpty() )
    return true;

  for ( int i = 0; i < flags.count(); ++i ) {
    if ( !checkIfExists || !item.relatesToFlag( flags[ i ] ) ) {
      flagsChanged = true;
      if ( !item.addFlag( flags[i] ) )
        return false;
    }
  }
  if ( flagsChanged )
    mNotificationCollector->itemChanged( item, QSet<QByteArray>() << "FLAGS", col );
  return true;
}

bool DataStore::removeItemFlags( const PimItem &item, const QVector<Flag> &flags )
{
  for ( int i = 0; i < flags.count(); ++i ) {
    if ( !item.removeFlag( flags[ i ] ) )
      return false;
  }

  mNotificationCollector->itemChanged( item, QSet<QByteArray>() << "FLAGS" );
  return true;
}

/* --- ItemParts ----------------------------------------------------- */

bool DataStore::removeItemParts( const PimItem &item, const QList<QByteArray> &parts )
{
  Part::List existingParts = item.parts();
  Q_FOREACH ( Part part, existingParts ) {
    if( parts.contains( part.name().toLatin1() ) ) {
      if ( !PartHelper::remove(&part) )
        return false;
    }
  }

  mNotificationCollector->itemChanged( item, parts.toSet() );
  return true;
}

bool DataStore::invalidateItemCache( const PimItem &item )
{
  // find all expired item parts
  SelectQueryBuilder<Part> qb;
  qb.addJoin( QueryBuilder::InnerJoin, PimItem::tableName(), PimItem::idFullColumnName(), Part::pimItemIdFullColumnName() );
  qb.addValueCondition( Part::pimItemIdFullColumnName(), Query::Equals, item.id() );
  qb.addValueCondition( Part::dataFullColumnName(), Query::IsNot, QVariant() );
  qb.addValueCondition( QString::fromLatin1( "substr( %1, 1, 4 )" ).arg( Part::nameFullColumnName() ), Query::Equals, QLatin1String( "PLD:" ) );
  qb.addValueCondition( PimItem::dirtyFullColumnName(), Query::Equals, false );

  if ( !qb.exec() )
    return false;

  const Part::List parts = qb.result();
  // clear data field
  Q_FOREACH ( Part part, parts ) {
    if ( !PartHelper::truncate( part ) )
      return false;
  }

  return true;
}

/* --- Collection ------------------------------------------------------ */
bool DataStore::appendCollection( Collection &collection )
{
  // no need to check for already existing collection with the same name,
  // a unique index on parent + name prevents that in the database
  if ( !collection.insert() )
    return false;

  mNotificationCollector->collectionAdded( collection );
  return true;
}

bool Akonadi::DataStore::cleanupCollection(Collection &collection)
{
  // delete the content
  const PimItem::List items = collection.items();
  const QByteArray resource = collection.resource().name().toLatin1();
  Q_FOREACH ( const PimItem &item, items ) {

    // generate the notification before actually removing the data
    mNotificationCollector->itemRemoved( item, collection, QString(), resource ); // TODO: make mimetype available as part as an item bulk query

    if ( !item.clearFlags() ) // TODO: move out of loop and use only a single query
      return false;
    if ( !PartHelper::remove( Part::pimItemIdColumn(), item.id() ) ) // TODO: reduce to single query
      return false;
    if ( !PimItem::remove( PimItem::idColumn(), item.id() ) ) // TODO: move into single query
      return false;

    if ( !Entity::clearRelation<CollectionPimItemRelation>( item.id(), Entity::Right ) ) // TODO: move into single query
      return false;
  }

  // delete collection mimetypes
  collection.clearMimeTypes();
  Collection::clearPimItems( collection.id() );

  // delete attributes
  Q_FOREACH ( CollectionAttribute attr, collection.attributes() )
    if ( !attr.remove() )
      return false;

  // delete the collection itself
  mNotificationCollector->collectionRemoved( collection );
  return collection.remove();
}

static bool recursiveSetResourceId( const Collection & collection, qint64 resourceId )
{
  Transaction transaction( DataStore::self() );

  QueryBuilder qb( Collection::tableName(), QueryBuilder::Update );
  qb.addValueCondition( Collection::parentIdColumn(), Query::Equals, collection.id() );
  qb.setColumnValue( Collection::resourceIdColumn(), resourceId );
  if ( !qb.exec() )
    return false;

  // this is a cross-resource move, so also reset any resource-specific data (RID, RREV, etc)
  // as well as mark the items dirty to prevent cache purging before they have been written back
  qb = QueryBuilder( PimItem::tableName(), QueryBuilder::Update );
  qb.addValueCondition( PimItem::collectionIdColumn(), Query::Equals, collection.id() );
  qb.setColumnValue( PimItem::remoteIdColumn(), QVariant() );
  qb.setColumnValue( PimItem::remoteRevisionColumn(), QVariant() );
  const QDateTime now = QDateTime::currentDateTime();
  qb.setColumnValue( PimItem::datetimeColumn(), now );
  qb.setColumnValue( PimItem::atimeColumn(), now );
  qb.setColumnValue( PimItem::dirtyColumn(), true );
  if ( !qb.exec() )
    return false;

  transaction.commit();

  Q_FOREACH ( const Collection &col, collection.children() ) {
    if ( !recursiveSetResourceId( col, resourceId ) )
      return false;
  }
  return true;
}

bool Akonadi::DataStore::moveCollection( Collection & collection, const Collection &newParent )
{
  if ( collection.parentId() == newParent.id() )
    return true;

  if ( !m_dbOpened || !newParent.isValid() )
    return false;

  const QByteArray oldResource = collection.resource().name().toLatin1();

  int resourceId = collection.resourceId();
  const Collection source = collection.parent();
  if ( newParent.id() > 0 ) // not root
    resourceId = newParent.resourceId();
  if ( !CollectionQueryHelper::canBeMovedTo ( collection, newParent ) )
    return false;

  collection.setParentId( newParent.id() );
  if ( collection.resourceId() != resourceId ) {
    collection.setResourceId( resourceId );
    if ( !recursiveSetResourceId( collection, resourceId ) )
      return false;
  }

  if ( !collection.update() )
    return false;

  mNotificationCollector->collectionMoved( collection, source, oldResource, newParent.resource().name().toLatin1() );
  return true;
}


bool DataStore::appendMimeTypeForCollection( qint64 collectionId, const QStringList & mimeTypes )
{
  if ( mimeTypes.isEmpty() )
    return true;
  SelectQueryBuilder<MimeType> qb;
  qb.addValueCondition( MimeType::nameColumn(), Query::In, mimeTypes );
  if ( !qb.exec() )
    return false;
  QStringList missingMimeTypes = mimeTypes;

  Q_FOREACH ( const MimeType &mt, qb.result() ) {
    // unique index on n:m relation prevents duplicates, ie. this will fail
    // if this mimetype is already set
    if ( !Collection::addMimeType( collectionId, mt.id() ) )
      return false;
    missingMimeTypes.removeAll( mt.name() );
  }

  // the MIME type doesn't exist, so we have to add it to the db
  Q_FOREACH ( const QString &mtName, missingMimeTypes ) {
    qint64 mimeTypeId;
    if ( !appendMimeType( mtName, &mimeTypeId ) )
      return false;
    if ( !Collection::addMimeType( collectionId, mimeTypeId ) )
      return false;
  }

  return true;
}


void DataStore::activeCachePolicy(Collection & col)
{
  if ( !col.cachePolicyInherit() )
    return;

  Collection parent = col;
  while ( parent.parentId() != 0 ) {
    parent = parent.parent();
    if ( !parent.cachePolicyInherit() ) {
      col.setCachePolicyCheckInterval( parent.cachePolicyCheckInterval() );
      col.setCachePolicyCacheTimeout( parent.cachePolicyCacheTimeout() );
      col.setCachePolicySyncOnDemand( parent.cachePolicySyncOnDemand() );
      col.setCachePolicyLocalParts( parent.cachePolicyLocalParts() );
      return;
    }
  }

  // ### system default
  col.setCachePolicyCheckInterval( -1 );
  col.setCachePolicyCacheTimeout( -1 );
  col.setCachePolicySyncOnDemand( false );
  col.setCachePolicyLocalParts( QLatin1String("ALL") );
}

/* --- MimeType ------------------------------------------------------ */
bool DataStore::appendMimeType( const QString & mimetype, qint64 *insertId )
{
  if ( MimeType::exists( mimetype ) ) {
    akDebug() << "Cannot insert mimetype " << mimetype
             << " because it already exists.";
    return false;
  }

  MimeType mt( mimetype );
  return mt.insert( insertId );
}



/* --- PimItem ------------------------------------------------------- */
bool DataStore::appendPimItem( QVector<Part> & parts,
                               const MimeType & mimetype,
                               const Collection & collection,
                               const QDateTime & dateTime,
                               const QString & remote_id,
                               const QString & remoteRevision,
                               PimItem &pimItem )
{
  pimItem.setMimeTypeId( mimetype.id() );
  pimItem.setCollectionId( collection.id() );
  if ( dateTime.isValid() )
    pimItem.setDatetime( dateTime );
  if ( remote_id.isEmpty() ) {
    // from application
    pimItem.setDirty( true );
  } else {
    // from resource
    pimItem.setRemoteId( remote_id );
    pimItem.setDirty( false );
  }
  pimItem.setRemoteRevision( remoteRevision );
  pimItem.setAtime( QDateTime::currentDateTime() );

  if ( !pimItem.insert() )
    return false;

  // insert every part
  if ( !parts.isEmpty() ) {
    //don't use foreach, the caller depends on knowing the part has changed, see the Append handler
    for(QVector<Part>::iterator it = parts.begin(); it != parts.end(); ++it ) {

      (*it).setPimItemId( pimItem.id() );
      if ( (*it).datasize() < (*it).data().size() )
        (*it).setDatasize( (*it).data().size() );

//       akDebug() << "Insert from DataStore::appendPimItem";
      if( !PartHelper::insert(&(*it)) )
        return false;
    }
  }

//   akDebug() << "appendPimItem: " << pimItem;

  mNotificationCollector->itemAdded( pimItem, collection, mimetype.name() );
  return true;
}

bool DataStore::unhidePimItem( PimItem &pimItem )
{
  if ( !m_dbOpened )
    return false;

  akDebug() << "DataStore::unhidePimItem(" << pimItem << ")";

  // FIXME: This is inefficient. Using a bit on the PimItemTable record would probably be some orders of magnitude faster...
  QList< QByteArray > parts;
  parts << "ATR:HIDDEN";

  return removeItemParts( pimItem, parts );
}

bool DataStore::unhideAllPimItems()
{
  if ( !m_dbOpened )
    return false;

  akDebug() << "DataStore::unhideAllPimItems()";

  return PartHelper::remove( Part::nameFullColumnName(), QLatin1String( "ATR:HIDDEN" ) );
}

bool DataStore::cleanupPimItem( const PimItem &item )
{
  if ( !item.isValid() )
    return false;

  // generate the notification before actually removing the data
  mNotificationCollector->itemRemoved( item );

  if ( !item.clearFlags() )
    return false;
  if ( !PartHelper::remove( Part::pimItemIdColumn(), item.id() ) )
    return false;
  if ( !PimItem::remove( PimItem::idColumn(), item.id() ) )
    return false;

  if ( !Entity::clearRelation<CollectionPimItemRelation>( item.id(), Entity::Right ) )
    return false;

  return true;
}

bool DataStore::addCollectionAttribute(const Collection & col, const QByteArray & key, const QByteArray & value)
{
  SelectQueryBuilder<CollectionAttribute> qb;
  qb.addValueCondition( CollectionAttribute::collectionIdColumn(), Query::Equals, col.id() );
  qb.addValueCondition( CollectionAttribute::typeColumn(), Query::Equals, key );
  if ( !qb.exec() )
    return false;

  if ( qb.result().count() > 0 ) {
    akDebug() << "Attribute" << key << "already exists for collection" << col.id();
    return false;
  }

  CollectionAttribute attr;
  attr.setCollectionId( col.id() );
  attr.setType( key );
  attr.setValue( value );

  if ( !attr.insert() )
    return false;

  mNotificationCollector->collectionChanged( col, QList<QByteArray>() << key );
  return true;
}

bool Akonadi::DataStore::removeCollectionAttribute(const Collection & col, const QByteArray & key)
{
  SelectQueryBuilder<CollectionAttribute> qb;
  qb.addValueCondition( CollectionAttribute::collectionIdColumn(), Query::Equals, col.id() );
  qb.addValueCondition( CollectionAttribute::typeColumn(), Query::Equals, key );
  if ( !qb.exec() )
    throw HandlerException( "Unable to query for collection attribute" );

  const QVector<CollectionAttribute> result = qb.result();
  Q_FOREACH ( CollectionAttribute attr, result ) {
    if ( !attr.remove() )
      throw HandlerException( "Unable to remove collection attribute" );
  }

  if (!result.isEmpty()) {
    mNotificationCollector->collectionChanged( col, QList<QByteArray>() << key );
    return true;
  }
  return false;
}


void DataStore::debugLastDbError( const char* actionDescription ) const
{
  akError() << "Database error:" << actionDescription;
  akError() << "  Last driver error:" << m_database.lastError().driverText();
  akError() << "  Last database error:" << m_database.lastError().databaseText();

  Tracer::self()->error( "DataStore (Database Error)",
                         QString::fromLatin1( "%1\nDriver said: %2\nDatabase said:%3" )
                            .arg( QString::fromLatin1( actionDescription ) )
                            .arg( m_database.lastError().driverText() )
                            .arg( m_database.lastError().databaseText() )
                       );
}

void DataStore::debugLastQueryError( const QSqlQuery &query, const char* actionDescription ) const
{
  akError() << "Query error:" << actionDescription;
  akError() << "  Last error message:" << query.lastError().text();
  akError() << "  Last driver error:" << m_database.lastError().driverText();
  akError() << "  Last database error:" << m_database.lastError().databaseText();

  Tracer::self()->error( "DataStore (Database Query Error)",
                         QString::fromLatin1( "%1: %2" )
                            .arg( QString::fromLatin1( actionDescription ) )
                            .arg( query.lastError().text() )
                       );
}

// static
QString DataStore::dateTimeFromQDateTime( const QDateTime & dateTime )
{
    QDateTime utcDateTime = dateTime;
    if ( utcDateTime.timeSpec() != Qt::UTC )
        utcDateTime.toUTC();
    return utcDateTime.toString( QLatin1String("yyyy-MM-dd hh:mm:ss") );
}


// static
QDateTime DataStore::dateTimeToQDateTime( const QByteArray & dateTime )
{
    return QDateTime::fromString( QString::fromLatin1(dateTime), QLatin1String("yyyy-MM-dd hh:mm:ss") );
}

bool Akonadi::DataStore::beginTransaction()
{
  if ( !m_dbOpened )
    return false;

  if ( m_transactionLevel == 0 ) {
    TRANSACTION_MUTEX_LOCK; 
    QSqlDriver *driver = m_database.driver();
    if ( !driver->beginTransaction() ) {
      debugLastDbError( "DataStore::beginTransaction" );
      TRANSACTION_MUTEX_UNLOCK;
      return false;
    }
  }

  m_transactionLevel++;

  return true;
}

bool Akonadi::DataStore::rollbackTransaction()
{
  if ( !m_dbOpened )
    return false;

  if ( m_transactionLevel == 0 ) {
    qWarning() << "DataStore::rollbackTransaction(): No transaction in progress!";
    return false;
  }

  m_transactionLevel--;

  if ( m_transactionLevel == 0 ) {
    QSqlDriver *driver = m_database.driver();
    Q_EMIT transactionRolledBack();
    if ( !driver->rollbackTransaction() ) {
      TRANSACTION_MUTEX_UNLOCK;
      debugLastDbError( "DataStore::rollbackTransaction" );
      return false;
    }

    TRANSACTION_MUTEX_UNLOCK;
  }

  return true;
}

bool Akonadi::DataStore::commitTransaction()
{
  if ( !m_dbOpened )
    return false;

  if ( m_transactionLevel == 0 ) {
    qWarning() << "DataStore::commitTransaction(): No transaction in progress!";
    return false;
  }

  if ( m_transactionLevel == 1 ) {
    QSqlDriver *driver = m_database.driver();
    if ( !driver->commitTransaction() ) {
      debugLastDbError( "DataStore::commitTransaction" );
      rollbackTransaction();
      return false;
    } else {
      TRANSACTION_MUTEX_UNLOCK;
      Q_EMIT transactionCommitted();
    }
  }

  m_transactionLevel--;
  return true;
}

bool Akonadi::DataStore::inTransaction() const
{
  return m_transactionLevel > 0;
}

#include "datastore.moc"
