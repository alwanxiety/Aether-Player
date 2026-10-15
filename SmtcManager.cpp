#include "SmtcManager.h"

SmtcManager::SmtcManager(QWidget* parentWindow, QObject* parent)
    : QObject(parent), m_window(parentWindow) {}

SmtcManager::~SmtcManager() = default;

bool SmtcManager::init() {
    return true;
}

void SmtcManager::updateMetadata(const QString& title, const QString& artist, const QString& album, const QPixmap& cover) {
    Q_UNUSED(album);
    Q_UNUSED(cover);

    if (m_window) {
        if (!title.isEmpty() && title != "Не играет") {
            m_window->setWindowTitle(QString("%1 — %2 | Aether").arg(artist.isEmpty() ? "Трек" : artist, title));
        } else {
            m_window->setWindowTitle("Aether Audio");
        }
    }
}

void SmtcManager::updatePlaybackState(bool isPlaying) {
    Q_UNUSED(isPlaying);
}

void SmtcManager::updateTimeline(qint64 posMs, qint64 durMs) {
    Q_UNUSED(posMs);
    Q_UNUSED(durMs);
}
