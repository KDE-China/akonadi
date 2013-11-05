/*
    Copyright (c) 2010 Volker Krause <vkrause@kde.org>

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

#include "config-akonadi.h"
#include "searchmanager.h"
#include <akdebug.h>
#include "agentsearchengine.h"
#include "nepomuksearchengine.h"
#include "entities.h"
#include "xesamsearchengine.h"
#include <storage/notificationcollector.h>

using namespace Akonadi;

SearchManager *SearchManager::m_instance = 0;

Q_DECLARE_METATYPE( Collection )

SearchManager::SearchManager( const QStringList &searchEngines, QObject *parent )
  : QObject( parent )
{
  Q_ASSERT( m_instance == 0 );
  m_instance = this;

  qRegisterMetaType<Collection>();

  m_engines.reserve( searchEngines.size() );
  Q_FOREACH ( const QString &engineName, searchEngines ) {
    if ( engineName == QLatin1String( "Nepomuk" ) ) {
#ifdef HAVE_SOPRANO
      m_engines.append( new NepomukSearchEngine );
#else
      akError() << "Akonadi has been built without Nepomuk support!";
#endif
    } else if ( engineName == QLatin1String( "Agent" ) ) {
      m_engines.append( new AgentSearchEngine );
    } else if ( engineName == QLatin1String( "Xesam" ) ) {
      m_engines.append( new XesamSearchEngine );
    } else {
      akError() << "Unknown search engine type: " << engineName;
    }
  }
}

SearchManager::~SearchManager()
{
  qDeleteAll( m_engines );
  m_instance = 0;
}

SearchManager *SearchManager::instance()
{
  return m_instance;
}

bool SearchManager::addSearch( const Collection &collection )
{
  if ( collection.queryString().size() >= 32768 ) {
    qWarning() << "The query is at least 32768 chars long, which is the maximum size supported by the akonadi db schema. The query is therefore most likely truncated and will not be executed.";
    return false;
  }
  if ( collection.queryString().isEmpty() || collection.queryLanguage().isEmpty() ) {
    return false;
  }

  // send to the main thread
  QMetaObject::invokeMethod( this, "addSearchInternal", Qt::QueuedConnection, Q_ARG( Collection, collection ) );
  return true;
}

void SearchManager::addSearchInternal( const Collection &collection )
{
  Q_FOREACH ( AbstractSearchEngine *engine, m_engines ) {
    engine->addSearch( collection );
  }
}

bool SearchManager::removeSearch( qint64 id )
{
  // send to the main thread
  QMetaObject::invokeMethod( this, "removeSearchInternal", Qt::QueuedConnection, Q_ARG( qint64, id ) );
  return true;
}

void SearchManager::removeSearchInternal( qint64 id )
{
  Q_FOREACH ( AbstractSearchEngine *engine, m_engines ) {
    engine->removeSearch( id );
  }
}

void SearchManager::updateSearch( const Akonadi::Collection &collection, NotificationCollector *collector )
{
  removeSearch( collection.id() );
  collector->itemsUnlinked( collection.pimItems(), collection );
  collection.clearPimItems();
  addSearch( collection );
}
