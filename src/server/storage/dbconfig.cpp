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
#include "dbconfigpostgresql.h"
#include "dbconfigsqlite.h"

#include <shared/akdebug.h>
#include <private/standarddirs_p.h>
#include <private/xdgbasedirs_p.h>
#include <private/instance_p.h>

#include <QtCore/QDir>
#include <QtCore/QStringBuilder>

using namespace Akonadi;
using namespace Akonadi::Server;

//TODO: make me Q_GLOBAL_STATIC
static DbConfig *s_DbConfigInstance = Q_NULLPTR;

DbConfig::DbConfig()
{
    const QString serverConfigFile = StandardDirs::serverConfigFile(XdgBaseDirs::ReadWrite);
    QSettings settings(serverConfigFile, QSettings::IniFormat);

    mSizeThreshold = 4096;
    const QVariant value = settings.value(QStringLiteral("General/SizeThreshold"), mSizeThreshold);
    if (value.canConvert<qint64>()) {
        mSizeThreshold = value.value<qint64>();
    } else {
        mSizeThreshold = 0;
    }

    if (mSizeThreshold < 0) {
        mSizeThreshold = 0;
    }
}

DbConfig::~DbConfig()
{
}

bool DbConfig::isConfigured()
{
    return s_DbConfigInstance;
}

DbConfig *DbConfig::configuredDatabase()
{
    if (!s_DbConfigInstance) {
        const QString serverConfigFile = StandardDirs::serverConfigFile(XdgBaseDirs::ReadWrite);
        QSettings settings(serverConfigFile, QSettings::IniFormat);

        // determine driver to use
        QString driverName = settings.value(QStringLiteral("General/Driver")).toString();
        if (driverName.isEmpty()) {
            driverName = QStringLiteral(AKONADI_DATABASE_BACKEND);
            // when using the default, write it explicitly, in case the default changes later
            settings.setValue(QStringLiteral("General/Driver"), driverName);
            settings.sync();
        }

        if (driverName == QLatin1String("QMYSQL")) {
            s_DbConfigInstance = new DbConfigMysql;
        } else if (driverName == QLatin1String("QSQLITE")) {
            s_DbConfigInstance = new DbConfigSqlite(DbConfigSqlite::Default);
        } else if (driverName == QLatin1String("QSQLITE3")) {
            s_DbConfigInstance = new DbConfigSqlite(DbConfigSqlite::Custom);
        } else if (driverName == QLatin1String("QPSQL")) {
            s_DbConfigInstance = new DbConfigPostgresql;
        } else {
            akError() << "Unknown database driver: " << driverName;
            akError() << "Available drivers are: " << QSqlDatabase::drivers();
            return Q_NULLPTR;
        }

        if (!s_DbConfigInstance->init(settings)) {
            delete s_DbConfigInstance;
            s_DbConfigInstance = Q_NULLPTR;
        }
    }

    return s_DbConfigInstance;
}

bool DbConfig::startInternalServer()
{
    // do nothing
    return true;
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

QString DbConfig::defaultDatabaseName()
{
    if (!Instance::hasIdentifier()) {
        return QStringLiteral("akonadi");
    }
    // dash is not allowed in PSQL
    return QLatin1Literal("akonadi_") % Instance::identifier().replace(QLatin1Char('-'), QLatin1Char('_'));
}

void DbConfig::initSession(const QSqlDatabase &database)
{
    Q_UNUSED(database);
}
