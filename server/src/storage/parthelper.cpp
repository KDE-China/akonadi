/***************************************************************************
 *   Copyright (C) 2009 by Andras Mantia <amantia@kde.org>                 *
 *   Copyright (C) 2010 Volker Krause <vkrause@kde.org>                    *
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

#include "parthelper.h"
#include <akdebug.h>
#include "entities.h"
#include "selectquerybuilder.h"
#include "dbconfig.h"
#include <akstandarddirs.h>
#include <libs/xdgbasedirs_p.h>

#include <QDir>
#include <QFile>
#include <QDebug>

#include <QSqlError>

using namespace Akonadi;

static QString fileNameForPart( Part *part )
{
  Q_ASSERT( part->id() >= 0 );
  const QString dataDir = AkStandardDirs::saveDir( "data", QLatin1String( "file_db_data" ) ) + QDir::separator();
  Q_ASSERT( dataDir != QDir::separator() );
  return dataDir + QString::number( part->id() );
}

void PartHelper::update( Part *part, const QByteArray &data, qint64 dataSize )
{
  if (!part)
    throw PartHelperException( "Invalid part" );

  QString origFileName;
  // currently external, so recover the filename to delete it after the update succeeded
  if ( part->external() )
    origFileName = QString::fromUtf8( part->data() );

  const bool storeExternal = dataSize > DbConfig::configuredDatabase()->sizeThreshold();

  if ( storeExternal ) {
    QString fileName = origFileName;
    if ( fileName.isEmpty() )
      fileName = fileNameForPart( part );
    QString rev = QString::fromAscii("_r0");
    if ( fileName.contains( QString::fromAscii("_r") ) ) {
      int revIndex = fileName.indexOf(QString::fromAscii("_r"));
      rev = fileName.mid( revIndex + 2  );
      int r = rev.toInt();
      r++;
      rev = QString::number( r );
      fileName = fileName.left( revIndex );
      rev.prepend( QString::fromAscii("_r") );
    }
    fileName += rev;

    QFile file( fileName );
    if ( file.open( QIODevice::WriteOnly | QIODevice::Truncate ) ) {
      if ( file.write( data ) == data.size() ) {
        part->setData( fileName.toLocal8Bit() );
        part->setExternal( true );
      } else {
        throw PartHelperException( QString::fromLatin1( "Failed to write into '%1', error was '%2'" ).arg( fileName ).arg( file.errorString() ) );
      }
      file.close();
    } else  {
     throw PartHelperException( QString::fromLatin1( "Could not open '%1' for writing, error was '%2'" ).arg( fileName ).arg( file.errorString() ) );
    }

  // internal storage
  } else {
    part->setData( data );
    part->setExternal( false );
  }

  part->setDatasize( dataSize );
  const bool result = part->update();
  if ( !result )
    throw PartHelperException( "Failed to update database record" );
  // everything worked, remove the old file
  if ( !origFileName.isEmpty() )
    QFile::remove( origFileName );
}

bool PartHelper::insert( Part *part, qint64* insertId )
{
  if (!part)
    return false;

  const bool storeInFile = part->datasize() > DbConfig::configuredDatabase()->sizeThreshold();

  //it is needed to insert first the metadata so a new id is generated for the part,
  //and we need this id for the payload file name
  QByteArray data;
  if ( storeInFile ) {
    data = part->data();
    part->setData( QByteArray() );
    part->setExternal( true );
  } else {
    part->setExternal( false );
  }

  bool result = part->insert( insertId );

  if ( storeInFile && result )
  {
    QString fileName = fileNameForPart( part );
    fileName +=  QString::fromUtf8("_r0");

    QFile file( fileName );
    if ( file.open( QIODevice::WriteOnly | QIODevice::Truncate ) ) {
      if ( file.write( data ) == data.size() ) {
        part->setData( fileName.toLocal8Bit() );
        result = part->update();
      } else {
        akError() << "Insert: payload file " << fileName << " could not be written to!";
        akError() << "Error: " << file.errorString();
        return false;
      }
      file.close();
    } else {
      akError() << "Insert: payload file " << fileName << " could not be open for writing!";
      akError() << "Error: " << file.errorString();
      return false;
    }
  }
  return result;
}

bool PartHelper::remove( Akonadi::Part *part )
{
  if (!part)
    return false;

  if ( part->external() ) {
    // akDebug() << "remove part file " << part->data();
    const QString fileName = QString::fromUtf8( part->data() );
    QFile::remove( fileName );
  }
  return part->remove();
}

bool PartHelper::remove( const QString &column, const QVariant &value )
{
  SelectQueryBuilder<Part> builder;
  builder.addValueCondition( column, Query::Equals, value );
  builder.addValueCondition( Part::externalColumn(), Query::Equals, true );
  builder.addValueCondition( Part::dataColumn(), Query::IsNot, QVariant() );
  if ( !builder.exec() ) {
//      akDebug() << "Error selecting records to be deleted from table"
//          << Part::tableName() << builder.query().lastError().text();
    return false;
  }
  const Part::List parts = builder.result();
  Part::List::ConstIterator it = parts.constBegin();
  Part::List::ConstIterator end = parts.constEnd();
  for ( ; it != end; ++it )
  {
    const QString fileName = QString::fromUtf8( (*it).data() );
    // akDebug() << "remove part file " << fileName;
    QFile::remove( fileName );
  }
  return Part::remove( column, value );
}

QByteArray PartHelper::translateData( const QByteArray &data, bool isExternal )
{
  if ( isExternal ) {
    const QString fileName = QString::fromUtf8( data );
    QFile file( fileName );
    if ( file.open( QIODevice::ReadOnly ) ) {
      const QByteArray payload = file.readAll();
      file.close();
      return payload;
    } else {
      akError() << "Payload file " << fileName << " could not be open for reading!";
      akError() << "Error: " << file.errorString();
      return QByteArray();
    }
  } else {
    // not external
    return data;
  }
}

QByteArray PartHelper::translateData( const Part& part )
{
  return translateData( part.data(), part.external() );
}

bool Akonadi::PartHelper::truncate(Part& part)
{
  if ( part.external() ) {
    const QString fileName = QString::fromUtf8( part.data() );
    QFile::remove( fileName );
  }

  part.setData( QByteArray() );
  part.setDatasize( 0 );
  return part.update();
}
