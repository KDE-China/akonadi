/*
 * Copyright (C) 2010 Tobias Koenig <tokoe@kde.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#ifndef UTILS_H
#define UTILS_H

#include <QtCore/QVariant>
#include <QtCore/QVector>
#include <QtCore/QDateTime>
#include <QtCore/QSet>

namespace Akonadi {
namespace Server {
namespace Utils {

/**
 * Converts a QVariant to a QString depending on its internal type.
 */
static inline QString variantToString(const QVariant &variant)
{
    if (variant.type() == QVariant::String) {
        return variant.toString();
    } else if (variant.type() == QVariant::ByteArray) {
        return QString::fromUtf8(variant.toByteArray());
    } else {
        qWarning("Unable to convert variant of type %s to QString", variant.typeName());
        Q_ASSERT(false);
        return QString();
    }
}

/**
 * Converts a QVariant to a QByteArray depending on its internal type.
 */
static inline QByteArray variantToByteArray(const QVariant &variant)
{
    if (variant.type() == QVariant::String) {
        return variant.toString().toUtf8();
    } else if (variant.type() == QVariant::ByteArray) {
        return variant.toByteArray();
    } else {
        qWarning("Unable to convert variant of type %s to QByteArray", variant.typeName());
        Q_ASSERT(false);
        return QByteArray();
    }
}

static inline QDateTime variantToDateTime(const QVariant &variant)
{
    if (variant.canConvert(QVariant::DateTime)) {
        // the dt is stored as QString, which is serialized to Qt::LocalTime,
        // even though it's UTC (but the information about TZ is not stored in DB)
        QDateTime dt = variant.toDateTime();
        dt.setTimeSpec(Qt::UTC);
        return dt;
    } else {
        qWarning("Unable to convert variant of type %s to QDateTime", variant.typeName());
        Q_ASSERT(false);
        return QDateTime();
    }
}

template<typename T>
static inline QSet<T> vectorToSet(const QVector<T> &v)
{
    QSet<T> set;
    set.reserve(v.size());
    for (const T &t : v) {
        set.insert(t);
    }
    return set;
}

/**
 * Returns the socket @p directory that is passed to this method or the one
 * the user has overwritten via the config file.
 */
QString preferredSocketDirectory(const QString &directory);

/**
 * Returns name of filesystem that @p directory is stored on. This
 * only works on Linux and returns empty string on other platforms or when it's
 * unable to detect the filesystem.
 */
QString getDirectoryFileSystem(const QString &directory);

/**
 * Disables filesystem copy-on-write feature on given file or directory.
 * Only works on Linux and does nothing on other platforms.
 *
 * It was tested only with Btrfs but in theory can be called on any FS that
 * supports NOCOW.
 */
void disableCoW(const QString &path);

} // namespace Utils
} // namespace Server
} // namespace Akonadi

#endif