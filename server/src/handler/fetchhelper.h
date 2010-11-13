/***************************************************************************
 *   Copyright (C) 2006-2009 by Tobias Koenig <tokoe@kde.org>              *
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

#ifndef AKONADI_FETCHHELPER_H
#define AKONADI_FETCHHELPER_H

#include <QtCore/QStack>

#include "scope.h"
#include "libs/imapset_p.h"
#include "storage/countquerybuilder.h"
#include "storage/datastore.h"
#include "storage/itemretriever.h"

namespace Akonadi {

class AkonadiConnection;
class ImapSet;
class Response;

class FetchHelper : public QObject
{
  Q_OBJECT

  public:
    FetchHelper( AkonadiConnection *connection, const Scope &scope );

    void setStreamParser( ImapStreamParser *parser );

    bool parseStream( const QByteArray &responseIdentifier );

  Q_SIGNALS:
    void responseAvailable( const Response& );

  private:
    void init();
    void parseCommand( const QByteArray &line );
    void updateItemAccessTime();
    void triggerOnDemandFetch();
    QSqlQuery buildItemQuery();
    QSqlQuery buildPartQuery( const QStringList &partList, bool allPayload, bool allAttrs );
    QSqlQuery buildFlagQuery();
    void parseCommandStream();
    QStack<Collection> ancestorsForItem( Collection::Id parentColId );

  private:
    ImapStreamParser *mStreamParser;

    AkonadiConnection *mConnection;
    Scope mScope;
    QList<QByteArray> mRequestedParts;
    QHash<Collection::Id, QStack<Collection> > mAncestorCache;
    int mAncestorDepth;
    bool mCacheOnly;
    bool mFullPayload;
    bool mAllAttrs;
    bool mSizeRequested;
    bool mMTimeRequested;
    bool mExternalPayloadSupported;
    bool mRemoteRevisionRequested;
};

}

#endif
