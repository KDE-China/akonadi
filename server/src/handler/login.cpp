/***************************************************************************
 *   Copyright (C) 2006 by Till Adam <adam@kde.org>                        *
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
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.             *
 ***************************************************************************/
#include "login.h"

#include <QtCore/QDebug>

#include "response.h"
#include "akonadiconnection.h"
#include "imapstreamparser.h"

using namespace Akonadi;

Login::Login(): Handler()
{
}


Login::~Login()
{
}


bool Login::parseStream()
{
  QByteArray sessionId = m_streamParser->readString();
  if ( sessionId.isEmpty() )
    return failureResponse( "Missing session identifier." );
  connection()->setSessionId( sessionId );

  Response response;
  response.setTag( tag() );
  response.setSuccess();
  response.setString( "User logged in" );

  emit responseAvailable( response );
  emit connectionStateChange( Authenticated );
  deleteLater();
  return true;
}

#include "login.moc"
