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

#ifndef DBINITIALIZER_H
#define DBINITIALIZER_H

#include "dbintrospector.h"

#include <QtCore/QHash>
#include <QtCore/QPair>
#include <QtCore/QVector>
#include <QtCore/QStringList>
#include <QtSql/QSqlDatabase>

#include <boost/shared_ptr.hpp>

class QDomElement;

class DebugInterface
{
  public:
    virtual ~DebugInterface() {};
    virtual void createTableStatement( const QString &tableName, const QString &statement ) = 0;
};

/**
 * A helper class which takes a reference to a database object and
 * the file name of a template file and initializes the database
 * according to the rules in the template file.
 */
class DbInitializer
{
  public:
   typedef boost::shared_ptr<DbInitializer> Ptr;

    /**
      Returns an initializer instance for a given backend.
    */
    static DbInitializer::Ptr createInstance( const QSqlDatabase &database, const QString &templateFile );

    /**
     * Destroys the database initializer.
     */
    virtual ~DbInitializer();

    /**
     * Starts the initialization process.
     * On success true is returned, false otherwise.
     *
     * If something went wrong @see errorMsg() can be used to retrieve more
     * information.
     */
    bool run();

    /**
     * Returns the textual description of an occurred error.
     */
    QString errorMsg() const;

  protected:
    /**
     * @short A helper class that describes a column of a table for the DbInitializer
     */
    class ColumnDescription
    {
      public:
        ColumnDescription();

        QString name;
        QString type;
        int size;
        bool allowNull;
        bool isAutoIncrement;
        bool isPrimaryKey;
        bool isUnique;
        QString refTable;
        QString refColumn;
        QString defaultValue;
    };

    /**
     * @short A helper class that describes indexes of a table for the DbInitializer
     */
    class IndexDescription
    {
      public:
        IndexDescription();

        QString name;
        QStringList columns;
        bool isUnique;
    };

    /**
     * @short A helper class that describes the predefined data of a table for the DbInitializer
     */
    class DataDescription
    {
      public:
        DataDescription();

        /**
         * Key contains the column name, value the data.
         */
        QHash<QString, QString> data;
    };

    /**
     * @short A helper class that describes a table for the DbInitializer
     */
    class TableDescription
    {
      public:
        TableDescription();

        QString name;
        QVector<ColumnDescription> columns;
        QVector<IndexDescription> indexes;
        QVector<DataDescription> data;
    };

    /**
     * @short A helper class that describes the relation between to tables for the DbInitializer
     */
    class RelationDescription
    {
      public:
        RelationDescription();

        QString firstTable;
        QString firstTableName;
        QString firstColumn;
        QString secondTable;
        QString secondTableName;
        QString secondColumn;
    };

    /**
     * Creates a new database initializer.
     *
     * @param database The reference to the database.
     * @param templateFile The template file.
     */
    DbInitializer( const QSqlDatabase &database, const QString &templateFile );

    /**
     * Overwrite in backend-specific sub-classes to return the SQL type for a given C++ type.
     * @param type Name of the C++ type.
     * @param size Optional size hint for the column, if -1 use the default SQL type for @p type.
     */
    virtual QString sqlType( const QString &type, int size ) const;
    /** Overwrite in backend-specific sub-classes to return the SQL value for a given C++ value. */
    virtual QString sqlValue( const QString &type, const QString &value ) const;

    virtual QString buildCreateTableStatement( const TableDescription &tableDescription ) const = 0;
    virtual QString buildColumnStatement( const ColumnDescription &columnDescription ) const = 0;
    virtual QString buildAddColumnStatement( const TableDescription &tableDescription, const ColumnDescription &columnDescription ) const;
    virtual QString buildCreateIndexStatement( const TableDescription &tableDescription, const IndexDescription &indexDescription ) const;
    virtual QString buildInsertValuesStatement( const TableDescription &tableDescription, const DataDescription &dataDescription ) const = 0;
    virtual QString buildCreateRelationTableStatement( const QString &tableName, const RelationDescription &relationDescription ) const;

  private:
    friend class DbInitializerTest;

    /**
     * Sets the debug @p interface that shall be used on unit test run.
     */
    void setDebugInterface( DebugInterface *interface );

    /**
     * Runs the DbInitializer in unit test mode.
     * This won't change anything in the database, only calls the code
     * to generate SQL statements.
     */
    void unitTestRun();


    bool checkTable( const QDomElement& );
    bool checkRelation( const QDomElement &element );

    TableDescription parseTableDescription( const QDomElement& ) const;
    RelationDescription parseRelationDescription( const QDomElement& ) const;

    QSqlDatabase mDatabase;
    QString mTemplateFile;
    QString mErrorMsg;
    DebugInterface *mDebugInterface;
    DbIntrospector::Ptr m_introspector;
};

#endif
