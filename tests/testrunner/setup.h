/*
 * Copyright (c) 2008  Igor Trindade Oliveira <igor_trindade@yahoo.com.br>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef SETUP_H
#define SETUP_H

#include <akonadi/servermanager.h>

#include <QtCore/QMap>
#include <QtCore/QObject>
#include <QStringList>
#include <QDBusConnection>

class QIODevice;
class KProcess;
class QSignalMapper;

class SetupTest : public QObject
{
  Q_OBJECT
  Q_CLASSINFO( "D-Bus Interface", "org.kde.Akonadi.Testrunner" )

  public:
    SetupTest();
    ~SetupTest();

    /**
      Sets the instance identifier for the Akonadi session.
      Call this before using any other Akonadi API!
    */
    void setupInstanceId();
    bool startAkonadiDaemon();
    void stopAkonadiDaemon();
    QString basePath() const;

    /// Identifier used for the Akonadi session
    QString instanceId() const;

  public Q_SLOTS:
    Q_SCRIPTABLE void shutdown();
    Q_SCRIPTABLE void shutdownHarder();
    /** Synchronously restarts the server. */
    Q_SCRIPTABLE void restartAkonadiServer();
    Q_SCRIPTABLE void trackAkonadiProcess( bool track );

  Q_SIGNALS:
    void setupDone();
    void serverExited(int exitCode);

  private Q_SLOTS:
    void serverStateChanged( Akonadi::ServerManager::State state );
    void dbusNameOwnerChanged( const QString &name, const QString &oldOwner, const QString &newOwner );
    void resourceSynchronized( const QString &agentId );
    void slotAkonadiDaemonProcessFinished( int exitCode );

  private:
    bool clearEnvironment();
    QMap<QString, QString> environment() const;
    void setupAgents();
    void copyDirectory( const QString &src, const QString &dst );
    void createTempEnvironment();
    void deleteDirectory( const QString &dirName );
    void cleanTempEnvironment();

  private slots:
    void synchronizeResources();

  private:
    KProcess *mAkonadiDaemonProcess;
    QStringList mPendingAgents;
    QStringList mPendingResources;
    QStringList mPendingSyncs;
    bool mShuttingDown;
    QSignalMapper *mSyncMapper;
    bool mAgentsCreated;
    bool mTrackAkonadiProcess;
};

#endif
