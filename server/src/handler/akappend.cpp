/***************************************************************************
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

#include "akappend.h"

#include "libs/imapparser_p.h"
#include "imapstreamparser.h"

#include "append.h"
#include "response.h"
#include "handlerhelper.h"

#include "akonadi.h"
#include "akonadiconnection.h"
#include "preprocessormanager.h"
#include "storage/datastore.h"
#include "storage/entity.h"
#include "storage/transaction.h"

#include <libs/protocol_p.h>

#include <QtCore/QDebug>

using namespace Akonadi;

AkAppend::AkAppend()
    : Handler()
{
}

AkAppend::~AkAppend()
{
}

bool Akonadi::AkAppend::commit()
{
    Response response;

    DataStore *db = connection()->storageBackend();
    Transaction transaction( db );

    Collection col = HandlerHelper::collectionFromIdOrName( m_mailbox );
    if ( !col.isValid() ) {
      return failureResponse( QByteArray( "Unknown collection for '" ) + m_mailbox + QByteArray( "'." ) );
    }

    QByteArray mt;
    QString remote_id;
    QString remote_revision;
    QString gid;
    QList<QByteArray> flags;
    Q_FOREACH ( const QByteArray &flag, m_flags ) {
      if ( flag.startsWith( AKONADI_FLAG_MIMETYPE ) ) {
        int pos1 = flag.indexOf( '[' );
        int pos2 = flag.indexOf( ']', pos1 );
        mt = flag.mid( pos1 + 1, pos2 - pos1 - 1 );
      } else if ( flag.startsWith( AKONADI_FLAG_REMOTEID ) ) {
        int pos1 = flag.indexOf( '[' );
        int pos2 = flag.lastIndexOf( ']' );
        remote_id = QString::fromUtf8( flag.mid( pos1 + 1, pos2 - pos1 - 1 ) );
      } else if ( flag.startsWith( AKONADI_FLAG_REMOTEREVISION ) ) {
        int pos1 = flag.indexOf( '[' );
        int pos2 = flag.lastIndexOf( ']' );
        remote_revision = QString::fromUtf8( flag.mid( pos1 + 1, pos2 - pos1 - 1 ) );
      } else if ( flag.startsWith( AKONADI_FLAG_GID ) ) {
        int pos1 = flag.indexOf( '[' );
        int pos2 = flag.lastIndexOf( ']' );
        gid = QString::fromUtf8( flag.mid( pos1 + 1, pos2 - pos1 - 1 ) );
      } else {
        flags << flag;
      }
    }
    // standard imap does not know this attribute, so that's mail
    if ( mt.isEmpty() ) {
      mt = "message/rfc822";
    }
    MimeType mimeType = MimeType::retrieveByName( QString::fromLatin1( mt ) );
    if ( !mimeType.isValid() ) {
      MimeType m( QString::fromLatin1( mt ) );
      if ( !m.insert() ) {
        return failureResponse( QByteArray( "Unable to create mimetype '" ) + mt + QByteArray( "'." ) );
      }
      mimeType = m;
    }

    PimItem item;
    item.setRev( 0 );
    item.setSize( m_size );

    // If we have active preprocessors then we also set the hidden attribute
    // for the UI and we enqueue the item for preprocessing.
    bool doPreprocessing = PreprocessorManager::instance()->isActive();

    if ( doPreprocessing ) {
      Part hiddenAttribute;
      hiddenAttribute.setName( QLatin1String( AKONADI_ATTRIBUTE_HIDDEN ) );
      hiddenAttribute.setData( QByteArray() );
      hiddenAttribute.setPimItemId( item.id() );

      m_parts.append( hiddenAttribute );
    }

    bool ok = db->appendPimItem( m_parts, mimeType, col, m_dateTime, remote_id, remote_revision, gid, item );

    response.setTag( tag() );
    if ( !ok ) {
      return failureResponse( "Append failed" );
    }

    // set message flags
    const Flag::List flagList = HandlerHelper::resolveFlags( flags );
    bool flagsChanged = false;
    if ( !db->appendItemsFlags( PimItem::List() << item, flagList, flagsChanged, false, col ) ) {
      return failureResponse( "Unable to append item flags." );
    }

    // TODO if the mailbox is currently selected, the normal new message
    //      actions SHOULD occur.  Specifically, the server SHOULD notify the
    //      client immediately via an untagged EXISTS response.

    if ( !transaction.commit() ) {
      return failureResponse( "Unable to commit transaction." );
    }

    if ( doPreprocessing ) {
      // enqueue the item for preprocessing
      PreprocessorManager::instance()->beginHandleItem( item, db );
    }

    response.setTag( tag() );
    response.setUserDefined();
    response.setString( "[UIDNEXT " + QByteArray::number( item.id() ) + ']' );
    Q_EMIT responseAvailable( response );

    response.setSuccess();
    response.setString( "Append completed" );
    Q_EMIT responseAvailable( response );
    return true;
}

bool AkAppend::parseStream()
{
    // Arguments:  mailbox name
    //        OPTIONAL flag parenthesized list
    //        OPTIONAL date/time string
    //        (partname literal)+
    //
    // Syntax:
    // x-akappend = "X-AKAPPEND" SP mailbox SP size [SP flag-list] [SP date-time] SP (partname SP literal)+

  m_mailbox = m_streamParser->readString();

  m_size = m_streamParser->readNumber();

  // parse optional flag parenthesized list
  // Syntax:
  // flag-list      = "(" [flag *(SP flag)] ")"
  // flag           = "\ANSWERED" / "\FLAGGED" / "\DELETED" / "\SEEN" /
  //                  "\DRAFT" / flag-keyword / flag-extension
  //                    ; Does not include "\Recent"
  // flag-extension = "\" atom
  // flag-keyword   = atom
  if ( m_streamParser->hasList() ) {
    m_flags = m_streamParser->readParenthesizedList();
  }

  // parse optional date/time string
  if ( m_streamParser->hasDateTime() ) {
    m_dateTime = m_streamParser->readDateTime();
    // FIXME Should we return an error if m_dateTime is invalid?
  }
  // if date/time is not given then it will be set to the current date/time
  // by the database

  // parse part specification
  QVector<QPair<QByteArray, QPair<qint64, int> > > partSpecs;
  QByteArray partName = "";
  qint64 partSize = -1;
  qint64 partSizes = 0;
  bool ok = false;

  QList<QByteArray> list = m_streamParser->readParenthesizedList();
  Q_FOREACH ( const QByteArray &item, list ) {
    if ( partName.isEmpty() && partSize == -1 ) {
      partName = item;
      continue;
    }
    if ( item.startsWith( ':' ) ) {
      int pos = 1;
      ImapParser::parseNumber( item, partSize, &ok, pos );
      if ( !ok ) {
        partSize = 0;
      }

      int version = 0;
      QByteArray plainPartName;
      ImapParser::splitVersionedKey( partName, plainPartName, version );

      partSpecs.append( qMakePair( plainPartName, qMakePair( partSize, version ) ) );
      partName = "";
      partSizes += partSize;
      partSize = -1;
    }
  }

  m_size = qMax( partSizes, m_size );

  // TODO streaming support!
  QByteArray allParts = m_streamParser->readString();

  // chop up literal data in parts
  int pos = 0; // traverse through part data now
  QPair<QByteArray, QPair<qint64, int> > partSpec;
  Q_FOREACH ( partSpec, partSpecs ) {
    // wrap data into a part
    Part part;
    part.setName( QLatin1String( partSpec.first ) );
    part.setData( allParts.mid( pos, partSpec.second.first ) );
    if ( partSpec.second.second != 0 ) {
      part.setVersion( partSpec.second.second );
    }
    part.setDatasize( partSpec.second.first );
    m_parts.append( part );
    pos += partSpec.second.first;
  }

  return commit();
}
