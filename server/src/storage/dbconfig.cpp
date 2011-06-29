/*
    Copyright (c) 2010 Tobias Koenig <tokoe@kde.org>

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

#include "dbconfig.h"

#include "dbconfigmysql.h"
#include "dbconfigmysqlembedded.h"
#include "dbconfigpostgresql.h"
#include "dbconfigsqlite.h"
#include "dbconfigvirtuoso.h"

#include <akdebug.h>
#include <libs/xdgbasedirs_p.h>
#include <config-akonadi.h>

#include <QtCore/QDir>

using namespace Akonadi;

//TODO: make me Q_GLOBAL_STATIC
static DbConfig *s_DbConfigInstance = 0;

DbConfig::DbConfig()
{
  const QString serverConfigFile = XdgBaseDirs::akonadiServerConfigFile( XdgBaseDirs::ReadWrite );
  QSettings settings( serverConfigFile, QSettings::IniFormat );

  mSizeThreshold = 4096;
  const QVariant value = settings.value( QLatin1String( "General/SizeThreshold" ), mSizeThreshold );
  if ( value.canConvert<qint64>() )
    mSizeThreshold = value.value<qint64>();
  else
    mSizeThreshold = 0;

  if ( mSizeThreshold < 0 )
    mSizeThreshold = 0;
}

DbConfig::~DbConfig()
{
}

DbConfig* DbConfig::configuredDatabase()
{
  if ( !s_DbConfigInstance ) {
    const QString serverConfigFile = XdgBaseDirs::akonadiServerConfigFile( XdgBaseDirs::ReadWrite );
    QSettings settings( serverConfigFile, QSettings::IniFormat );

    // determine driver to use
    QString driverName = settings.value( QLatin1String( "General/Driver" ) ).toString();
    if ( driverName.isEmpty() ) {
      driverName = QLatin1String( AKONADI_DATABASE_BACKEND );
      // when using the default, write it explicitly, in case the default changes later
      settings.setValue( QLatin1String("General/Driver"), driverName );
      settings.sync();
    }

    if ( driverName == QLatin1String( "QMYSQL" ) )
      s_DbConfigInstance = new DbConfigMysql;
    else if ( driverName == QLatin1String( "QMYSQL_EMBEDDED" ) )
      s_DbConfigInstance = new DbConfigMysqlEmbedded;
    else if ( driverName == QLatin1String( "QSQLITE" ) )
      s_DbConfigInstance = new DbConfigSqlite( DbConfigSqlite::Default );
    else if ( driverName == QLatin1String( "QSQLITE3" ) )
      s_DbConfigInstance = new DbConfigSqlite( DbConfigSqlite::Custom );
    else if ( driverName == QLatin1String( "QPSQL" ) )
      s_DbConfigInstance = new DbConfigPostgresql;
    else if ( driverName == QLatin1String( "QODBC" ) )
      s_DbConfigInstance = new DbConfigVirtuoso;
    else {
      akError() << "Unknown database driver: " << driverName;
      akError() << "Available drivers are: " << QSqlDatabase::drivers();
      akFatal();
    }
  }

  return s_DbConfigInstance;
}

void DbConfig::startInternalServer()
{
  // do nothing
}

void DbConfig::stopInternalServer()
{
  // do nothing
}

void DbConfig::setup()
{
  // do nothing
}

qint64 DbConfig::sizeThreshold() const
{
  return mSizeThreshold;
}
