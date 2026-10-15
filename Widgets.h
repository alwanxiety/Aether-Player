#pragma once
#include <QWidget>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QTimer>
#include <QElapsedTimer>
#include <QSlider>
#include <QTableView>
#include <QAbstractTableModel>
#include <QStyledItemDelegate>
#include <QPixmap>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QCache>
#include <QStackedWidget>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include "AudioTrack.h"

class AnimatedStackedWidget : public QStackedWidget {
    Q_OBJECT
public:
    explicit AnimatedStackedWidget(QWidget* parent = nullptr);
    void slideToPage(int index);
    bool isAnimating() const { return m_isAnimating; }

protected:
    void paintEvent(QPaintEvent* event) override;

private slots:
    void animationFinished();

private:
    int m_targetIndex = 0;
    int m_currentIndex = 0;
    bool m_isAnimating = false;
    double m_animProgress = 1.0;
    QPixmap m_prevPixmap;
    QPixmap m_nextPixmap;
    QPropertyAnimation* m_anim = nullptr;

    Q_PROPERTY(double animProgress READ animProgress WRITE setAnimProgress)
    double animProgress() const { return m_animProgress; }
    void setAnimProgress(double p) { m_animProgress = p; update(); }
};

class MarqueeLabel : public QWidget {
    Q_OBJECT
public:
    explicit MarqueeLabel(const QString& text = "", QWidget* parent = nullptr);
    void setText(const QString& text);
    QString text() const { return m_text; }
    void setFont(const QFont& font);
    void setColor(const QColor& color);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onTick();

private:
    void resetScroll();

    QString m_text;
    QFont m_font;
    QColor m_color{241, 245, 249};
    double m_offset = 0.0;
    double m_textWidth = 0.0;
    int m_direction = 1;
    double m_pauseTimer = 0.0;
    QTimer m_animTimer;
    QElapsedTimer m_elapsedTimer;
};

class FluidClickSlider : public QWidget {
    Q_OBJECT
public:
    explicit FluidClickSlider(Qt::Orientation orientation = Qt::Horizontal, QWidget* parent = nullptr);
    void setValue(int val);
    int value() const { return m_value; }
    void setRange(int min, int max);
    void setProgress(double normProgress);
    bool isSliderDown() const { return m_isDragging; }
    void setAccentColor(const QColor& color) { m_accentColor = color; update(); }

    QSize sizeHint() const override { return QSize(200, 20); }
    QSize minimumSizeHint() const override { return QSize(60, 20); }

signals:
    void valueChanged(int value);
    void sliderMoved(int value);
    void sliderReleased(int value);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent*) override { m_isHovered = true; update(); }
    void leaveEvent(QEvent*) override { m_isHovered = false; update(); }

private:
    void updateFromPosition(double posX);

    int m_min = 0;
    int m_max = 1000;
    int m_value = 0;
    double m_smoothProgress = 0.0;
    bool m_isDragging = false;
    bool m_isHovered = false;
    QColor m_accentColor{147, 197, 253};
};

class CoverCache {
public:
    static CoverCache& instance();
    QPixmap get(const QString& path);
    void put(const QString& path, const QPixmap& pm);
    void clear();
private:
    CoverCache();
    QCache<QString, QPixmap> m_cache;
};

class ClickableLyricLabel : public QLabel {
    Q_OBJECT
public:
    explicit ClickableLyricLabel(const QString& text = "", QWidget* parent = nullptr);
signals:
    void clicked();
protected:
    void mousePressEvent(QMouseEvent* event) override;
};

class SmoothKaraokeWidget : public QScrollArea {
    Q_OBJECT
public:
    explicit SmoothKaraokeWidget(bool isFullscreen = false, bool isBold = false, QWidget* parent = nullptr);
    void setBold(bool state);
    void setLyrics(const QString& rawLyrics);
    void updatePosition(qint64 posMs);
    void updatePlainStyle();

signals:
    void seekRequested(qint64 posMs);

private slots:
    void scrollStep();

private:
    void smoothScrollToActive();
    void updateLabelState(int idx, bool active);

    bool m_isFullscreen = false;
    bool m_isBold = false;
    struct LyricLine { qint64 timeMs; QString text; };
    QVector<LyricLine> m_lines;
    int m_currentLineIdx = -1;
    int m_targetScrollY = 0;
    double m_currentScrollY = 0.0;

    QWidget* m_contentWidget = nullptr;
    QVBoxLayout* m_layoutV = nullptr;
    QTimer m_scrollTimer;
};

class GlassToggleSwitch : public QWidget {
    Q_OBJECT
public:
    explicit GlassToggleSwitch(bool checked = false, QWidget* parent = nullptr);
    bool isChecked() const { return m_checked; }
    void setChecked(bool checked);
    void setAccentColor(const QColor& color) { m_accentColor = color; update(); }

signals:
    void toggled(bool checked);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    bool m_checked = false;
    double m_thumbPos = 0.0;
    QColor m_accentColor{147, 197, 253};
    QTimer m_animTimer;
};

class VinylCoverWidget : public QWidget {
    Q_OBJECT
public:
    explicit VinylCoverWidget(QWidget* parent = nullptr);
    void setCover(const QPixmap& pixmap);
    void setPlaying(bool playing);
    void resetAnimation();
    bool updatePhysics(double dt);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void updateCompositeCache(int d);
    void updateCoverCache(int covSize);

    QPixmap m_coverPixmap;
    bool m_isPlaying = false;
    double m_vinylAngle = 0.0;
    double m_slideProgress = 0.0;
    double m_targetSlide = 0.0;

    double m_targetTiltX = 0.0;
    double m_targetTiltY = 0.0;
    double m_currentTiltX = 0.0;
    double m_currentTiltY = 0.0;

    int m_cachedVinylD = 0;
    QPixmap m_compositeVinylCache;

    QPixmap m_cachedCoverPixmap;
    int m_lastCoverSize = 0;
};

class TrackTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit TrackTableModel(QObject* parent = nullptr);
    void setTracks(const QVector<AudioTrack>& tracks);
    void setCurrentPlayingPath(const QString& path, bool isPlaying = false);
    void setBold(bool bold);
    void setAccentColor(const QColor& color);
    int playingRow() const;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    const AudioTrack& getTrack(int row) const { return m_tracks[row]; }

private:
    QVector<AudioTrack> m_tracks;
    QString m_playingPath;
    bool m_isPlaying = false;
    bool m_isBold = false;
    QColor m_accentColor{147, 197, 253};
};

class PureTextDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit PureTextDelegate(QObject* parent = nullptr);
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void setPhase(double phase) { m_phase = phase; }
    void setAccentColor(const QColor& color) { m_accentColor = color; }

private:
    double m_phase = 0.0;
    QColor m_accentColor{147, 197, 253};
};

class CleanTrackTable : public QTableView {
    Q_OBJECT
public:
    explicit CleanTrackTable(QWidget* parent = nullptr);
    int activeRow = -1;
    int hoverRow = -1;
    bool singleClickPlay = false;

    void setActiveRow(int row);
    void setSingleClickPlay(bool enabled) { singleClickPlay = enabled; }
    bool updatePillPhysics();

signals:
    void rowActivated(int row);
    void contextMenuRequested(int row, const QPoint& globalPos);

protected:
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    double m_currentPillY = -100.0;
    double m_targetPillY = -100.0;
    double m_pillAlpha = 0.0;
};

class DebugOverlayWidget : public QWidget {
    Q_OBJECT
public:
    explicit DebugOverlayWidget(QWidget* parent = nullptr);
    void updateMetrics(float fps, float cpuUsage, float gpuTimeMs, const QString& status);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    float m_fps = 0.0f;
    float m_cpu = 0.0f;
    float m_gpuMs = 0.0f;
    QString m_status = "Покой (0% CPU)";
};

class SmoothSpringButton : public QWidget {
    Q_OBJECT
public:
    explicit SmoothSpringButton(const QString& iconType = "play", int size = 36, bool isToggle = false, QWidget* parent = nullptr);
    bool updatePhysics();
    void setChecked(bool state);
    bool isChecked() const { return m_isActive; }
    void setIconType(const QString& type) { m_iconType = type; update(); }
    QString iconType() const { return m_iconType; }
    void setAccentColor(const QColor& color) { m_accentColor = color; update(); }

signals:
    void clicked();

protected:
    void enterEvent(QEnterEvent*) override { m_targetScale = 1.06; m_targetHover = 1.0; }
    void leaveEvent(QEvent*) override { m_targetScale = 1.0; m_targetHover = 0.0; }
    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void paintEvent(QPaintEvent* event) override;

private:
    QString m_iconType;
    int m_btnSize = 36;
    bool m_isToggle = false;
    bool m_isActive = false;
    double m_currentScale = 1.0, m_targetScale = 1.0;
    double m_currentHover = 0.0, m_targetHover = 0.0;
    QColor m_accentColor{147, 197, 253};
};

class WindowControlButton : public QWidget {
    Q_OBJECT
public:
    explicit WindowControlButton(const QString& type = "close", QWidget* parent = nullptr);
signals:
    void clicked();
protected:
    void enterEvent(QEnterEvent*) override { m_isHovered = true; update(); }
    void leaveEvent(QEvent*) override { m_isHovered = false; update(); }
    void mousePressEvent(QMouseEvent* e) override;
    void paintEvent(QPaintEvent* event) override;
private:
    QString m_type;
    bool m_isHovered = false;
};