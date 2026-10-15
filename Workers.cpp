#include "Workers.h"
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QtConcurrent>
#include <QStandardPaths>
#include <algorithm>
#include <cstring>

LyricsFetcherWorker::LyricsFetcherWorker(const QString& path, const QString& title, const QString& artist, int durationSec, QObject* parent)
    : QObject(parent), m_path(path), m_title(title), m_artist(artist), m_durationSec(durationSec) {
    m_nam.setTransferTimeout(10000);
}

QString LyricsFetcherWorker::cleanQuery(const QString& text) {
    QString q = text;
    static const QRegularExpression reUrls(R"(https?://\S+|www\.\S+)");
    static const QRegularExpression reBrackets(R"(\[[^\]]*\])");
    static const QRegularExpression reFeatParen(R"(\((feat|ft|prod)\.?[^\)]*\))", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reFeatWord(R"(\b(feat|ft|prod)\b.*)", QRegularExpression::CaseInsensitiveOption);

    q.remove(reUrls);
    q.remove(reBrackets);
    q.remove(reFeatParen);
    q.remove(reFeatWord);
    return q.trimmed();
}

void LyricsFetcherWorker::start() {
    QString cTitle = cleanQuery(m_title);
    QString cArtist = cleanQuery(m_artist);

    if (cArtist == "Server Library" || cArtist == "Неизвестный исполнитель" || cArtist == "Cloud Stream") {
        cArtist = "";
    }

    if (cArtist.isEmpty() && cTitle.contains('-')) {
        const auto parts = cTitle.split('-');
        if (parts.size() >= 2) {
            cArtist = cleanQuery(parts[0]);
            cTitle = cleanQuery(parts[1]);
        }
    }

    if (cTitle.isEmpty()) {
        emit lyricsFound(m_path, "");
        return;
    }

    QUrl url("https://lrclib.net/api/get");
    QUrlQuery query;
    query.addQueryItem("track_name", cTitle);
    if (!cArtist.isEmpty()) {
        query.addQueryItem("artist_name", cArtist);
    }
    if (m_durationSec > 0) {
        query.addQueryItem("duration", QString::number(m_durationSec));
    }
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "AetherPlayer/22.0 (Windows NT 10.0; Win64; x64)");

    auto* reply = m_nam.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, cTitle, cArtist]() {
        QString syncedLyrics;
        QString plainLyrics;

        if (reply->error() == QNetworkReply::NoError) {
            const auto doc = QJsonDocument::fromJson(reply->readAll());
            if (doc.isObject()) {
                const auto obj = doc.object();
                syncedLyrics = obj.value("syncedLyrics").toString();
                plainLyrics = obj.value("plainLyrics").toString();
            }
        }
        reply->deleteLater();

        if (!syncedLyrics.isEmpty()) {
            emit lyricsFound(m_path, syncedLyrics);
            return;
        }

        const QString searchQuery = cArtist.isEmpty() ? cTitle : (cArtist + " " + cTitle);
        QUrl searchUrl("https://lrclib.net/api/search");
        QUrlQuery sQuery;
        sQuery.addQueryItem("q", searchQuery);
        searchUrl.setQuery(sQuery);

        QNetworkRequest sRequest(searchUrl);
        sRequest.setHeader(QNetworkRequest::UserAgentHeader, "AetherPlayer/22.0");

        auto* sReply = m_nam.get(sRequest);
        connect(sReply, &QNetworkReply::finished, this, [this, sReply, plainLyrics, searchQuery]() {
            QString fallbackSynced;
            QString fallbackPlain = plainLyrics;
            if (sReply->error() == QNetworkReply::NoError) {
                const auto doc = QJsonDocument::fromJson(sReply->readAll());
                if (doc.isArray()) {
                    for (const auto& item : doc.array()) {
                        const auto obj = item.toObject();
                        const QString s = obj.value("syncedLyrics").toString();
                        if (!s.isEmpty()) {
                            fallbackSynced = s;
                            break;
                        }
                        if (fallbackPlain.isEmpty()) {
                            fallbackPlain = obj.value("plainLyrics").toString();
                        }
                    }
                }
            }
            sReply->deleteLater();

            if (!fallbackSynced.isEmpty()) {
                emit lyricsFound(m_path, fallbackSynced);
            } else if (!fallbackPlain.isEmpty()) {
                emit lyricsFound(m_path, fallbackPlain);
            } else {
                fetchFromGenius(searchQuery);
            }
        });
    });
}

void LyricsFetcherWorker::fetchFromGenius(const QString& queryText) {
    QUrl url("https://genius.com/api/search/song");
    QUrlQuery query;
    query.addQueryItem("q", queryText);
    query.addQueryItem("page", "1");
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");

    auto* reply = m_nam.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        QString songUrl;
        if (reply->error() == QNetworkReply::NoError) {
            const auto doc = QJsonDocument::fromJson(reply->readAll());
            const auto sections = doc.object().value("response").toObject().value("sections").toArray();
            for (const auto& secVal : sections) {
                const auto hits = secVal.toObject().value("hits").toArray();
                if (!hits.isEmpty()) {
                    songUrl = hits.first().toObject().value("result").toObject().value("url").toString();
                    break;
                }
            }
        }
        reply->deleteLater();

        if (songUrl.isEmpty()) {
            emit lyricsFound(m_path, "");
            return;
        }

        QNetworkRequest pageReq((QUrl(songUrl)));
        pageReq.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");

        auto* pageReply = m_nam.get(pageReq);
        connect(pageReply, &QNetworkReply::finished, this, [this, pageReply]() {
            QString lyrics;
            if (pageReply->error() == QNetworkReply::NoError) {
                const QString html = QString::fromUtf8(pageReply->readAll());
                static const QRegularExpression reContainer(
                    R"(<div[^>]*data-lyrics-container="true"[^>]*>(.*?)</div>)",
                    QRegularExpression::DotMatchesEverythingOption
                );
                static const QRegularExpression reBr(R"(<br\s*/?>)");
                static const QRegularExpression reTags(R"(<.*?>)");

                auto matchIterator = reContainer.globalMatch(html);
                QString rawLyricsHtml;
                while (matchIterator.hasNext()) {
                    rawLyricsHtml += matchIterator.next().captured(1) + "\n";
                }

                if (!rawLyricsHtml.isEmpty()) {
                    rawLyricsHtml.replace(reBr, "\n");
                    rawLyricsHtml.replace(reTags, "");
                    rawLyricsHtml.replace("&amp;", "&");
                    rawLyricsHtml.replace("&quot;", "\"");
                    rawLyricsHtml.replace("&#x27;", "'");
                    lyrics = rawLyricsHtml.trimmed();
                }
            }
            pageReply->deleteLater();
            emit lyricsFound(m_path, lyrics);
        });
    });
}

static QString decodeRawStringWithEncoding(const uchar* rawBytes, int len, quint8 enc) {
    if (len <= 0 || !rawBytes) return {};

    if (enc == 0) {
        QString utf8 = QString::fromUtf8(reinterpret_cast<const char*>(rawBytes), len).trimmed();
        return utf8.contains(QChar(0xFFFD)) ? QString::fromLatin1(reinterpret_cast<const char*>(rawBytes), len).trimmed() : utf8;
    }

    if (enc == 1 || enc == 2) {
        if (len < 2) return {};
        const uchar* p = rawBytes;
        int l = len;

        bool isBE = (enc == 2);
        if (l >= 2) {
            if (p[0] == 0xFF && p[1] == 0xFE) {
                isBE = false;
                p += 2;
                l -= 2;
            } else if (p[0] == 0xFE && p[1] == 0xFF) {
                isBE = true;
                p += 2;
                l -= 2;
            }
        }

        const int charCount = l / 2;
        if (charCount <= 0) return {};

        QString result;
        result.resize(charCount);
        for (int i = 0; i < charCount; ++i) {
            const ushort ch = isBE ? static_cast<ushort>((p[i * 2] << 8) | p[i * 2 + 1])
                                   : static_cast<ushort>(p[i * 2] | (p[i * 2 + 1] << 8));
            result[i] = QChar(ch);
        }
        result.remove(QChar(0x0000));
        return result.trimmed();
    }

    QString res = QString::fromUtf8(reinterpret_cast<const char*>(rawBytes), len);
    res.remove(QChar(0x0000));
    return res.trimmed();
}

static QString decodeID3StringRaw(const uchar* data, int size) {
    if (size <= 1 || !data) return {};
    const quint8 enc = data[0];
    return decodeRawStringWithEncoding(data + 1, size - 1, enc);
}

static QString decodeID3LyricsRaw(const uchar* payload, int size) {
    if (size < 5 || !payload) return {};
    const quint8 enc = payload[0];
    int offset = 4;

    if (enc == 1 || enc == 2) {
        while (offset + 1 < size) {
            if (payload[offset] == 0 && payload[offset + 1] == 0) {
                offset += 2;
                break;
            }
            offset += 2;
        }
    } else {
        while (offset < size) {
            if (payload[offset] == 0) {
                offset += 1;
                break;
            }
            ++offset;
        }
    }

    if (offset >= size) return {};
    return decodeRawStringWithEncoding(payload + offset, size - offset, enc);
}

static QByteArray extractImageDirectRaw(const uchar* data, qint64 size) {
    if (size < 16 || !data) return {};
    const qint64 searchLimit = std::min<qint64>(size, 16 * 1024 * 1024);

    for (qint64 i = 0; i < searchLimit - 4; ++i) {
        if (data[i] == 0xFF && data[i + 1] == 0xD8 && data[i + 2] == 0xFF &&
            (data[i + 3] == 0xE0 || data[i + 3] == 0xE1 || data[i + 3] == 0xDB || data[i + 3] == 0xEE)) {
            const qint64 maxEnd = std::min<qint64>(size - 1, i + 12 * 1024 * 1024);
            for (qint64 j = i + 4; j < maxEnd; ++j) {
                if (data[j] == 0xFF && data[j + 1] == 0xD9) {
                    return QByteArray(reinterpret_cast<const char*>(data + i), static_cast<int>(j - i + 2));
                }
            }
        }
    }

    static const uchar pngSig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    for (qint64 i = 0; i < searchLimit - 8; ++i) {
        if (std::memcmp(data + i, pngSig, 8) == 0) {
            const qint64 maxEnd = std::min<qint64>(size - 4, i + 12 * 1024 * 1024);
            for (qint64 j = i + 8; j < maxEnd; ++j) {
                if (std::memcmp(data + j, "IEND", 4) == 0) {
                    const qint64 totalLen = std::min<qint64>(size - i, j - i + 8);
                    return QByteArray(reinterpret_cast<const char*>(data + i), static_cast<int>(totalLen));
                }
            }
        }
    }
    return {};
}

static quint32 readU32BE(const uchar* p) {
    return (static_cast<quint32>(p[0]) << 24) |
           (static_cast<quint32>(p[1]) << 16) |
           (static_cast<quint32>(p[2]) << 8)  |
           static_cast<quint32>(p[3]);
}

static quint32 readU32LE(const uchar* p) {
    return static_cast<quint32>(p[0]) |
           (static_cast<quint32>(p[1]) << 8)  |
           (static_cast<quint32>(p[2]) << 16) |
           (static_cast<quint32>(p[3]) << 24);
}

static qint64 estimateAudioDurationMapped(const uchar* ptr, qint64 fileSize, int tagSize) {
    if (fileSize <= 0 || !ptr) return 0;

    if (fileSize >= 44 && std::memcmp(ptr, "RIFF", 4) == 0 && std::memcmp(ptr + 8, "WAVE", 4) == 0) {
        quint32 byteRate = 0;
        qint64 dataBytes = fileSize - 44;
        qint64 offset = 12;
        while (offset + 8 <= fileSize) {
            const quint32 chunkSize = readU32LE(ptr + offset + 4);
            if (std::memcmp(ptr + offset, "fmt ", 4) == 0 && offset + 8 + 12 <= fileSize) {
                byteRate = readU32LE(ptr + offset + 8 + 8);
            } else if (std::memcmp(ptr + offset, "data", 4) == 0) {
                dataBytes = chunkSize;
                break;
            }
            offset += 8 + chunkSize + (chunkSize & 1);
        }
        if (byteRate > 0) return (dataBytes * 1000) / byteRate;
    }

    if (fileSize >= 26 && std::memcmp(ptr, "fLaC", 4) == 0) {
        const uchar* p = ptr + 4;
        if ((p[0] & 0x7F) == 0) {
            const quint32 sampleRate = ((p[14] << 12) | (p[15] << 4) | (p[16] >> 4));
            const quint64 totalSamples = ((static_cast<quint64>(p[17] & 0x0F) << 32) |
                                          (static_cast<quint64>(p[18]) << 24) |
                                          (static_cast<quint64>(p[19]) << 16) |
                                          (static_cast<quint64>(p[20]) << 8)  |
                                          static_cast<quint64>(p[21]));
            if (sampleRate > 0) return static_cast<qint64>((totalSamples * 1000) / sampleRate);
        }
    }

    const qint64 startOffset = (tagSize > 0) ? (10 + tagSize) : 0;
    const qint64 audioBytes = fileSize - startOffset;
    if (audioBytes > 4 && startOffset < fileSize - 4) {
        const qint64 scanLimit = std::min<qint64>(fileSize - 4, startOffset + 8192);
        for (qint64 i = startOffset; i < scanLimit; ++i) {
            if (ptr[i] == 0xFF && (ptr[i + 1] & 0xE0) == 0xE0) {
                const int versionBits = (ptr[i + 1] >> 3) & 0x03;
                const int layerBits = (ptr[i + 1] >> 1) & 0x03;
                const int bitrateIdx = (ptr[i + 2] >> 4) & 0x0F;
                const int srIdx = (ptr[i + 2] >> 2) & 0x03;

                if (versionBits == 1 || layerBits == 0 || bitrateIdx == 0 || bitrateIdx == 15 || srIdx == 3) {
                    continue;
                }

                static const int srTable[4][3] = {
                    {11025, 12000, 8000},
                    {0, 0, 0},
                    {22050, 24000, 16000},
                    {44100, 48000, 32000}
                };
                const int sampleRate = srTable[versionBits][srIdx];
                const int samplesPerFrame = (versionBits == 3) ? 1152 : 576;

                const qint64 vbrScanEnd = std::min<qint64>(fileSize - 16, i + 256);
                for (qint64 k = i + 4; k < vbrScanEnd; ++k) {
                    if (std::memcmp(ptr + k, "Xing", 4) == 0 || std::memcmp(ptr + k, "Info", 4) == 0) {
                        const quint32 flags = readU32BE(ptr + k + 4);
                        if ((flags & 0x01) && sampleRate > 0) {
                            const quint32 totalFrames = readU32BE(ptr + k + 8);
                            return (static_cast<qint64>(totalFrames) * samplesPerFrame * 1000) / sampleRate;
                        }
                    } else if (std::memcmp(ptr + k, "VBRI", 4) == 0 && k + 18 <= fileSize && sampleRate > 0) {
                        const quint32 totalFrames = readU32BE(ptr + k + 14);
                        return (static_cast<qint64>(totalFrames) * samplesPerFrame * 1000) / sampleRate;
                    }
                }

                static const int bitratesMpeg1L3[] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};
                static const int bitratesMpeg2L3[] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0};
                const int kbps = (versionBits == 3) ? bitratesMpeg1L3[bitrateIdx] : bitratesMpeg2L3[bitrateIdx];
                if (kbps > 0) return (audioBytes * 8) / kbps;
            }
        }
        return (audioBytes * 8) / 192;
    }
    return 0;
}

TrackLoaderWorker::TrackLoaderWorker(const QStringList& paths, QObject* parent)
    : QThread(parent), m_paths(paths) {}

void TrackLoaderWorker::run() {
    const QVector<AudioTrack> results = QtConcurrent::blockingMapped(m_paths, &TrackLoaderWorker::parseFileFast);
    if (!m_cancelled.load(std::memory_order_relaxed)) {
        emit tracksLoaded(results);
    }
}

AudioTrack TrackLoaderWorker::parseFileFast(const QString& path) {
    AudioTrack track;
    track.path = path;
    const QFileInfo fi(path);
    track.title = fi.baseName();
    track.artist = "Неизвестный исполнитель";
    track.album = "Сингл";
    track.durationMs = 0;
    track.durationStr = "0:00";
    track.hasCover = false;

    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        const qint64 fSize = file.size();
        uchar* ptr = file.map(0, fSize);

        if (ptr) {
            int tagSize = 0;
            if (fSize >= 10 && std::memcmp(ptr, "ID3", 3) == 0) {
                const int ver = ptr[3];
                const quint8 flags = ptr[5];
                tagSize = ((ptr[6] & 0x7F) << 21) |
                          ((ptr[7] & 0x7F) << 14) |
                          ((ptr[8] & 0x7F) << 7)  |
                          (ptr[9] & 0x7F);

                int offset = 10;
                const int maxOffset = static_cast<int>(std::min<qint64>(fSize, 10 + tagSize));

                if ((flags & 0x40) && offset + 4 < maxOffset) {
                    const int extSize = (ver == 4)
                        ? (((ptr[offset] & 0x7F) << 21) | ((ptr[offset + 1] & 0x7F) << 14) | ((ptr[offset + 2] & 0x7F) << 7) | (ptr[offset + 3] & 0x7F))
                        : static_cast<int>(readU32BE(ptr + offset)) + 4;
                    offset += std::max(4, extSize);
                }

                while (offset + 10 < maxOffset) {
                    if (ptr[offset] == 0) break;
                    const char frameId[5] = {
                        static_cast<char>(ptr[offset]),
                        static_cast<char>(ptr[offset + 1]),
                        static_cast<char>(ptr[offset + 2]),
                        static_cast<char>(ptr[offset + 3]),
                        0
                    };

                    const quint32 rawFrameSize = (ver == 4)
                        ? (((ptr[offset + 4] & 0x7F) << 21) | ((ptr[offset + 5] & 0x7F) << 14) | ((ptr[offset + 6] & 0x7F) << 7) | (ptr[offset + 7] & 0x7F))
                        : readU32BE(ptr + offset + 4);

                    if (rawFrameSize == 0 || rawFrameSize > static_cast<quint32>(maxOffset - offset - 10)) break;
                    const int frameSize = static_cast<int>(rawFrameSize);
                    const uchar* payload = ptr + offset + 10;

                    if (std::strcmp(frameId, "TIT2") == 0) {
                        const QString t = decodeID3StringRaw(payload, frameSize);
                        if (!t.isEmpty()) track.title = t;
                    } else if (std::strcmp(frameId, "TPE1") == 0) {
                        const QString a = decodeID3StringRaw(payload, frameSize);
                        if (!a.isEmpty()) track.artist = a;
                    } else if (std::strcmp(frameId, "TALB") == 0) {
                        const QString al = decodeID3StringRaw(payload, frameSize);
                        if (!al.isEmpty()) track.album = al;
                    } else if (std::strcmp(frameId, "TLEN") == 0) {
                        track.durationMs = decodeID3StringRaw(payload, frameSize).toLongLong();
                    } else if (std::strcmp(frameId, "USLT") == 0 || std::strcmp(frameId, "SYLT") == 0) {
                        track.lyrics = decodeID3LyricsRaw(payload, frameSize);
                    } else if (std::strcmp(frameId, "APIC") == 0) {
                        track.hasCover = true;
                    }

                    offset += 10 + frameSize;
                }
            }

            if (track.durationMs <= 0) {
                track.durationMs = estimateAudioDurationMapped(ptr, fSize, tagSize);
            }

            file.unmap(ptr);
        }
        file.close();
    }

    if (track.durationMs > 0) {
        const int s = static_cast<int>((track.durationMs / 1000) % 60);
        const int m = static_cast<int>(track.durationMs / 60000);
        track.durationStr = QString("%1:%2").arg(m).arg(s, 2, 10, QChar('0'));
    }

    if (track.artist == "Неизвестный исполнитель" && track.title.contains('-')) {
        const auto parts = track.title.split('-');
        if (parts.size() >= 2) {
            track.artist = parts[0].trimmed();
            track.title = parts.mid(1).join('-').trimmed();
        }
    }
    return track;
}

QPixmap TrackLoaderWorker::loadCoverDirect(const QString& path, int /*size*/, qreal /*dpr*/) {
    QFile file(path);
    QByteArray imgData;
    if (file.open(QIODevice::ReadOnly)) {
        const qint64 fSize = file.size();
        uchar* ptr = file.map(0, fSize);
        if (ptr) {
            imgData = extractImageDirectRaw(ptr, fSize);
            file.unmap(ptr);
        }
        file.close();
    }

    if (imgData.isEmpty()) {
        const QDir dir = QFileInfo(path).dir();
        for (const auto& name : {"cover.jpg", "cover.png", "folder.jpg", "folder.png", "front.jpg", "front.png"}) {
            if (dir.exists(name)) {
                QFile f(dir.filePath(name));
                if (f.open(QIODevice::ReadOnly)) {
                    imgData = f.readAll();
                    break;
                }
            }
        }
    }

    if (!imgData.isEmpty()) {
        QPixmap pm;
        if (pm.loadFromData(imgData)) return pm;
    }
    return {};
}

VkMusicWorker::VkMusicWorker(const QString& token, QObject* parent)
    : QObject(parent), m_token(token.trimmed()) {
    m_nam.setTransferTimeout(10000);
}

void VkMusicWorker::search(const QString& query) {
    if (m_token.isEmpty()) {
        emit errorOccurred("Вставьте VK токен в настройках");
        return;
    }

    QUrl url("https://api.vk.com/method/audio.search");
    QUrlQuery q;
    q.addQueryItem("q", query);
    q.addQueryItem("count", "100");
    q.addQueryItem("access_token", m_token);
    q.addQueryItem("v", "5.131");
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "KateMobileAndroid/108 lite-arm64-v8a (Android 14; SDK 34; ru)");

    auto* reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            parseVkResponse(reply->readAll());
        } else {
            emit errorOccurred("Ошибка сети: " + reply->errorString());
        }
        reply->deleteLater();
    });
}

void VkMusicWorker::getMyTracks() {
    if (m_token.isEmpty()) {
        emit errorOccurred("Вставьте VK токен в настройках");
        return;
    }

    QUrl urlUser("https://api.vk.com/method/users.get");
    QUrlQuery qu;
    qu.addQueryItem("access_token", m_token);
    qu.addQueryItem("v", "5.131");
    urlUser.setQuery(qu);

    QNetworkRequest reqUser(urlUser);
    reqUser.setHeader(QNetworkRequest::UserAgentHeader, "KateMobileAndroid/108 lite-arm64-v8a (Android 14; SDK 34; ru)");

    auto* replyUser = m_nam.get(reqUser);
    connect(replyUser, &QNetworkReply::finished, this, [this, replyUser]() {
        QString ownerId;
        if (replyUser->error() == QNetworkReply::NoError) {
            const auto doc = QJsonDocument::fromJson(replyUser->readAll());
            const auto respArr = doc.object().value("response").toArray();
            if (!respArr.isEmpty()) {
                ownerId = QString::number(respArr.first().toObject().value("id").toInteger());
            }
        }
        replyUser->deleteLater();

        QUrl url("https://api.vk.com/method/audio.get");
        QUrlQuery q;
        if (!ownerId.isEmpty()) q.addQueryItem("owner_id", ownerId);
        q.addQueryItem("count", "200");
        q.addQueryItem("access_token", m_token);
        q.addQueryItem("v", "5.131");
        url.setQuery(q);

        QNetworkRequest req(url);
        req.setHeader(QNetworkRequest::UserAgentHeader, "KateMobileAndroid/108 lite-arm64-v8a (Android 14; SDK 34; ru)");

        auto* reply = m_nam.get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            if (reply->error() == QNetworkReply::NoError) {
                parseVkResponse(reply->readAll());
            } else {
                emit errorOccurred("Ошибка сети VK: " + reply->errorString());
            }
            reply->deleteLater();
        });
    });
}

void VkMusicWorker::parseVkResponse(const QByteArray& json) {
    QVector<AudioTrack> tracks;
    const auto doc = QJsonDocument::fromJson(json);
    const auto root = doc.object();

    if (root.contains("error")) {
        emit errorOccurred(root.value("error").toObject().value("error_msg").toString());
        return;
    }

    const auto items = root.value("response").toObject().value("items").toArray();
    for (const auto& itemVal : items) {
        const auto obj = itemVal.toObject();
        const QString streamUrl = obj.value("url").toString();
        if (streamUrl.isEmpty()) continue;

        AudioTrack tr;
        tr.path = streamUrl;
        tr.title = obj.value("title").toString();
        tr.artist = obj.value("artist").toString();
        tr.durationMs = obj.value("duration").toInt() * 1000;

        const int s = static_cast<int>((tr.durationMs / 1000) % 60);
        const int m = static_cast<int>(tr.durationMs / 60000);
        tr.durationStr = QString("%1:%2").arg(m).arg(s, 2, 10, QChar('0'));
        tr.album = "VK Music";
        tr.hasCover = false;

        tracks.append(tr);
    }

    if (tracks.isEmpty()) emit errorOccurred("Аудиозаписи не найдены");
    else emit vkTracksLoaded(tracks);
}

ServerMusicWorker::ServerMusicWorker(const QString& serverHost, QObject* parent)
    : QObject(parent), m_serverHost(serverHost) {
    m_nam.setTransferTimeout(10000);
}

void ServerMusicWorker::search(const QString& query) {
    QUrl url(m_serverHost + "/search");
    QUrlQuery q;
    q.addQueryItem("q", query);
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "AetherPlayer/ServerClient");

    auto* reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QVector<AudioTrack> tracks;
            const auto doc = QJsonDocument::fromJson(reply->readAll());
            const auto arr = doc.object().value("results").toArray();

            for (const auto& item : arr) {
                const QString filename = item.toString();
                AudioTrack tr;
                tr.path = m_serverHost + "/track/" + QUrl::toPercentEncoding(filename);

                const QString rawName = filename.section('.', 0, -2);
                tr.title = rawName;
                tr.artist = "Server Library";
                tr.album = "Cloud Stream";
                tr.durationStr = "Загрузка...";
                tr.durationMs = 0;
                tr.hasCover = false;

                if (rawName.contains('-')) {
                    const auto parts = rawName.split('-');
                    if (parts.size() >= 2) {
                        tr.artist = parts[0].trimmed();
                        tr.title = parts.mid(1).join('-').trimmed();
                    }
                }
                tracks.append(tr);
            }
            emit serverTracksLoaded(tracks);
        } else {
            emit errorOccurred("Ошибка сервера: " + reply->errorString());
        }
        reply->deleteLater();
    });
}

StreamDownloadWorker::StreamDownloadWorker(const QString& urlStr, QObject* parent)
    : QObject(parent), m_url(urlStr) {
    m_nam.setTransferTimeout(20000);
}

void StreamDownloadWorker::start() {
    const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/AetherCache";
    QDir().mkpath(cacheDir);

    const QUrl parsedUrl(m_url);
    const QByteArray urlHash = QCryptographicHash::hash(m_url.toUtf8(), QCryptographicHash::Sha256).toHex().left(16);
    QString ext = QFileInfo(parsedUrl.path()).suffix().toLower();
    if (ext != "mp3" && ext != "flac" && ext != "wav") {
        ext = "mp3";
    }
    const QString localPath = QString("%1/stream_%2.%3").arg(cacheDir, QString::fromLatin1(urlHash), ext);

    if (QFile::exists(localPath) && QFileInfo(localPath).size() > 4096) {
        emit downloaded(localPath);
        return;
    }

    QNetworkRequest req(parsedUrl);
    req.setHeader(QNetworkRequest::UserAgentHeader, "AetherPlayer/22.0");

    auto* reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, localPath]() {
        if (reply->error() == QNetworkReply::NoError) {
            const QByteArray data = reply->readAll();
            if (data.size() > 1024) {
                QFile file(localPath);
                if (file.open(QIODevice::WriteOnly)) {
                    file.write(data);
                    file.flush();
                    file.close();
                    emit downloaded(localPath);
                } else {
                    emit errorOccurred("Ошибка записи кэша");
                }
            } else {
                emit errorOccurred("Файл на сервере пуст");
            }
        } else {
            emit errorOccurred("Ошибка сети: " + reply->errorString());
        }
        reply->deleteLater();
    });
}