/*
    Copyright (c) 2009 Volker Krause <vkrause@kde.org>

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

#include "resourceselect.h"

#include "connection.h"
#include "entities.h"
#include "imapstreamparser.h"

#include <private/protocol_p.h>

using namespace Akonadi;
using namespace Akonadi::Server;


bool ResourceSelect::parseStream()
{
    Protocol::SelectResourceCommand cmd;
    mInStream >> cmd;

    if (cmd.resourceId().isEmpty()) {
        connection()->context()->setResource(Resource());
        mOutStream << Protocol::SelectResourceResponse();
        return true;
    }

    const Resource res = Resource::retrieveByName(cmd.resourceId());
    if (!res.isValid()) {
        return failureResponse<Protocol::SelectResourceResponse>(
            cmd.resourceId() % QStringLiteral(" is not a valid resource identifier"));
    }

    connection()->context()->setResource(res);

    mOutStream << Protocol::SelectResourceResponse();
    return true;
}
