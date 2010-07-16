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

#ifndef AKONADI_DBUPDATER_H
#define AKONADI_DBUPDATER_H

#include <QtCore/QMap>
#include <QtCore/QStringList>
#include <QtSql/QSqlDatabase>

#include "../akonadiprivate_export.h"

class QDomElement;

/**
 * @short A helper class that contains an update set.
 */
class UpdateSet
{
  public:
    typedef QMap<int, UpdateSet> Map;

    UpdateSet()
      : version( -1 ), abortOnFailure( false )
    {
    }

    int version;
    bool abortOnFailure;
    QStringList statements;
};

/**
  Updates the database schema.
*/
class AKONADIPRIVATE_EXPORT DbUpdater
{
  public:
    /**
     * Creates a new database updates.
     *
     * @param database The reference to the database.
     * @param filename The file containing the update descriptions.
     */
    DbUpdater( const QSqlDatabase &database, const QString &filename );

    /**
     * Starts the update process.
     * On success true is returned, false otherwise.
     */
    bool run();

  private:
    friend class DbUpdaterTest;

    bool updateApplicable( const QString& ) const;
    QString buildRawSqlStatement( const QDomElement& ) const;

    bool parseUpdateSets( int, UpdateSet::Map& ) const;

    QSqlDatabase m_database;
    QString m_filename;
};

#endif
