#pragma once
#include <QObject>
#include <QThread>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPixmap>
#include <QStringList>
#include <QVector>
#include <atomic>
#include "AudioTrack.h"

class LyricsFetcherWorker : public QObject {
    Q_OBJECT
public:
    LyricsFetcherWorker(const QString& path, const QString& title, const QString& artist, int durationSec = 0, QObject* parent = nullptr);
    void start();

signals:
    void lyricsFound(const QString& trackPath, const QString& lyrics);

private:
    static QString cleanQuery(const QString& text);
    void fetchFromGenius(const QString& queryText);

    QString m_path;
    QString m_title;
    QString m_artist;
    int m_durationSec = 0;
    QNetworkAccessManager m_nam;
};

class TrackLoaderWorker : public QThread {
    Q_OBJECT
public:
    explicit TrackLoaderWorker(const QStringList& paths, QObject* parent = nullptr);
    void cancel() { m_cancelled.store(true, std::memory_order_relaxed); }

    static AudioTrack parseFileFast(const QString& path);
    static QPixmap loadCoverDirect(const QString& path, int size = 500, qreal dpr = 1.0);

signals:
    void tracksLoaded(const QVector<AudioTrack>& tracks);

protected:
    void run() override;

private:
    QStringList m_paths;
    std::atomic<bool> m_cancelled{false};
};

class VkMusicWorker : public QObject {
    Q_OBJECT
public:
    explicit VkMusicWorker(const QString& token, QObject* parent = nullptr);
    void search(const QString& query);
    void getMyTracks();

signals:
    void vkTracksLoaded(const QVector<AudioTrack>& tracks);
    void errorOccurred(const QString& errorMsg);

private:
    void parseVkResponse(const QByteArray& json);

    QString m_token;
    QNetworkAccessManager m_nam;
};

class ServerMusicWorker : public QObject {
    Q_OBJECT
public:
    explicit ServerMusicWorker(const QString& serverHost = "http://87.251.66.219:8080", QObject* parent = nullptr);
    void search(const QString& query);

signals:
    void serverTracksLoaded(const QVector<AudioTrack>& tracks);
    void errorOccurred(const QString& errorMsg);

private:
    QString m_serverHost;
    QNetworkAccessManager m_nam;
};

class StreamDownloadWorker : public QObject {
    Q_OBJECT
public:
    explicit StreamDownloadWorker(const QString& urlStr, QObject* parent = nullptr);
    void start();

signals:
    void downloaded(const QString& localPath);
    void errorOccurred(const QString& err);

private:
    QString m_url;
    QNetworkAccessManager m_nam;
};