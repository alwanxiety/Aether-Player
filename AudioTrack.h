#pragma once
#include <QString>

struct AudioTrack {
    QString path;
    QString title;
    QString artist;
    QString album;
    qint64 durationMs = 0;
    QString durationStr = "0:00";
    QString lyrics;
    bool isFav = false;
    bool hasCover = false;

    bool operator==(const AudioTrack& other) const {
        return path == other.path;
    }
};