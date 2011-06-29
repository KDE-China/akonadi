/***************************************************************************
 *   Copyright (C) 2006 by Tobias Koenig <tokoe@kde.org>                   *
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

#include "store.h"

#include "akonadi.h"
#include "akonadiconnection.h"
#include "handlerhelper.h"
#include "response.h"
#include "storage/datastore.h"
#include "storage/transaction.h"
#include "storage/itemqueryhelper.h"
#include "storage/selectquerybuilder.h"
#include "storage/parthelper.h"
#include "storage/dbconfig.h"
#include "storage/itemretriever.h"

#include "libs/imapparser_p.h"
#include "libs/protocol_p.h"
#include "imapstreamparser.h"

#include <QtCore/QStringList>
#include <QLocale>
#include <QDebug>
#include <QFile>

#include <algorithm>
#include <functional>

using namespace Akonadi;

Store::Store( Scope::SelectionScope scope )
  : Handler()
  , mScope( scope )
  , mPos( 0 )
  , mPreviousRevision( -1 )
  , mSize( 0 )
  , mCheckRevision( false )
{
}

bool Store::replaceFlags( const PimItem &item, const QList<QByteArray> &flags )
{
  Flag::List flagList = HandlerHelper::resolveFlags( flags );
  DataStore *store = connection()->storageBackend();

  Flag::List currentFlags = item.flags();
  std::sort( flagList.begin(), flagList.end(), _detail::ById<std::less>() );
  std::sort( currentFlags.begin(), currentFlags.end(), _detail::ById<std::less>() );

  if ( flagList == currentFlags )
    return false;

  if ( !store->setItemFlags( item, flagList ) )
    throw HandlerException( "Store::replaceFlags: Unable to set new item flags" );

  return true;
}

bool Store::addFlags( const PimItem &item, const QList<QByteArray> &flags, bool& flagsChanged )
{
  const Flag::List flagList = HandlerHelper::resolveFlags( flags );
  DataStore *store = connection()->storageBackend();

  if ( !store->appendItemFlags( item, flagList, flagsChanged ) ) {
    qDebug( "Store::addFlags: Unable to add new item flags" );
    return false;
  }
  return true;
}

bool Store::deleteFlags( const PimItem &item, const QList<QByteArray> &flags )
{
  DataStore *store = connection()->storageBackend();

  QVector<Flag> flagList;
  flagList.reserve( flags.size() );
  for ( int i = 0; i < flags.count(); ++i ) {
    Flag flag = Flag::retrieveByName( QString::fromUtf8( flags[ i ] ) );
    if ( !flag.isValid() )
      continue;

    flagList.append( flag );
  }

  if ( !store->removeItemFlags( item, flagList ) ) {
    qDebug( "Store::deleteFlags: Unable to remove item flags" );
    return false;
  }
  return true;
}

bool Store::parseStream()
{
  parseCommand();
  DataStore *store = connection()->storageBackend();
  Transaction transaction( store );
   // Set the same modification time for each item.
  const QDateTime modificationtime = QDateTime::currentDateTime().toUTC();

  // retrieve selected items
  SelectQueryBuilder<PimItem> qb;
  ItemQueryHelper::scopeToQuery( mScope, connection(), qb );
  if ( !qb.exec() )
    return failureResponse( "Unable to retrieve items" );
  PimItem::List pimItems = qb.result();
  if ( pimItems.isEmpty() )
    return failureResponse( "No items found" );

  for ( int i = 0; i < pimItems.size(); ++i ) {
    if ( mCheckRevision ) {
      // check for conflicts if a resources tries to overwrite an item with dirty payload
      if ( connection()->isOwnerResource( pimItems.at( i ) ) ) {
        if ( pimItems.at( i ).dirty() )
          throw HandlerException( "[LRCONFLICT] Resource tries to modify item with dirty payload, aborting STORE." );
      }

      // check and update revisions
      if ( pimItems.at( i ).rev() != (int)mPreviousRevision )
        throw HandlerException( "[LLCONFLICT] Item was modified elsewhere, aborting STORE." );
    }
    pimItems[ i ].setRev( pimItems[ i ].rev() + 1 );
  }

  QSet<QByteArray> changes;
  qint64 partSizes = 0;
  bool invalidateCache = false;
  bool silent = false;

  // apply modifications
  m_streamParser->beginList();
  while ( !m_streamParser->atListEnd() ) {
    // parse the command
    QByteArray command = m_streamParser->readString();
    if ( command.isEmpty() )
      throw HandlerException( "Syntax error" );
    Operation op = Replace;
    if ( command.startsWith( '+' ) ) {
      op = Add;
      command = command.mid( 1 );
    } else if ( command.startsWith( '-' ) ) {
      op = Delete;
      command = command.mid( 1 );
    }
    if ( command.endsWith( ".SILENT" ) ) {
      command.chop( 7 );
      silent = true;
    }
//     qDebug() << "STORE: handling command: " << command;


    // handle commands that can be applied to more than one item
    if ( command == AKONADI_PARAM_FLAGS ) {
      bool flagsChanged = true;
      const QList<QByteArray> flags = m_streamParser->readParenthesizedList();
      // TODO move this iteration to an SQL query.
      for ( int i = 0; i < pimItems.count(); ++i ) {
        if ( op == Replace ) {
          flagsChanged = replaceFlags( pimItems[ i ], flags );
        } else if ( op == Add ) {
          if ( !addFlags( pimItems[ i ], flags, flagsChanged ) )
            return failureResponse( "Unable to add item flags." );
        } else if ( op == Delete ) {
          if ( !deleteFlags( pimItems[ i ], flags ) )
            return failureResponse( "Unable to remove item flags." );
        }
      }

      if ( flagsChanged )
        changes << AKONADI_PARAM_FLAGS;
      continue;
    }


    // handle commands that can only be applied to one item
    if ( pimItems.size() > 1 )
      throw HandlerException( "This Modification can only be applied to a single item" );
    PimItem &item = pimItems.first();
    if ( !item.isValid() )
      throw HandlerException( "Invalid item in query result!?" );

    if ( command == AKONADI_PARAM_REMOTEID ) {
      const QString rid = m_streamParser->readUtf8String();
      if ( item.remoteId() != rid ) {
        if ( !connection()->isOwnerResource( item ) )
          throw HandlerException( "Only resources can modify remote identifiers" );
        item.setRemoteId( rid );
        changes << AKONADI_PARAM_REMOTEID;
      }
    }

    else if ( command == AKONADI_PARAM_REMOTEREVISION ) {
      const QString remoteRevision = m_streamParser->readUtf8String();
      if ( item.remoteRevision() != remoteRevision ) {
        if ( !connection()->isOwnerResource( item ) )
          throw HandlerException( "Only resources can modify remote revisions" );
        item.setRemoteRevision( remoteRevision );
        changes << AKONADI_PARAM_REMOTEREVISION;
      }
    }

    else if ( command == AKONADI_PARAM_UNDIRTY ) {
      m_streamParser->readString(); // read the 'false' string
      item.setDirty( false );
    }

    else if ( command == AKONADI_PARAM_INVALIDATECACHE ) {
      invalidateCache = true;
    }

    else if ( command == AKONADI_PARAM_SIZE ) {
      mSize = m_streamParser->readNumber();
      changes << AKONADI_PARAM_SIZE;
    }

    else if ( command == "PARTS" ) {
      const QList<QByteArray> parts = m_streamParser->readParenthesizedList();
      if ( op == Delete ) {
        if ( !store->removeItemParts( item, parts ) )
          return failureResponse( "Unable to remove item parts." );
        changes += QSet<QByteArray>::fromList( parts );
      }
    }

    else if ( command == "COLLECTION" ) {
      throw HandlerException( "Item moving via STORE is deprecated, update your Akonadi client" );
    }

    // parts/attributes
    else {
      // obtain and configure the part object
      int partVersion = 0;
      QByteArray partName;
      ImapParser::splitVersionedKey( command, partName, partVersion );

      SelectQueryBuilder<Part> qb;
      qb.addValueCondition( Part::pimItemIdColumn(), Query::Equals, item.id() );
      qb.addValueCondition( Part::nameColumn(), Query::Equals, QString::fromUtf8( partName ) );
      if ( !qb.exec() )
        return failureResponse( "Unable to check item part existence" );
      Part::List result = qb.result();
      Part part;
      if ( !result.isEmpty() )
        part = result.first();
      part.setName( QString::fromUtf8( partName ) );
      part.setVersion( partVersion );
      part.setPimItemId( item.id() );

      QByteArray value;
      if ( m_streamParser->hasLiteral() ) {
        const qint64 dataSize = m_streamParser->remainingLiteralSize();
        if ( partName.startsWith( "PLD:" ) )
          partSizes += dataSize;
        const bool storeInFile = dataSize > DbConfig::configuredDatabase()->sizeThreshold();
        //actual case when streaming storage is used: external payload is enabled, data is big enough in a literal
        if ( storeInFile ) {
          // use first part as value for the initial insert into / update to the database.
          // this will give us a proper filename to stream the rest of the parts contents into
          // NOTE: we have to set the correct size (== dataSize) directly
          value = m_streamParser->readLiteralPart();
         // qDebug() << Q_FUNC_INFO << "VALUE in STORE: " << value << value.size() << dataSize;

          if ( part.isValid() ) {
            PartHelper::update( &part, value, dataSize );
          } else {
//             qDebug() << "insert from Store::handleLine";
            part.setData( value );
            part.setDatasize( dataSize );
            if ( !PartHelper::insert( &part ) )
              return failureResponse( "Unable to add item part" );
          }

          //the actual streaming code for the remaining parts:
          // reads from the parser, writes immediately to the file
          // ### move this entire block to part helper? should be useful for append as well
          const QString fileName = QString::fromUtf8( part.data() );
          QFile file( fileName );
          if ( file.open( QIODevice::WriteOnly | QIODevice::Append ) ) {
            while ( !m_streamParser->atLiteralEnd() ) {
              value = m_streamParser->readLiteralPart();
              file.write( value ); // ### error handling?
            }
            file.close();
          } else {
            return failureResponse( "Unable to update item part" );
          }

          changes << partName;
          continue;
        } else { // not store in file
          //don't write in streaming way as the data goes to the database
          while (!m_streamParser->atLiteralEnd()) {
            value += m_streamParser->readLiteralPart();
          }
        }
      } else { //not a literal
        value = m_streamParser->readString();
        if ( partName.startsWith( "PLD:" ) )
          partSizes += value.size();
      }

      // only relevant for non-literals or non-external literals
      const QByteArray origData = PartHelper::translateData( part );
      if ( origData != value ) {
        if ( part.isValid() ) {
          PartHelper::update( &part, value, value.size() );
        } else {
//           qDebug() << "insert from Store::handleLine: " << value.left(100);
          part.setData( value );
          part.setDatasize( value.size() );
          if ( !PartHelper::insert( &part ) )
            return failureResponse( "Unable to add item part" );
        }
        changes << partName;
      }

    } // parts/attribute modification
  }

  QString datetime;
  if ( !changes.isEmpty() ) {
    // update item size
    if ( pimItems.size() == 1 && (mSize > 0 || partSizes > 0) )
      pimItems.first().setSize( qMax( mSize, partSizes ) );

    // run update query and prepare change notifications
    for ( int i = 0; i < pimItems.count(); ++i ) {
      PimItem &item = pimItems[ i ];
      item.setDatetime( modificationtime );
      item.setAtime( modificationtime );
      if ( !connection()->isOwnerResource( item ) )
        item.setDirty( true );
      if ( !item.update() )
        throw HandlerException( "Unable to write item changes into the database" );

      if ( invalidateCache ) {
        if ( !store->invalidateItemCache( item ) ) {
          throw HandlerException( "Unable to invalidate item cache in the database" );
        }
      }

      store->notificationCollector()->itemChanged( item, changes );
      if ( !silent )
        sendPimItemResponse( item );
    }

    if ( !transaction.commit() )
      return failureResponse( "Cannot commit transaction." );

    datetime = QLocale::c().toString( modificationtime, QLatin1String( "dd-MMM-yyyy hh:mm:ss +0000" ) );
  } else {
    datetime = QLocale::c().toString( pimItems.first().datetime(), QLatin1String( "dd-MMM-yyyy hh:mm:ss +0000" ) );
  }

  // TODO: When implementing support for modifying multiple items at once, the revisions of the items should be in the responses.
  // or only modified items should appear in the repsponse.
  Response response;
  response.setTag( tag() );
  response.setSuccess();
  response.setString( "DATETIME " + ImapParser::quote( datetime.toUtf8() ) + " STORE completed" );

  emit responseAvailable( response );
  return true;
}

void Store::parseCommand()
{
  mScope.parseScope( m_streamParser );

  // parse the stuff before the modification list
  while ( !m_streamParser->hasList() ) {
    const QByteArray command = m_streamParser->readString();
    if ( command.isEmpty() ) { // ie. we are at command end
      throw HandlerException( "No modification list provided in STORE command" );
    } else if ( command == AKONADI_PARAM_REVISION ) {
      mPreviousRevision = m_streamParser->readNumber();
      mCheckRevision = true;
    } else if ( command == AKONADI_PARAM_SIZE ) {
      mSize = m_streamParser->readNumber();
    }
  }
}

void Store::sendPimItemResponse( const PimItem &pimItem )
{
  QList<QByteArray> attrs;
  attrs.push_back( "REV" );
  attrs.push_back( QByteArray::number( pimItem.rev() ) );

  QByteArray result;
  result += QByteArray::number( pimItem.id() );
  result += " FETCH (";
  result += ImapParser::join( attrs, " " );
  result += ')';

  Response response;
  response.setUntagged();
  response.setString( result );
  emit responseAvailable( response );
}

#include "store.moc"
