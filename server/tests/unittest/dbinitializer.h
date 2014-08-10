/*
    Copyright (c) 2014 Christian Mollekopf <mollekopf@kolabsys.com>

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
#ifndef AKTEST_DBINITIALIZER_H
#define AKTEST_DBINITIALIZER_H

#include "entities.h"

class DbInitializer {
public:
    ~DbInitializer();
    Akonadi::Server::Resource createResource(const char *name);
    Akonadi::Server::Collection createCollection(const char *name, const Akonadi::Server::Collection &parent = Akonadi::Server::Collection());
    QByteArray toByteArray(bool enabled);
    QByteArray toByteArray(Akonadi::Server::Tristate tristate);
    QByteArray listResponse(const Akonadi::Server::Collection &col);
    Akonadi::Server::Collection collection(const char *name);

    void cleanup();

private:
    Akonadi::Server::Resource mResource;
};

#endif
