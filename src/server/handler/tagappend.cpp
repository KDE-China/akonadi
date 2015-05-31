/*
    Copyright (c) 2014 Daniel Vrátil <dvratil@redhat.com>

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

#include "tagappend.h"

#include "tagfetchhelper.h"
#include "connection.h"
#include "storage/datastore.h"
#include "storage/querybuilder.h"
#include "storage/countquerybuilder.h"

#include <private/scope_p.h>
#include <private/imapset_p.h>

using namespace Akonadi;
using namespace Akonadi::Server;

bool TagAppend::parseStream()
{
    Protocol::CreateTagCommand cmd;
    mInStream >> cmd;

    if (!cmd.remoteId().isEmpty() && !connection()->context()->resource().isValid()) {
        return failureResponse("Only resources can create tags with remote ID");
    }

    TagType tagType;
    if (!cmd.type().isEmpty()) {
        tagType = TagType::retrieveByName(cmd.type());
        if (!tagType.isValid()) {
            TagType t(cmd.type());
            if (!t.insert()) {
                return failureResponse(QStringLiteral("Unable to create tagtype '") % cmd.type() % QStringLiteral("'"));
            }
            tagType = t;
        }
    }

    qint64 tagId = -1;
    if (cmd.merge()) {
        QueryBuilder qb(Tag::tableName());
        qb.addColumn(Tag::idColumn());
        qb.addValueCondition(Tag::gidColumn(), Query::Equals, cmd.gid());
        if (!qb.exec()) {
            return failureResponse("Unable to list tags");
        }
        if (qb.query().next()) {
            tagId = qb.query().value(0).toLongLong();
        }
    }
    if (tagId < 0) {
        Tag insertedTag;
        insertedTag.setGid(cmd.gid());
        if (cmd.parentId() >= 0) {
            insertedTag.setParentId(cmd.parentId());
        }
        if (tagType.isValid()) {
            insertedTag.setTypeId(tagType.id());
        }
        if (!insertedTag.insert(&tagId)) {
            return failureResponse("Failed to store tag");
        }

        const Protocol::Attributes attrs = cmd.attributes();
        for (auto iter = attrs.cbegin(), end = attrs.cend(); iter != end; ++iter) {
            TagAttribute attribute;
            attribute.setTagId(tagId);
            attribute.setType(iter.key());
            attribute.setValue(iter.value());
            if (!attribute.insert()) {
                return failureResponse("Failed to store tag attribute");
            }
        }

        DataStore::self()->notificationCollector()->tagAdded(insertedTag);
    }

    if (!cmd.remoteId().isEmpty()) {
        const qint64 resourceId = connection()->context()->resource().id();

        CountQueryBuilder qb(TagRemoteIdResourceRelation::tableName());
        qb.addValueCondition(TagRemoteIdResourceRelation::tagIdColumn(), Query::Equals, tagId);
        qb.addValueCondition(TagRemoteIdResourceRelation::resourceIdColumn(), Query::Equals, resourceId);
        if (!qb.exec()) {
            return failureResponse("Failed to query for existing TagRemoteIdResourceRelation entries");
        }
        const bool exists = (qb.result() > 0);

        //If the relation is already existing simply update it (can happen if a resource simply creates the tag again while enabling merge)
        bool ret = false;
        if (exists) {
            //Simply using update() doesn't work since TagRemoteIdResourceRelation only takes the tagId for identification of the column
            QueryBuilder qb(TagRemoteIdResourceRelation::tableName(), QueryBuilder::Update);
            qb.addValueCondition(TagRemoteIdResourceRelation::tagIdColumn(), Query::Equals, tagId);
            qb.addValueCondition(TagRemoteIdResourceRelation::resourceIdColumn(), Query::Equals, resourceId);
            qb.setColumnValue(TagRemoteIdResourceRelation::remoteIdColumn(), cmd.remoteId());
            ret = qb.exec();
        } else {
            TagRemoteIdResourceRelation rel;
            rel.setTagId(tagId);
            rel.setResourceId(resourceId);
            rel.setRemoteId(cmd.remoteId());
            ret = rel.insert();
        }
        if (!ret) {
            return failureResponse("Failed to store tag remote ID");
        }
    }

    // FIXME BIN
    Scope scope;
    ImapSet set;
    set.add(QVector<qint64>() << tagId);
    scope.setUidSet(set);
    TagFetchHelper helper(connection(), scope);
    if (!helper.fetchTags()) {
        return failureResponse("Failed to fetch the new tag");
    }

    return successResponse<Protocol::CreateTagResponse>();
}
