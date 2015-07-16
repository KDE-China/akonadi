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

#include "fetch.h"

#include "connection.h"
#include "fetchhelper.h"
#include "cachecleaner.h"
#include "storage/selectquerybuilder.h"

using namespace Akonadi;
using namespace Akonadi::Server;


bool Fetch::parseStream()
{
    Protocol::FetchItemsCommand cmd(m_command);

    if (!connection()->context()->setScopeContext(cmd.scopeContext())) {
        return failureResponse("Invalid scope context");
    }

    // We require context in case we do RID fetch
    if (connection()->context()->isEmpty() && cmd.scope().scope() == Scope::Rid) {
        return failureResponse("No FETCH context specified");
    }

    CacheCleanerInhibitor inhibitor;

    FetchHelper fetchHelper(connection(), cmd.scope(), cmd.fetchScope());
    if (!fetchHelper.fetchItems()) {
        return failureResponse("Failed to fetch items");
    }

    return successResponse<Protocol::FetchItemsResponse>();
}