/*
    Copyright (c) 2009 Constantin Berzan <exit3219@gmail.com>

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

#include "specialcollectionshelperjobs_p.h"

#include "specialcollectionattribute_p.h"
#include "specialcollections.h"

#include <akonadi/agentinstance.h>
#include <akonadi/agentinstancecreatejob.h>
#include <akonadi/agentmanager.h>
#include <akonadi/collectionfetchjob.h>
#include <akonadi/collectionfetchscope.h>
#include <akonadi/collectionmodifyjob.h>
#include <akonadi/entitydisplayattribute.h>
#include <akonadi/resourcesynchronizationjob.h>

#include <KDebug>
#include <KLocalizedString>
#include <KStandardDirs>
#include <kcoreconfigskeleton.h>

#include <QtDBus/QDBusConnectionInterface>
#include <QtDBus/QDBusInterface>
#include <QtCore/QMetaMethod>
#include <QtCore/QTime>
#include <QtCore/QTimer>

#define DBUS_SERVICE_NAME QLatin1String( "org.kde.pim.SpecialCollections" )
#define LOCK_WAIT_TIMEOUT_SECONDS 3

using namespace Akonadi;

// convenient methods to get/set the default resource id
static void setDefaultResourceId( KCoreConfigSkeleton *settings, const QString &value )
{
  KConfigSkeletonItem *item = settings->findItem( QLatin1String( "DefaultResourceId" ) );
  Q_ASSERT( item );
  item->setProperty( value );
}

static QString defaultResourceId( KCoreConfigSkeleton *settings )
{
  const KConfigSkeletonItem *item = settings->findItem( QLatin1String( "DefaultResourceId" ) );
  Q_ASSERT( item );
  return item->property().toString();
}

static QVariant::Type argumentType( const QMetaObject *mo, const QString &method )
{
  QMetaMethod m;
  for ( int i = 0; i < mo->methodCount(); ++i ) {
    const QString signature = QString::fromLatin1( mo->method( i ).signature() );
    if ( signature.startsWith( method ) )
      m = mo->method( i );
  }

  if ( !m.signature() )
    return QVariant::Invalid;

  const QList<QByteArray> argTypes = m.parameterTypes();
  if ( argTypes.count() != 1 )
    return QVariant::Invalid;

  return QVariant::nameToType( argTypes.first() );
}

// ===================== ResourceScanJob ============================

/**
  @internal
*/
class Akonadi::ResourceScanJob::Private
{
  public:
    Private( KCoreConfigSkeleton *settings, ResourceScanJob *qq );

    void fetchResult( KJob *job ); // slot

    ResourceScanJob *const q;

    // Input:
    QString mResourceId;
    KCoreConfigSkeleton *mSettings;

    // Output:
    Collection mRootCollection;
    Collection::List mSpecialCollections;
};

ResourceScanJob::Private::Private( KCoreConfigSkeleton *settings, ResourceScanJob *qq )
  : q( qq ), mSettings( settings )
{
}

void ResourceScanJob::Private::fetchResult( KJob *job )
{
  if ( job->error() ) {
    return;
  }

  CollectionFetchJob *fetchJob = qobject_cast<CollectionFetchJob *>( job );
  Q_ASSERT( fetchJob );

  Q_ASSERT( !mRootCollection.isValid() );
  Q_ASSERT( mSpecialCollections.isEmpty() );
  foreach ( const Collection &collection, fetchJob->collections() ) {
    if ( collection.parentCollection() == Collection::root() ) {
      if ( mRootCollection.isValid() )
        kWarning() << "Resource has more than one root collection. I don't know what to do.";
      else
        mRootCollection = collection;
    }

    if ( collection.hasAttribute<SpecialCollectionAttribute>() )
      mSpecialCollections.append( collection );
  }

  kDebug() << "Fetched root collection" << mRootCollection.id()
           << "and" << mSpecialCollections.count() << "local folders"
           << "(total" << fetchJob->collections().count() << "collections).";

  if ( !mRootCollection.isValid() ) {
    q->setError( Unknown );
    q->setErrorText( i18n( "Could not fetch root collection of resource %1.", mResourceId ) );
    q->commit();
    return;
  }

  // We are done!
  q->commit();
}



ResourceScanJob::ResourceScanJob( const QString &resourceId, KCoreConfigSkeleton *settings, QObject *parent )
  : TransactionSequence( parent ),
    d( new Private( settings, this ) )
{
  setResourceId( resourceId );
}

ResourceScanJob::~ResourceScanJob()
{
  delete d;
}

QString ResourceScanJob::resourceId() const
{
  return d->mResourceId;
}

void ResourceScanJob::setResourceId( const QString &resourceId )
{
  d->mResourceId = resourceId;
}

Collection ResourceScanJob::rootResourceCollection() const
{
  return d->mRootCollection;
}

Collection::List ResourceScanJob::specialCollections() const
{
  return d->mSpecialCollections;
}

void ResourceScanJob::doStart()
{
  if ( d->mResourceId.isEmpty() ) {
    kError() << "No resource ID given.";
    setError( Job::Unknown );
    setErrorText( i18n( "No resource ID given." ) );
    emitResult();
    TransactionSequence::doStart(); // HACK: probable misuse of TransactionSequence API.
                                    // Calling commit() here hangs :-/
    return;
  }

  CollectionFetchJob *fetchJob = new CollectionFetchJob( Collection::root(),
                                                         CollectionFetchJob::Recursive, this );
  fetchJob->fetchScope().setResource( d->mResourceId );
  connect( fetchJob, SIGNAL( result( KJob* ) ), this, SLOT( fetchResult( KJob* ) ) );
}


// ===================== DefaultResourceJob ============================

/**
  @internal
*/
class Akonadi::DefaultResourceJobPrivate
{
  public:
    DefaultResourceJobPrivate( KCoreConfigSkeleton *settings, DefaultResourceJob *qq );

    void tryFetchResource();
    void resourceCreateResult( KJob *job ); // slot
    void resourceSyncResult( KJob *job ); // slot
    void collectionFetchResult( KJob *job ); // slot
    void collectionModifyResult( KJob *job ); // slot

    DefaultResourceJob *const q;
    KCoreConfigSkeleton *mSettings;
    bool mResourceWasPreexisting;
    int mPendingModifyJobs;
    QString mDefaultResourceType;
    QVariantMap mDefaultResourceOptions;
    QList<QByteArray> mKnownTypes;
    QMap<QByteArray, QString> mNameForTypeMap;
    QMap<QByteArray, QString> mIconForTypeMap;
};

DefaultResourceJobPrivate::DefaultResourceJobPrivate( KCoreConfigSkeleton *settings, DefaultResourceJob *qq )
  : q( qq ),
    mSettings( settings ),
    mResourceWasPreexisting( true /* for safety, so as not to accidentally delete data */ ),
    mPendingModifyJobs( 0 )
{
}

void DefaultResourceJobPrivate::tryFetchResource()
{
  // Get the resourceId from config. Another instance might have changed it in the meantime.
  mSettings->readConfig();

  const QString resourceId = defaultResourceId( mSettings );

  kDebug() << "Read defaultResourceId" << resourceId << "from config.";

  const AgentInstance resource = AgentManager::self()->instance( resourceId );
  if ( resource.isValid() ) {
    // The resource exists; scan it.
    mResourceWasPreexisting = true;
    kDebug() << "Found resource" << resourceId;
    q->setResourceId( resourceId );
    q->ResourceScanJob::doStart();
  } else {
    // Create the resource.
    mResourceWasPreexisting = false;
    kDebug() << "Creating maildir resource.";
    const AgentType type = AgentManager::self()->type( mDefaultResourceType );
    AgentInstanceCreateJob *job = new AgentInstanceCreateJob( type, q );
    QObject::connect( job, SIGNAL( result( KJob* ) ), q, SLOT( resourceCreateResult( KJob* ) ) );
    job->start(); // non-Akonadi::Job
  }
}

void DefaultResourceJobPrivate::resourceCreateResult( KJob *job )
{
  if ( job->error() ) {
    //fail( i18n( "Failed to create the default resource (%1).", job->errorString() ) );
    q->setError( job->error() );
    q->setErrorText( job->errorText() );
    q->emitResult();
    return;
  }

  AgentInstance agent;

  // Get the resource instance.
  {
    AgentInstanceCreateJob *createJob = qobject_cast<AgentInstanceCreateJob*>( job );
    Q_ASSERT( createJob );
    agent = createJob->instance();
    setDefaultResourceId( mSettings, agent.identifier() );
    kDebug() << "Created maildir resource with id" << defaultResourceId( mSettings );
  }

  // Configure the resource.
  {
    agent.setName( mDefaultResourceOptions.value( QLatin1String( "Name" ) ).toString() );

    QDBusInterface conf( QString::fromLatin1( "org.freedesktop.Akonadi.Resource." ) + defaultResourceId( mSettings ),
                         QString::fromLatin1( "/Settings" ), QString() );

    QMapIterator<QString, QVariant> it( mDefaultResourceOptions );
    while ( it.hasNext() ) {
      it.next();

      if ( it.key() == QLatin1String( "Name" ) )
        continue;

      const QString methodName = QString::fromLatin1( "set%1" ).arg( it.key() );
      const QVariant::Type argType = argumentType( conf.metaObject(), methodName );
      if ( argType == QVariant::Invalid ) {
        q->setError( Job::Unknown );
        q->setErrorText( i18n( "Failed to configure default resource via D-Bus." ) );
        q->commit();
        return;
      }

      QDBusReply<void> reply = conf.call( methodName, it.value() );
      if ( !reply.isValid() ) {
        q->setError( Job::Unknown );
        q->setErrorText( i18n( "Failed to configure default resource via D-Bus." ) );
        q->commit();
        return;
      }
    }

    agent.reconfigure();
  }

  // Sync the resource.
  {
    ResourceSynchronizationJob *syncJob = new ResourceSynchronizationJob( agent, q );
    QObject::connect( syncJob, SIGNAL( result( KJob* ) ), q, SLOT( resourceSyncResult( KJob* ) ) );
    syncJob->start(); // non-Akonadi
  }
}

void DefaultResourceJobPrivate::resourceSyncResult( KJob *job )
{
  if ( job->error() ) {
    //fail( i18n( "ResourceSynchronizationJob failed (%1).", job->errorString() ) );
    return;
  }

  // Fetch the collections of the resource.
  kDebug() << "Fetching maildir collections.";
  CollectionFetchJob *fetchJob = new CollectionFetchJob( Collection::root(), CollectionFetchJob::Recursive, q );
  fetchJob->fetchScope().setResource( defaultResourceId( mSettings ) );
  QObject::connect( fetchJob, SIGNAL( result( KJob* ) ), q, SLOT( collectionFetchResult( KJob* ) ) );
}

void DefaultResourceJobPrivate::collectionFetchResult( KJob *job )
{
  if ( job->error() ) {
    //fail( i18n( "Failed to fetch the root maildir collection (%1).", job->errorString() ) );
    return;
  }

  CollectionFetchJob *fetchJob = qobject_cast<CollectionFetchJob *>( job );
  Q_ASSERT( fetchJob );

  const Collection::List collections = fetchJob->collections();
  kDebug() << "Fetched" << collections.count() << "collections.";

  // Find the root maildir collection.
  Collection::List toRecover;
  Collection resourceCollection;
  foreach ( const Collection &collection, collections ) {
    if ( collection.parentCollection() == Collection::root() ) {
      resourceCollection = collection;
      toRecover.append( collection );
      break;
    }
  }

  if ( !resourceCollection.isValid() ) {
    q->setError( Job::Unknown );
    q->setErrorText( i18n( "Failed to fetch the resource collection." ) );
    q->commit();
    return;
  }

  // Find all children of the resource collection.
  foreach ( const Collection &collection, collections ) {
    if ( collection.parentCollection() == resourceCollection ) {
      toRecover.append( collection );
    }
  }

  QHash<QString, QByteArray> typeForName;
  foreach ( const QByteArray &type, mKnownTypes ) {
    const QString displayName = mNameForTypeMap.value( type );
    typeForName[ displayName ] = type;
  }

  // These collections have been created by the maildir resource, when it
  // found the folders on disk. So give them the necessary attributes now.
  Q_ASSERT( mPendingModifyJobs == 0 );
  foreach ( Collection collection, toRecover ) {          // krazy:exclude=foreach

    // Find the type for the collection.
    QByteArray type;
    if ( typeForName.contains( collection.name() ) )
      type = typeForName[ collection.name() ];

    if ( !type.isEmpty() ) {
      kDebug() << "Recovering collection" << collection.name();
      setCollectionAttributes( collection, type, mNameForTypeMap, mIconForTypeMap );

      CollectionModifyJob *modifyJob = new CollectionModifyJob( collection, q );
      QObject::connect( modifyJob, SIGNAL( result( KJob* ) ), q, SLOT( collectionModifyResult( KJob* ) ) );
      mPendingModifyJobs++;
    } else {
      kDebug() << "Unknown collection name" << collection.name() << "-- not recovering.";
    }
  }
}

void DefaultResourceJobPrivate::collectionModifyResult( KJob *job )
{
  if ( job->error() ) {
    //fail( i18n( "Failed to modify the root maildir collection (%1).", job->errorString() ) );
    return;
  }

  Q_ASSERT( mPendingModifyJobs > 0 );
  mPendingModifyJobs--;
  kDebug() << "pendingModifyJobs now" << mPendingModifyJobs;
  if ( mPendingModifyJobs == 0 ) {
    // Write the updated config.
    kDebug() << "Writing defaultResourceId" << defaultResourceId( mSettings ) << "to config.";
    mSettings->writeConfig();

    // Scan the resource.
    q->setResourceId( defaultResourceId( mSettings ) );
    q->ResourceScanJob::doStart();
  }
}



DefaultResourceJob::DefaultResourceJob( KCoreConfigSkeleton *settings, QObject *parent )
  : ResourceScanJob( QString(), settings, parent ),
    d( new DefaultResourceJobPrivate( settings, this ) )
{
}

DefaultResourceJob::~DefaultResourceJob()
{
  delete d;
}

void DefaultResourceJob::setDefaultResourceType( const QString &type )
{
  d->mDefaultResourceType = type;
}

void DefaultResourceJob::setDefaultResourceOptions( const QVariantMap &options )
{
  d->mDefaultResourceOptions = options;
}

void DefaultResourceJob::setTypes( const QList<QByteArray> &types )
{
  d->mKnownTypes = types;
}

void DefaultResourceJob::setNameForTypeMap( const QMap<QByteArray, QString> &map )
{
  d->mNameForTypeMap = map;
}

void DefaultResourceJob::setIconForTypeMap( const QMap<QByteArray, QString> &map )
{
  d->mIconForTypeMap = map;
}

void DefaultResourceJob::doStart()
{
  d->tryFetchResource();
}

void DefaultResourceJob::slotResult( KJob *job )
{
  if ( job->error() ) {
    // Do some cleanup.
    if ( !d->mResourceWasPreexisting ) {
      // We only removed the resource instance if we have created it.
      // Otherwise we might lose the user's data.
      const AgentInstance resource = AgentManager::self()->instance( defaultResourceId( d->mSettings ) );
      kDebug() << "Removing resource" << resource.identifier();
      AgentManager::self()->removeInstance( resource );
    }
  }

  TransactionSequence::slotResult( job );
}

// ===================== GetLockJob ============================

class Akonadi::GetLockJob::Private
{
  public:
    Private( GetLockJob *qq );

    void doStart(); // slot
    void serviceOwnerChanged( const QString &name, const QString &oldOwner,
                              const QString &newOwner ); // slot
    void timeout(); // slot

    GetLockJob *const q;
    QTimer *mSafetyTimer;
};

GetLockJob::Private::Private( GetLockJob *qq )
  : q( qq ),
    mSafetyTimer( 0 )
{
}

void GetLockJob::Private::doStart()
{
  // Just doing registerService() and checking its return value is not sufficient,
  // since we may *already* own the name, and then registerService() returns true.

  QDBusConnection bus = QDBusConnection::sessionBus();
  const bool alreadyLocked = bus.interface()->isServiceRegistered( DBUS_SERVICE_NAME );
  const bool gotIt = bus.registerService( DBUS_SERVICE_NAME );

  if ( gotIt && !alreadyLocked ) {
    //kDebug() << "Got lock immediately.";
    q->emitResult();
  } else {
    //kDebug() << "Waiting for lock.";
    connect( QDBusConnection::sessionBus().interface(), SIGNAL( serviceOwnerChanged( QString, QString, QString ) ),
             q, SLOT( serviceOwnerChanged( QString, QString, QString ) ) );

    mSafetyTimer = new QTimer( q );
    mSafetyTimer->setSingleShot( true );
    mSafetyTimer->setInterval( LOCK_WAIT_TIMEOUT_SECONDS * 1000 );
    mSafetyTimer->start();
    connect( mSafetyTimer, SIGNAL( timeout() ), q, SLOT( timeout() ) );
  }
}

void GetLockJob::Private::serviceOwnerChanged( const QString &name, const QString &oldOwner, const QString &newOwner )
{
  Q_UNUSED( oldOwner );

  if ( name == DBUS_SERVICE_NAME && newOwner.isEmpty() ) {
    const bool gotIt = QDBusConnection::sessionBus().registerService( DBUS_SERVICE_NAME );
    if ( gotIt ) {
      mSafetyTimer->stop();
      q->emitResult();
    }
  }
}

void GetLockJob::Private::timeout()
{
  kWarning() << "Timeout trying to get lock.";
  q->setError( Job::Unknown );
  q->setErrorText( i18n( "Timeout trying to get lock." ) );
  q->emitResult();
}


GetLockJob::GetLockJob( QObject *parent )
  : KJob( parent ),
    d( new Private( this ) )
{
}

GetLockJob::~GetLockJob()
{
  delete d;
}

void GetLockJob::start()
{
  QTimer::singleShot( 0, this, SLOT( doStart() ) );
}

void Akonadi::setCollectionAttributes( Akonadi::Collection &collection, const QByteArray &type,
                                       const QMap<QByteArray, QString> &nameForType,
                                       const QMap<QByteArray, QString> &iconForType )
{
  {
    EntityDisplayAttribute *attr = new EntityDisplayAttribute;
    attr->setIconName( iconForType.value( type ) );
    attr->setDisplayName( nameForType.value( type ) );
    collection.addAttribute( attr );
  }

  {
    SpecialCollectionAttribute *attr = new SpecialCollectionAttribute;
    attr->setCollectionType( type );
    collection.addAttribute( attr );
  }
}

bool Akonadi::releaseLock()
{
  return QDBusConnection::sessionBus().unregisterService( DBUS_SERVICE_NAME );
}

#include "specialcollectionshelperjobs_p.moc"
