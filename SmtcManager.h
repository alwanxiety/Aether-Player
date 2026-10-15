#pragma once

#include <QObject>
#include <QWidget>
#include <QPixmap>

class SmtcManager : public QObject {
    Q_OBJECT
public:
    explicit SmtcManager(QWidget* parentWindow, QObject* parent = nullptr);
    ~SmtcManager() override;

    bool init();
    void updateMetadata(const QString& title, const QString& artist, const QString& album, const QPixmap& cover);
    void updatePlaybackState(bool isPlaying);
    void updateTimeline(qint64 posMs, qint64 durMs);

    signals:
        void playRequested();
    void pauseRequested();
    void nextRequested();
    void prevRequested();

private:
    QWidget* m_window = nullptr;
};