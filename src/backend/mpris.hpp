#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QDBusAbstractAdaptor>
#include <QDBusObjectPath>

class MprisRootAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    
    Q_PROPERTY(bool CanQuit READ canQuit)
    Q_PROPERTY(bool Fullscreen READ fullscreen WRITE setFullscreen)
    Q_PROPERTY(bool CanSetFullscreen READ canSetFullscreen)
    Q_PROPERTY(bool CanRaise READ canRaise)
    Q_PROPERTY(bool HasTrackList READ hasTrackList)
    Q_PROPERTY(QString Identity READ identity)
    Q_PROPERTY(QString DesktopEntry READ desktopEntry)
    Q_PROPERTY(QStringList SupportedUriSchemes READ supportedUriSchemes)
    Q_PROPERTY(QStringList SupportedMimeTypes READ supportedMimeTypes)

public:
    explicit MprisRootAdaptor(QObject *parent);

    bool canQuit() const { return true; }
    bool fullscreen() const { return false; }
    void setFullscreen(bool) {}
    bool canSetFullscreen() const { return false; }
    bool canRaise() const { return true; }
    bool hasTrackList() const { return false; } 
    QString identity() const { return "Qobuz"; }
    QString desktopEntry() const { return "qobuz-qt"; }
    QStringList supportedUriSchemes() const { return {}; }
    QStringList supportedMimeTypes() const { return {}; }

public slots:
    void Quit() { emit quitRequested(); }
    void Raise() { emit raiseRequested(); }

signals:
    void quitRequested();
    void raiseRequested();
};

class MprisPlayerAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")

    Q_PROPERTY(QString PlaybackStatus READ playbackStatus NOTIFY playbackStatusChanged)
    Q_PROPERTY(QString LoopStatus READ loopStatus WRITE setLoopStatus)
    Q_PROPERTY(double Rate READ rate WRITE setRate)
    Q_PROPERTY(bool Shuffle READ shuffle WRITE setShuffle)
    Q_PROPERTY(QVariantMap Metadata READ metadata NOTIFY metadataChanged)
    Q_PROPERTY(double Volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(double MinimumRate READ minimumRate)
    Q_PROPERTY(double MaximumRate READ maximumRate)
    Q_PROPERTY(bool CanGoNext READ canGoNext)
    Q_PROPERTY(bool CanGoPrevious READ canGoPrevious)
    Q_PROPERTY(bool CanPlay READ canPlay)
    Q_PROPERTY(bool CanPause READ canPause)
    Q_PROPERTY(bool CanSeek READ canSeek)
    Q_PROPERTY(bool CanControl READ canControl)

public:
    explicit MprisPlayerAdaptor(QObject *parent);

    QString playbackStatus() const { return m_playbackStatus; }
    void setPlaybackStatus(const QString &s) {
        if (m_playbackStatus != s) {
            m_playbackStatus = s;
            emit playbackStatusChanged();
        }
    }

    QString loopStatus() const { return "None"; }
    void setLoopStatus(const QString &) {}

    double rate() const { return 1.0; }
    void setRate(double) {}

    bool shuffle() const { return false; }
    void setShuffle(bool) {}

    QVariantMap metadata() const { return m_metadata; }
    void setMetadata(const QVariantMap &m) { 
        m_metadata = m; 
        emit metadataChanged(); 
    }

    double volume() const { return m_volume; }
    void setVolume(double v) { 
        emit volumeChangeRequested(v); 
    }
    void updateVolume(double v) {
        if (m_volume != v) {
            m_volume = v;
            emit volumeChanged();
        }
    }

    qlonglong position() const { return m_positionMicro; }
    void updatePosition(qlonglong posSecs) { m_positionMicro = posSecs * 1000000LL; }

    double minimumRate() const { return 1.0; }
    double maximumRate() const { return 1.0; }

    bool canGoNext() const { return true; }
    bool canGoPrevious() const { return true; }
    bool canPlay() const { return true; }
    bool canPause() const { return true; }
    bool canSeek() const { return true; }
    bool canControl() const { return true; }

public slots:
    void Next() { emit nextRequested(); }
    void Previous() { emit previousRequested(); }
    void Pause() { emit pauseRequested(); }
    void PlayPause() { emit playPauseRequested(); }
    void Stop() { emit stopRequested(); }
    void Play() { emit playRequested(); }
    void Seek(qlonglong offset) { emit seekRequested(offset); }
    void SetPosition(const QDBusObjectPath &trackId, qlonglong position) { Q_UNUSED(trackId); emit seekToRequested(position); }
    void OpenUri(const QString &uri) { Q_UNUSED(uri); }

signals:
    void playbackStatusChanged();
    void metadataChanged();
    void volumeChanged();

    // Commands to MainWindow
    void playRequested();
    void pauseRequested();
    void playPauseRequested();
    void stopRequested();
    void nextRequested();
    void previousRequested();
    void seekRequested(qlonglong offsetMicroseconds);
    void seekToRequested(qlonglong positionMicroseconds);
    void volumeChangeRequested(double volume);

private:
    QString m_playbackStatus = "Stopped";
    QVariantMap m_metadata;
    double m_volume = 1.0;
    qlonglong m_positionMicro = 0;
};

class Mpris : public QObject
{
    Q_OBJECT
public:
    explicit Mpris(QObject *parent = nullptr);

    MprisRootAdaptor *root() const { return m_root; }
    MprisPlayerAdaptor *player() const { return m_player; }

private:
    MprisRootAdaptor *m_root;
    MprisPlayerAdaptor *m_player;
};
