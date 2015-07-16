/***************************************************************************
 *   Copyright (C) 2013 by Volker Krause <vkrause@kde.org>                 *
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

#include "session.h"

#include <shared/akapplication.h>
#include <shared/akstandarddirs.h>

#include <QCoreApplication>
#include <QDebug>

int main(int argc, char **argv)
{
    AkCoreApplication app(argc, argv);
    app.setDescription(QLatin1String("Akonadi ASAP cat\n"
                                     "This is a development tool, only use this if you know what you are doing."));

    app.addPositionalCommandLineOption(QLatin1String("input"), QLatin1String("Input file to read commands from"));
    app.parseCommandLine();


    const QStringList args = app.commandLineArguments().positionalArguments();
    if (args.isEmpty()) {
        app.printUsage();
        return -1;
    }

    Session session(args[0]);
    QObject::connect(&session, SIGNAL(disconnected()), QCoreApplication::instance(), SLOT(quit()));
    QMetaObject::invokeMethod(&session, "connectToHost", Qt::QueuedConnection);

    const int result = app.exec();
    session.printStats();
    return result;
}