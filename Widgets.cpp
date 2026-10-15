#include "Widgets.h"
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QScrollBar>
#include <QHeaderView>
#include <QRegularExpression>
#include <QFontMetricsF>
#include <QTransform>
#include <algorithm>
#include <cmath>

static const QString FONT_FAM = "Segoe UI";

AnimatedStackedWidget::AnimatedStackedWidget(QWidget* parent)
    : QStackedWidget(parent) {
    m_anim = new QPropertyAnimation(this, "animProgress", this);
    m_anim->setDuration(220);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_anim, &QPropertyAnimation::finished, this, &AnimatedStackedWidget::animationFinished);
}

static QPixmap grabTransparent(QWidget* w, const QSize& fallbackSize) {
    if (!w) return {};
    const QSize sz = w->size().isEmpty() ? fallbackSize : w->size();
    if (sz.isEmpty()) return {};
    const qreal dpr = w->devicePixelRatioF();
    QPixmap pm(sz * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    w->render(&pm, QPoint(), QRegion(), QWidget::DrawChildren);
    return pm;
}

void AnimatedStackedWidget::slideToPage(int index) {
    const int oldIdx = currentIndex();
    if (index == oldIdx || index < 0 || index >= count()) {
        return;
    }
    if (m_isAnimating) {
        m_anim->stop();
        animationFinished();
    }

    m_targetIndex = index;
    m_currentIndex = oldIdx;

    m_prevPixmap = grabTransparent(currentWidget(), size());

    if (QWidget* nextW = widget(index)) {
        nextW->setGeometry(rect());
        nextW->ensurePolished();
        m_nextPixmap = grabTransparent(nextW, size());
    }

    setCurrentIndex(index);
    if (currentWidget()) {
        currentWidget()->hide();
    }

    m_isAnimating = true;
    m_anim->stop();
    m_anim->setStartValue(0.0);
    m_anim->setEndValue(1.0);
    m_anim->start();
}

void AnimatedStackedWidget::animationFinished() {
    m_isAnimating = false;
    m_prevPixmap = QPixmap();
    m_nextPixmap = QPixmap();
    if (currentWidget()) {
        currentWidget()->setGeometry(rect());
        currentWidget()->show();
    }
    update();
}

void AnimatedStackedWidget::paintEvent(QPaintEvent* event) {
    if (!m_isAnimating) {
        QStackedWidget::paintEvent(event);
        return;
    }

    QPainter p(this);
    p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);

    const double progress = std::clamp(m_animProgress, 0.0, 1.0);
    const qreal dir = (m_targetIndex > m_currentIndex) ? 1.0 : -1.0;

    if (!m_prevPixmap.isNull()) {
        const qreal prevOffset = -dir * 16.0 * progress;
        p.setOpacity(1.0 - progress);
        p.drawPixmap(QPointF(prevOffset, 0.0), m_prevPixmap);
    }

    if (!m_nextPixmap.isNull()) {
        const qreal nextOffset = dir * 20.0 * (1.0 - progress);
        p.setOpacity(progress);
        p.drawPixmap(QPointF(nextOffset, 0.0), m_nextPixmap);
    }
}

FluidClickSlider::FluidClickSlider(Qt::Orientation, QWidget* parent)
    : QWidget(parent) {
    setFixedHeight(20);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);
}

void FluidClickSlider::setRange(int min, int max) {
    m_min = min;
    m_max = std::max(min + 1, max);
    update();
}

void FluidClickSlider::setValue(int val) {
    const int clamped = std::clamp(val, m_min, m_max);
    const bool changed = (m_value != clamped);
    m_value = clamped;
    m_smoothProgress = static_cast<double>(m_value - m_min) / (m_max - m_min);
    if (changed) {
        emit valueChanged(m_value);
    }
    update();
}

void FluidClickSlider::setProgress(double normProgress) {
    if (m_isDragging) return;
    const double clamped = std::clamp(normProgress, 0.0, 1.0);
    if (std::abs(clamped - m_smoothProgress) < 0.0004) return;
    m_smoothProgress = clamped;
    m_value = static_cast<int>(m_min + m_smoothProgress * (m_max - m_min));
    update();
}

void FluidClickSlider::updateFromPosition(double posX) {
    const double pad = 4.0;
    const double trackW = std::max(1.0, width() - pad * 2.0);
    const double clampedX = std::clamp(posX - pad, 0.0, trackW);
    m_smoothProgress = clampedX / trackW;
    const int newValue = static_cast<int>(m_min + m_smoothProgress * (m_max - m_min));
    const bool changed = (m_value != newValue);
    m_value = newValue;
    emit sliderMoved(m_value);
    if (changed) {
        emit valueChanged(m_value);
    }
    update();
}

void FluidClickSlider::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_isDragging = true;
        updateFromPosition(event->position().x());
        event->accept();
    }
}

void FluidClickSlider::mouseMoveEvent(QMouseEvent* event) {
    if (m_isDragging) {
        updateFromPosition(event->position().x());
        event->accept();
    }
}

void FluidClickSlider::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && m_isDragging) {
        updateFromPosition(event->position().x());
        m_isDragging = false;
        emit sliderReleased(m_value);
        update();
        event->accept();
    }
}

void FluidClickSlider::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);

    const qreal pad = 4.0;
    const qreal trackH = 4.0;
    const qreal trackW = width() - pad * 2.0;
    const qreal trackY = (height() - trackH) / 2.0;

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 38));
    p.drawRoundedRect(QRectF(pad, trackY, trackW, trackH), 2.0, 2.0);

    const qreal activeW = trackW * m_smoothProgress;
    if (activeW > 0.5) {
        p.setBrush(m_accentColor.isValid() ? m_accentColor : QColor(147, 197, 253));
        p.drawRoundedRect(QRectF(pad, trackY, activeW, trackH), 2.0, 2.0);
    }

    const qreal thumbX = pad + activeW;
    const qreal thumbY = height() / 2.0;
    const qreal thumbR = (m_isDragging || m_isHovered) ? 5.5 : 4.0;

    p.setBrush(Qt::white);
    p.setPen(QPen(QColor(0, 0, 0, 40), 1.0));
    p.drawEllipse(QPointF(thumbX, thumbY), thumbR, thumbR);
}

MarqueeLabel::MarqueeLabel(const QString& text, QWidget* parent)
    : QWidget(parent), m_text(text) {
    m_font = QFont(FONT_FAM, 10, QFont::DemiBold);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    setAttribute(Qt::WA_TransparentForMouseEvents);

    m_animTimer.setInterval(16);
    connect(&m_animTimer, &QTimer::timeout, this, &MarqueeLabel::onTick);
    resetScroll();
}

void MarqueeLabel::setText(const QString& text) {
    QString clean = text;
    clean.remove(QChar(0x0000));
    clean = clean.trimmed();

    if (m_text == clean) return;
    m_text = clean;
    resetScroll();
    update();
}

void MarqueeLabel::setFont(const QFont& font) {
    m_font = font;
    resetScroll();
    update();
}

void MarqueeLabel::setColor(const QColor& color) {
    m_color = color;
    update();
}

void MarqueeLabel::resetScroll() {
    m_offset = 0.0;
    m_direction = 1;
    m_pauseTimer = 2.0;
    QFontMetricsF fm(m_font);
    m_textWidth = fm.horizontalAdvance(m_text);

    if (m_textWidth > width() && width() > 0) {
        m_elapsedTimer.start();
        if (!m_animTimer.isActive()) m_animTimer.start();
    } else {
        m_animTimer.stop();
    }
}

void MarqueeLabel::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    resetScroll();
}

void MarqueeLabel::onTick() {
    const double maxOffset = m_textWidth - width();
    if (maxOffset <= 0) {
        m_animTimer.stop();
        m_offset = 0.0;
        update();
        return;
    }

    double dt = m_elapsedTimer.restart() / 1000.0;
    if (dt > 0.05) dt = 0.016;

    if (m_pauseTimer > 0.0) {
        m_pauseTimer -= dt;
        return;
    }

    const double speed = 36.0;
    m_offset += m_direction * (speed * dt);

    if (m_offset >= maxOffset) {
        m_offset = maxOffset;
        m_direction = -1;
        m_pauseTimer = 1.6;
    } else if (m_offset <= 0.0) {
        m_offset = 0.0;
        m_direction = 1;
        m_pauseTimer = 2.0;
    }

    update();
}

void MarqueeLabel::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    p.setFont(m_font);
    p.setPen(m_color);
    p.setClipRect(rect());

    QFontMetricsF fm(m_font);
    const qreal y = (height() + fm.ascent() - fm.descent()) / 2.0;

    if (m_textWidth <= width()) {
        p.drawText(QPointF(0.0, y), m_text);
    } else {
        p.drawText(QPointF(-m_offset, y), m_text);
    }
}

static QString repairCyrillicMojibake(const QString& str) {
    if (str.isEmpty()) return str;

    if (str.contains(QChar(0x04)) || str.contains(QChar(0x00)) || str.contains("ÿþ") || str.contains(QChar(0xFFFD))) {
        QString cleaned = str;
        cleaned.remove("ÿþ");
        cleaned.remove(QChar(0xFEFF));

        QString repaired;
        repaired.reserve(cleaned.length());

        for (int i = 0; i < cleaned.length(); ++i) {
            const QChar c = cleaned[i];
            if (i + 1 < cleaned.length() && cleaned[i + 1] == QChar(0x04)) {
                const ushort cyrillicChar = static_cast<ushort>(0x0400 | (c.unicode() & 0xFF));
                repaired.append(QChar(cyrillicChar));
                ++i;
            } else if (i + 1 < cleaned.length() && cleaned[i + 1] == QChar(0x00)) {
                repaired.append(c);
                ++i;
            } else if (c != QChar(0x04) && c != QChar(0x00) && c != QChar(0xFFFD)) {
                repaired.append(c);
            }
        }
        return repaired;
    }
    return str;
}

CoverCache& CoverCache::instance() {
    static CoverCache s_instance;
    return s_instance;
}

CoverCache::CoverCache() {
    m_cache.setMaxCost(64 * 1024 * 1024);
}

QPixmap CoverCache::get(const QString& path) {
    if (const QPixmap* pm = m_cache.object(path)) {
        return *pm;
    }
    return {};
}

void CoverCache::put(const QString& path, const QPixmap& pm) {
    if (pm.isNull()) return;
    const int cost = static_cast<int>(pm.width() * pm.height() * 4 * pm.devicePixelRatio());
    m_cache.insert(path, new QPixmap(pm), std::max(1, cost));
}

void CoverCache::clear() {
    m_cache.clear();
}

ClickableLyricLabel::ClickableLyricLabel(const QString& text, QWidget* parent)
    : QLabel(text, parent) {
    setWordWrap(true);
    setCursor(Qt::PointingHandCursor);
    setAutoFillBackground(false);
}

void ClickableLyricLabel::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        emit clicked();
        event->accept();
    }
    QLabel::mousePressEvent(event);
}

SmoothKaraokeWidget::SmoothKaraokeWidget(bool isFullscreen, bool isBold, QWidget* parent)
    : QScrollArea(parent), m_isFullscreen(isFullscreen), m_isBold(isBold) {
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    if (!m_isFullscreen) {
        setStyleSheet("QScrollArea { background: rgba(255, 255, 255, 0.03); border: 1px solid rgba(255, 255, 255, 0.07); border-top: 1px solid rgba(255, 255, 255, 0.12); border-radius: 12px; }");
    } else {
        setStyleSheet("background: transparent; border: none;");
    }

    m_contentWidget = new QWidget(this);
    m_contentWidget->setStyleSheet("background: transparent; border: none;");
    m_layoutV = new QVBoxLayout(m_contentWidget);

    const int padY = isFullscreen ? 60 : 16;
    const int padX = isFullscreen ? 24 : 14;
    m_layoutV->setContentsMargins(padX, padY, padX, padY);
    m_layoutV->setSpacing(isFullscreen ? 16 : 8);
    setWidget(m_contentWidget);

    m_scrollTimer.setInterval(16);
    connect(&m_scrollTimer, &QTimer::timeout, this, &SmoothKaraokeWidget::scrollStep);
}

void SmoothKaraokeWidget::setBold(bool state) {
    m_isBold = state;
    for (int i = 0; i < m_layoutV->count(); ++i) {
        updateLabelState(i, (i == m_currentLineIdx));
    }
}

void SmoothKaraokeWidget::updateLabelState(int idx, bool active) {
    if (idx < 0 || idx >= m_layoutV->count()) return;
    if (auto* lbl = qobject_cast<QLabel*>(m_layoutV->itemAt(idx)->widget())) {
        const int fSize = m_isFullscreen ? (active ? 18 : 17) : (active ? 12 : 11);
        const auto w = active ? (m_isBold ? QFont::ExtraBold : QFont::Bold)
                              : (m_isBold ? QFont::DemiBold : QFont::Normal);
        QFont f(FONT_FAM, fSize, w);
        f.setStyleStrategy(static_cast<QFont::StyleStrategy>(QFont::PreferAntialias | QFont::NoSubpixelAntialias));
        lbl->setFont(f);

        lbl->setAlignment(m_isFullscreen ? Qt::AlignCenter : (Qt::AlignLeft | Qt::AlignVCenter));

        QPalette pal = lbl->palette();
        pal.setColor(QPalette::WindowText, active ? QColor(255, 255, 255) : QColor(203, 213, 225, 166));
        lbl->setPalette(pal);
    }
}

void SmoothKaraokeWidget::setLyrics(const QString& rawLyrics) {
    QLayoutItem* child;
    while ((child = m_layoutV->takeAt(0)) != nullptr) {
        delete child->widget();
        delete child;
    }
    m_lines.clear();
    m_currentLineIdx = -1;
    m_scrollTimer.stop();
    verticalScrollBar()->setValue(0);
    m_currentScrollY = 0.0;

    const QString cleanLyrics = repairCyrillicMojibake(rawLyrics);

    if (cleanLyrics.trimmed().isEmpty()) {
        auto* lbl = new QLabel("Текст песни отсутствует или загружается...", this);
        QFont f(FONT_FAM, m_isFullscreen ? 14 : 10, QFont::Normal);
        f.setStyleStrategy(static_cast<QFont::StyleStrategy>(QFont::PreferAntialias | QFont::NoSubpixelAntialias));
        lbl->setFont(f);
        lbl->setAlignment(m_isFullscreen ? Qt::AlignCenter : (Qt::AlignLeft | Qt::AlignVCenter));
        QPalette pal = lbl->palette();
        pal.setColor(QPalette::WindowText, QColor("#94A3B8"));
        lbl->setPalette(pal);
        lbl->setWordWrap(true);
        m_layoutV->addWidget(lbl);
        return;
    }

    static const QRegularExpression reg(R"(\[(\d{1,2}):(\d{2})(?:\.(\d{1,3}))?\](.*))");
    const QStringList rawLines = cleanLyrics.split('\n');
    bool hasSync = false;

    for (const QString& l : rawLines) {
        const QString line = l.trimmed();
        if (line.isEmpty()) continue;

        const auto match = reg.match(line);
        if (match.hasMatch()) {
            hasSync = true;
            const qint64 m = match.captured(1).toInt();
            const qint64 s = match.captured(2).toInt();
            const qint64 ms = match.captured(3).isEmpty() ? 0 : match.captured(3).leftJustified(3, '0').left(3).toInt();
            const qint64 t_ms = m * 60000 + s * 1000 + ms;
            const QString text = match.captured(4).trimmed();
            if (!text.isEmpty()) {
                m_lines.append({t_ms, text});
            }
        }
    }

    if (hasSync) {
        for (int i = 0; i < m_lines.size(); ++i) {
            auto* lbl = new ClickableLyricLabel(m_lines[i].text, this);
            const qint64 t = m_lines[i].timeMs;
            connect(lbl, &ClickableLyricLabel::clicked, this, [this, t]() { emit seekRequested(t); });
            m_layoutV->addWidget(lbl);
            updateLabelState(i, false);
        }
    } else {
        static const QRegularExpression tagRemover(R"(\[.*?\])");
        for (const QString& line : rawLines) {
            QString clean = line;
            clean.remove(tagRemover);
            clean = clean.trimmed();
            if (!clean.isEmpty()) {
                auto* lbl = new QLabel(clean, this);
                lbl->setWordWrap(true);
                lbl->setAlignment(m_isFullscreen ? Qt::AlignCenter : (Qt::AlignLeft | Qt::AlignVCenter));
                m_layoutV->addWidget(lbl);
            }
        }
        updatePlainStyle();
    }
}

void SmoothKaraokeWidget::updatePlainStyle() {
    const int fSize = m_isFullscreen ? 15 : 10;
    const auto w = m_isBold ? QFont::Bold : QFont::DemiBold;
    for (int i = 0; i < m_layoutV->count(); ++i) {
        if (auto* lbl = qobject_cast<QLabel*>(m_layoutV->itemAt(i)->widget())) {
            QFont f(FONT_FAM, fSize, w);
            f.setStyleStrategy(static_cast<QFont::StyleStrategy>(QFont::PreferAntialias | QFont::NoSubpixelAntialias));
            lbl->setFont(f);
            lbl->setAlignment(m_isFullscreen ? Qt::AlignCenter : (Qt::AlignLeft | Qt::AlignVCenter));
            QPalette pal = lbl->palette();
            pal.setColor(QPalette::WindowText, QColor("#E2E8F0"));
            lbl->setPalette(pal);
        }
    }
}

void SmoothKaraokeWidget::updatePosition(qint64 posMs) {
    if (m_lines.isEmpty()) return;
    int newIdx = -1;
    for (int i = 0; i < m_lines.size(); ++i) {
        if (posMs >= m_lines[i].timeMs) newIdx = i;
        else break;
    }
    if (newIdx != m_currentLineIdx) {
        const int oldIdx = m_currentLineIdx;
        m_currentLineIdx = newIdx;
        updateLabelState(oldIdx, false);
        updateLabelState(m_currentLineIdx, true);
        smoothScrollToActive();
    }
}

void SmoothKaraokeWidget::smoothScrollToActive() {
    if (m_currentLineIdx >= 0 && m_currentLineIdx < m_layoutV->count()) {
        auto* w = m_layoutV->itemAt(m_currentLineIdx)->widget();
        if (w) {
            const double targetY = w->pos().y() - (viewport()->height() / 2.0) + (w->height() / 2.0);
            m_targetScrollY = std::max(0, static_cast<int>(targetY));
            m_currentScrollY = verticalScrollBar()->value();
            m_scrollTimer.start();
        }
    }
}

void SmoothKaraokeWidget::scrollStep() {
    const double diff = m_targetScrollY - m_currentScrollY;
    if (std::abs(diff) < 1.0) {
        verticalScrollBar()->setValue(m_targetScrollY);
        m_scrollTimer.stop();
    } else {
        m_currentScrollY += diff * 0.18;
        verticalScrollBar()->setValue(static_cast<int>(m_currentScrollY));
    }
}

TrackTableModel::TrackTableModel(QObject* parent) : QAbstractTableModel(parent) {}

void TrackTableModel::setTracks(const QVector<AudioTrack>& tracks) {
    beginResetModel();
    m_tracks = tracks;
    endResetModel();
}

int TrackTableModel::playingRow() const {
    if (m_playingPath.isEmpty()) return -1;
    for (int i = 0; i < m_tracks.size(); ++i) {
        if (m_tracks[i].path == m_playingPath) return i;
    }
    return -1;
}

void TrackTableModel::setCurrentPlayingPath(const QString& path, bool isPlaying) {
    const int oldRow = playingRow();
    m_playingPath = path;
    m_isPlaying = isPlaying;
    const int newRow = playingRow();

    if (oldRow >= 0 && oldRow < rowCount()) {
        emit dataChanged(index(oldRow, 0), index(oldRow, columnCount() - 1));
    }
    if (newRow >= 0 && newRow < rowCount() && newRow != oldRow) {
        emit dataChanged(index(newRow, 0), index(newRow, columnCount() - 1));
    }
}

void TrackTableModel::setBold(bool bold) {
    if (m_isBold == bold) return;
    m_isBold = bold;
    if (rowCount() > 0) {
        emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1), {Qt::FontRole});
    }
}

void TrackTableModel::setAccentColor(const QColor& color) {
    if (m_accentColor == color) return;
    m_accentColor = color;
    const int r = playingRow();
    if (r >= 0 && r < rowCount()) {
        emit dataChanged(index(r, 0), index(r, columnCount() - 1), {Qt::ForegroundRole});
    }
}

int TrackTableModel::rowCount(const QModelIndex&) const { return m_tracks.size(); }
int TrackTableModel::columnCount(const QModelIndex&) const { return 5; }

QVariant TrackTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= m_tracks.size()) return {};
    const auto& tr = m_tracks[index.row()];
    const bool isCur = (tr.path == m_playingPath);

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
            case 0: return isCur ? (m_isPlaying ? "ANIM_EQUALIZER" : "PAUSED_EQUALIZER") : QString::number(index.row() + 1);
            case 1: return tr.title;
            case 2: return tr.artist;
            case 3: return tr.album;
            case 4: return tr.durationStr;
            default: break;
        }
    } else if (role == Qt::ForegroundRole) {
        if (isCur) {
            const QColor curAccent = m_accentColor.isValid() ? m_accentColor : QColor("#93C5FD");
            if (index.column() == 0 || index.column() == 1) return curAccent;
            return QColor("#E2E8F0");
        }
        if (index.column() == 1) return QColor("#F8FAFC");
        return QColor("#CBD5E1");
    } else if (role == Qt::FontRole) {
        const auto wTitle = m_isBold ? QFont::Bold : QFont::DemiBold;
        const auto wMeta = m_isBold ? QFont::DemiBold : QFont::Normal;
        QFont f(FONT_FAM, index.column() == 1 ? 10 : 9, index.column() == 1 ? wTitle : wMeta);
        f.setStyleStrategy(static_cast<QFont::StyleStrategy>(QFont::PreferAntialias | QFont::NoSubpixelAntialias));
        return f;
    } else if (role == Qt::TextAlignmentRole) {
        if (index.column() == 0) return static_cast<int>(Qt::AlignCenter);
        if (index.column() == 4) return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
        return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
    }
    return {};
}

QVariant TrackTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation == Qt::Horizontal) {
        if (role == Qt::DisplayRole) {
            static const char* headers[] = {"#", "НАЗВАНИЕ", "ИСПОЛНИТЕЛЬ", "АЛЬБОМ", "ВРЕМЯ"};
            if (section >= 0 && section < 5) return headers[section];
        } else if (role == Qt::TextAlignmentRole) {
            if (section == 0) return static_cast<int>(Qt::AlignCenter);
            if (section == 4) return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
            return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
        }
    }
    return {};
}

PureTextDelegate::PureTextDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

void PureTextDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    painter->save();
    painter->setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);

    const QString text = index.data(Qt::DisplayRole).toString();

    if (index.column() == 0 && (text == "ANIM_EQUALIZER" || text == "PAUSED_EQUALIZER")) {
        const qreal cx = option.rect.center().x();
        const qreal cy = option.rect.center().y();
        const qreal baselineY = cy + 8.0;

        const QColor barColor = m_accentColor.isValid() ? m_accentColor : QColor("#93C5FD");
        painter->setPen(Qt::NoPen);
        painter->setBrush(barColor);

        const qreal barW = 2.4;
        const qreal spacing = 2.0;
        const qreal totalW = 3 * barW + 2 * spacing;
        const qreal startX = cx - totalW / 2.0;

        static const float staticHeights[3] = { 9.0f, 5.0f, 7.5f };

        for (int i = 0; i < 3; ++i) {
            qreal barH = 0.0;
            if (text == "ANIM_EQUALIZER") {
                const float phaseOffset = i * 1.35f;
                const float hFactor = 0.25f + 0.75f * std::abs(std::sin(static_cast<float>(m_phase) * 4.2f + phaseOffset));
                barH = 3.0 + 8.5 * hFactor;
            } else {
                barH = staticHeights[i];
            }

            const QRectF barRect(startX + i * (barW + spacing), baselineY - barH, barW, barH);
            painter->drawRoundedRect(barRect, 1.2, 1.2);
        }
        painter->restore();
        return;
    }

    const QFont font = index.data(Qt::FontRole).value<QFont>();
    const QColor textColor = index.data(Qt::ForegroundRole).value<QColor>();

    painter->setFont(font);
    painter->setPen(textColor);

    QRect textRect = option.rect;
    if (index.column() != 0 && index.column() != 4) {
        textRect.adjust(12, 0, 0, 0);
    } else if (index.column() == 4) {
        textRect.adjust(0, 0, -12, 0);
    }

    const auto align = static_cast<Qt::AlignmentFlag>(index.data(Qt::TextAlignmentRole).toInt());
    painter->drawText(textRect, align, text);
    painter->restore();
}

CleanTrackTable::CleanTrackTable(QWidget* parent) : QTableView(parent) {
    setMouseTracking(true);
    setShowGrid(false);
    setSelectionMode(QAbstractItemView::NoSelection);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setFocusPolicy(Qt::NoFocus);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    verticalScrollBar()->setSingleStep(14);
    verticalHeader()->setVisible(false);
    verticalHeader()->setDefaultSectionSize(42);
    setFrameShape(QFrame::NoFrame);

    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QTableView::customContextMenuRequested, this, [this](const QPoint& pos) {
        const int r = rowAt(pos.y());
        if (r >= 0) {
            setActiveRow(r);
            emit contextMenuRequested(r, viewport()->mapToGlobal(pos));
        }
    });
}

void CleanTrackTable::setActiveRow(int row) {
    activeRow = row;
    if (row >= 0) {
        m_targetPillY = rowViewportPosition(row) + verticalOffset();
        if (m_currentPillY < -50.0) m_currentPillY = m_targetPillY;
    }
    viewport()->update();
}

bool CleanTrackTable::updatePillPhysics() {
    if (!model() || activeRow < 0 || activeRow >= model()->rowCount()) {
        return false;
    }
    m_targetPillY = rowViewportPosition(activeRow) + verticalOffset();
    const double diff = m_targetPillY - m_currentPillY;
    if (std::abs(diff) > 0.4 || m_pillAlpha < 0.99) {
        m_currentPillY += diff * 0.28;
        m_pillAlpha = std::min(1.0, m_pillAlpha + 0.16);
        viewport()->update();
        return true;
    }
    m_currentPillY = m_targetPillY;
    m_pillAlpha = 1.0;
    return false;
}

void CleanTrackTable::mouseMoveEvent(QMouseEvent* event) {
    const int r = rowAt(event->pos().y());
    if (r != hoverRow) {
        hoverRow = r;
        viewport()->update();
    }
    QTableView::mouseMoveEvent(event);
}

void CleanTrackTable::leaveEvent(QEvent* event) {
    hoverRow = -1;
    viewport()->update();
    QTableView::leaveEvent(event);
}

void CleanTrackTable::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        const int r = rowAt(event->pos().y());
        setActiveRow(r);
        if (singleClickPlay && r >= 0) {
            emit rowActivated(r);
        }
    }
    QTableView::mousePressEvent(event);
}

void CleanTrackTable::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        const int r = rowAt(event->pos().y());
        if (!singleClickPlay && r >= 0) {
            setActiveRow(r);
            emit rowActivated(r);
        }
    }
    QTableView::mouseDoubleClickEvent(event);
}

void CleanTrackTable::paintEvent(QPaintEvent* event) {
    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing);

    if (model()) {
        static const QColor activeBrush(255, 255, 255, 20);
        static const QPen activePen(QColor(255, 255, 255, 28), 1);
        static const QColor hoverBrush(255, 255, 255, 10);

        if (hoverRow >= 0 && hoverRow != activeRow) {
            const int hY = rowViewportPosition(hoverRow);
            const int hH = rowHeight(hoverRow);
            if (hY + hH >= 0 && hY <= viewport()->height()) {
                painter.setBrush(hoverBrush);
                painter.setPen(Qt::NoPen);
                painter.drawRoundedRect(QRectF(4, hY + 2, viewport()->width() - 8, hH - 4), 9, 9);
            }
        }

        if (activeRow >= 0 && activeRow < model()->rowCount()) {
            const double targetContentY = rowViewportPosition(activeRow) + verticalOffset();
            if (std::abs(targetContentY - m_currentPillY) <= 0.4) {
                m_currentPillY = targetContentY;
                m_pillAlpha = 1.0;
            }

            const double drawY = m_currentPillY - verticalOffset();
            const int rH = rowHeight(activeRow);
            if (drawY + rH >= 0 && drawY <= viewport()->height()) {
                painter.setOpacity(m_pillAlpha);
                painter.setBrush(activeBrush);
                painter.setPen(activePen);
                painter.drawRoundedRect(QRectF(4, drawY + 2, viewport()->width() - 8, rH - 4), 9, 9);
                painter.setOpacity(1.0);
            }
        }
    }
    QTableView::paintEvent(event);
}

DebugOverlayWidget::DebugOverlayWidget(QWidget* parent) : QWidget(parent) {
    setFixedSize(220, 108);
    setAttribute(Qt::WA_TransparentForMouseEvents);
}

void DebugOverlayWidget::updateMetrics(float fps, float cpuUsage, float gpuTimeMs, const QString& status) {
    m_fps = fps;
    m_cpu = cpuUsage;
    m_gpuMs = gpuTimeMs;
    m_status = status;
    update();
}

void DebugOverlayWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);

    p.setBrush(QColor(12, 16, 24, 215));
    p.setPen(QPen(QColor(255, 255, 255, 30), 1));
    p.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 12, 12);

    const QFont fTitle(FONT_FAM, 8, QFont::Bold);
    p.setFont(fTitle);
    p.setPen(QColor("#93C5FD"));
    p.drawText(QRect(12, 10, width() - 24, 14), Qt::AlignLeft, "✦ ENGINE METRICS (DEBUG)");

    const QFont fBody(FONT_FAM, 8, QFont::Normal);
    p.setFont(fBody);
    p.setPen(QColor("#E2E8F0"));

    const QString l1 = QString("FPS: %1").arg(static_cast<int>(m_fps));
    const QString l2 = QString("CPU (App): %1%").arg(QString::number(m_cpu, 'f', 1));
    const QString l3 = QString("GPU Frame: %1 ms").arg(QString::number(m_gpuMs, 'f', 2));
    const QString l4 = QString("Статус: %1").arg(m_status);

    p.drawText(QRect(12, 28, 90, 14), Qt::AlignLeft, l1);
    p.drawText(QRect(110, 28, 100, 14), Qt::AlignLeft, l2);
    p.drawText(QRect(12, 46, width() - 24, 14), Qt::AlignLeft, l3);

    p.setPen(QColor("#CBD5E1"));
    p.drawText(QRect(12, 66, width() - 24, 30), Qt::AlignLeft | Qt::TextWordWrap, l4);
}

GlassToggleSwitch::GlassToggleSwitch(bool checked, QWidget* parent)
    : QWidget(parent), m_checked(checked), m_thumbPos(checked ? 1.0 : 0.0) {
    setFixedSize(48, 28);
    setCursor(Qt::PointingHandCursor);
    m_animTimer.setInterval(16);
    connect(&m_animTimer, &QTimer::timeout, this, [this]() {
        const double target = m_checked ? 1.0 : 0.0;
        const double diff = target - m_thumbPos;
        if (std::abs(diff) < 0.04) {
            m_thumbPos = target;
            m_animTimer.stop();
        } else {
            m_thumbPos += diff * 0.35;
        }
        update();
    });
}

void GlassToggleSwitch::setChecked(bool checked) {
    if (m_checked != checked) {
        m_checked = checked;
        m_animTimer.start();
    }
}

void GlassToggleSwitch::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_checked = !m_checked;
        m_animTimer.start();
        emit toggled(m_checked);
    }
    QWidget::mousePressEvent(event);
}

void GlassToggleSwitch::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF trackRect(1, 1, width() - 2, height() - 2);
    const qreal r = trackRect.height() / 2.0;

    const QColor activeColor = m_accentColor.isValid() ? m_accentColor : QColor(147, 197, 253);
    const QColor bg = (m_thumbPos > 0.0)
        ? QColor(activeColor.red(), activeColor.green(), activeColor.blue(), static_cast<int>(190 * m_thumbPos))
        : QColor(255, 255, 255, 20);

    p.setBrush(bg);
    p.setPen(QPen(QColor(255, 255, 255, static_cast<int>(30 + 40 * m_thumbPos)), 1));
    p.drawRoundedRect(trackRect, r, r);

    const qreal thumbD = 20.0;
    const qreal curX = 4.0 + (width() - thumbD - 8.0) * m_thumbPos;
    const QRectF thumbRect(curX, (height() - thumbD) / 2.0, thumbD, thumbD);

    p.setBrush(QColor(255, 255, 255, 245));
    p.setPen(Qt::NoPen);
    p.drawEllipse(thumbRect);
}

VinylCoverWidget::VinylCoverWidget(QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumSize(230, 210);
    setMouseTracking(true);
}

void VinylCoverWidget::mouseMoveEvent(QMouseEvent* event) {
    const double cx = width() / 2.0;
    const double cy = height() / 2.0;
    m_targetTiltX = std::clamp((event->position().x() - cx) / cx, -1.0, 1.0);
    m_targetTiltY = std::clamp((event->position().y() - cy) / cy, -1.0, 1.0);
    update();
    QWidget::mouseMoveEvent(event);
}

void VinylCoverWidget::leaveEvent(QEvent* event) {
    m_targetTiltX = 0.0;
    m_targetTiltY = 0.0;
    update();
    QWidget::leaveEvent(event);
}

void VinylCoverWidget::updateCompositeCache(int d) {
    if (d <= 10 || (m_cachedVinylD == d && !m_compositeVinylCache.isNull())) return;
    m_cachedVinylD = d;
    const qreal dpr = devicePixelRatioF();
    m_compositeVinylCache = QPixmap(static_cast<int>(d * dpr), static_cast<int>(d * dpr));
    m_compositeVinylCache.setDevicePixelRatio(dpr);
    m_compositeVinylCache.fill(Qt::transparent);

    QPainter p(&m_compositeVinylCache);
    p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    const qreal cx = d / 2.0;
    const qreal cy = d / 2.0;

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(12, 13, 16));
    p.drawEllipse(QRectF(1, 1, d - 2, d - 2));

    p.setBrush(Qt::NoBrush);
    const int minR = static_cast<int>(d * 0.18);
    const int maxR = static_cast<int>(d / 2.0) - 3;
    for (int r = minR; r < maxR; ++r) {
        if (r % 9 == 0) p.setPen(QPen(QColor(255, 255, 255, 38), 1.0));
        else if (r % 3 == 0) p.setPen(QPen(QColor(255, 255, 255, 18), 0.7));
        else p.setPen(QPen(QColor(0, 0, 0, 80), 0.6));
        p.drawEllipse(QPointF(cx, cy), r, r);
    }

    const qreal labelR = d * 0.175;
    const QRectF labelRect(cx - labelR, cy - labelR, labelR * 2.0, labelR * 2.0);
    QPainterPath labelPath;
    labelPath.addEllipse(labelRect);
    p.save();
    p.setClipPath(labelPath);
    if (!m_coverPixmap.isNull()) {
        const int physLbl = static_cast<int>(labelR * 2.0 * dpr);
        QPixmap scaledLbl = m_coverPixmap.scaled(physLbl, physLbl, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        scaledLbl.setDevicePixelRatio(dpr);
        p.drawPixmap(labelRect.toRect(), scaledLbl);
    } else {
        QLinearGradient lblGrad(labelRect.topLeft(), labelRect.bottomRight());
        lblGrad.setColorAt(0.0, QColor(42, 48, 64));
        lblGrad.setColorAt(1.0, QColor(24, 28, 38));
        p.fillRect(labelRect, lblGrad);
    }
    p.restore();

    p.setBrush(QColor(12, 13, 16));
    p.setPen(QPen(QColor(255, 255, 255, 90), 0.8));
    p.drawEllipse(QPointF(cx, cy), 3.5, 3.5);
}

void VinylCoverWidget::updateCoverCache(int covSize) {
    if (covSize <= 10 || (m_lastCoverSize == covSize && !m_cachedCoverPixmap.isNull())) return;
    m_lastCoverSize = covSize;
    const qreal dpr = devicePixelRatioF();
    m_cachedCoverPixmap = QPixmap(static_cast<int>(covSize * dpr), static_cast<int>(covSize * dpr));
    m_cachedCoverPixmap.setDevicePixelRatio(dpr);
    m_cachedCoverPixmap.fill(Qt::transparent);

    QPainter p(&m_cachedCoverPixmap);
    p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    const QRectF covRect(0.5, 0.5, covSize - 1.0, covSize - 1.0);
    QPainterPath covPath;
    covPath.addRoundedRect(covRect, 12, 12);
    p.save();
    p.setClipPath(covPath);
    if (!m_coverPixmap.isNull()) {
        const int physCov = static_cast<int>(covSize * dpr);
        QPixmap scaledCov = m_coverPixmap.scaled(physCov, physCov, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        scaledCov.setDevicePixelRatio(dpr);
        p.drawPixmap(0, 0, covSize, covSize, scaledCov);
    } else {
        QLinearGradient grad(0, 0, covSize, covSize);
        grad.setColorAt(0.0, QColor(42, 48, 64));
        grad.setColorAt(1.0, QColor(24, 28, 38));
        p.fillRect(covRect, grad);
        p.setPen(QColor(226, 232, 240));
        QFont f(FONT_FAM, static_cast<int>(covSize / 3.4), QFont::Bold);
        f.setStyleStrategy(static_cast<QFont::StyleStrategy>(QFont::PreferAntialias | QFont::NoSubpixelAntialias));
        p.setFont(f);
        p.drawText(covRect, Qt::AlignCenter, "♪");
    }
    p.restore();
    p.setPen(QPen(QColor(255, 255, 255, 32), 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(covRect, 12, 12);
}

void VinylCoverWidget::setCover(const QPixmap& pixmap) {
    m_coverPixmap = pixmap;
    m_cachedVinylD = 0;
    m_lastCoverSize = 0;
    m_compositeVinylCache = QPixmap();
    m_cachedCoverPixmap = QPixmap();
    update();
}

void VinylCoverWidget::setPlaying(bool playing) {
    m_isPlaying = playing;
    m_targetSlide = playing ? 1.0 : 0.0;
}

void VinylCoverWidget::resetAnimation() {
    m_slideProgress = 0.0;
    m_targetSlide = m_isPlaying ? 1.0 : 0.0;
    update();
}

bool VinylCoverWidget::updatePhysics(double dt) {
    bool changed = false;
    const double slideSpeed = 5.5 * dt;
    if (std::abs(m_slideProgress - m_targetSlide) > 0.001) {
        m_slideProgress += (m_targetSlide - m_slideProgress) * std::min(1.0, slideSpeed);
        changed = true;
    } else {
        m_slideProgress = m_targetSlide;
    }

    if (m_isPlaying || m_slideProgress > 0.01) {
        m_vinylAngle = std::fmod(m_vinylAngle + 120.0 * dt, 360.0);
        changed = true;
    }

    const double tiltSpeed = 8.0 * dt;
    if (std::abs(m_currentTiltX - m_targetTiltX) > 0.001 || std::abs(m_currentTiltY - m_targetTiltY) > 0.001) {
        m_currentTiltX += (m_targetTiltX - m_currentTiltX) * std::min(1.0, tiltSpeed);
        m_currentTiltY += (m_targetTiltY - m_currentTiltY) * std::min(1.0, tiltSpeed);
        changed = true;
    }

    return changed;
}

void VinylCoverWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);

    const qreal w = width();
    const qreal h = height();
    const qreal cx = w / 2.0;
    const qreal cy = h / 2.0;

    QTransform tiltTransform;
    tiltTransform.translate(cx, cy);
    tiltTransform.rotate(m_currentTiltX * 7.0, Qt::YAxis);
    tiltTransform.rotate(-m_currentTiltY * 7.0, Qt::XAxis);
    tiltTransform.translate(-cx, -cy);
    p.setTransform(tiltTransform);

    const qreal covSize = std::max(140.0, std::min(h - 10.0, (w - 12.0) / 1.34));
    const qreal covX = 4.0;
    const qreal covY = (h - covSize) / 2.0;

    const int covSizeInt = static_cast<int>(covSize);
    const int vinylDInt = static_cast<int>(covSize * 0.96);
    updateCompositeCache(vinylDInt);
    updateCoverCache(covSizeInt);

    const qreal vinylD = vinylDInt;
    const qreal labelR = vinylD * 0.175;
    const qreal targetOffset = (vinylD * 0.50) + labelR;
    const qreal maxOffset = std::min(targetOffset, w - covX - vinylD - 4.0);
    const qreal curOffset = m_slideProgress * maxOffset;

    const qreal vx = covX + (covSize - vinylD) / 2.0 + curOffset;
    const qreal vy = covY + (covSize - vinylD) / 2.0;

    if (m_slideProgress > 0.005) {
        p.save();
        p.translate(vx + vinylD / 2.0, vy + vinylD / 2.0);
        p.rotate(m_vinylAngle);
        p.translate(-vinylD / 2.0, -vinylD / 2.0);
        p.drawPixmap(0, 0, m_compositeVinylCache);
        p.restore();
    }

    p.drawPixmap(QPointF(covX, covY), m_cachedCoverPixmap);
}

SmoothSpringButton::SmoothSpringButton(const QString& iconType, int size, bool isToggle, QWidget* parent)
    : QWidget(parent), m_iconType(iconType), m_btnSize(size), m_isToggle(isToggle) {
    setFixedSize(size, size);
    setCursor(Qt::PointingHandCursor);
}

bool SmoothSpringButton::updatePhysics() {
    bool changed = false;
    if (std::abs(m_currentScale - m_targetScale) > 0.001) {
        m_currentScale += (m_targetScale - m_currentScale) * 0.25;
        changed = true;
    }
    if (std::abs(m_currentHover - m_targetHover) > 0.001) {
        m_currentHover += (m_targetHover - m_currentHover) * 0.2;
        changed = true;
    }
    if (changed) update();
    return changed;
}

void SmoothSpringButton::setChecked(bool state) {
    m_isActive = state;
    update();
}

void SmoothSpringButton::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) m_targetScale = 0.92;
    QWidget::mousePressEvent(event);
}

void SmoothSpringButton::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_targetScale = rect().contains(event->pos()) ? 1.06 : 1.0;
        if (m_isToggle) m_isActive = !m_isActive;
        emit clicked();
        update();
    }
    QWidget::mouseReleaseEvent(event);
}

void SmoothSpringButton::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const qreal cx = width() / 2.0;
    const qreal cy = height() / 2.0;
    p.translate(cx, cy);
    p.scale(m_currentScale, m_currentScale);
    p.translate(-cx, -cy);

    const QRectF rect(1.5, 1.5, m_btnSize - 3, m_btnSize - 3);
    const qreal radius = rect.width() / 2.0;

    const QColor accent = m_accentColor.isValid() ? m_accentColor : QColor(147, 197, 253);

    if (m_currentHover > 0.01 || m_isActive) {
        const int bgAlpha = !m_isActive ? static_cast<int>(25 * m_currentHover) : 45;
        const QColor bg = !m_isActive
            ? QColor(255, 255, 255, bgAlpha)
            : QColor(accent.red(), accent.green(), accent.blue(), bgAlpha);

        const int borderAlpha = !m_isActive ? static_cast<int>(40 * m_currentHover) : 70;
        p.setBrush(bg);
        p.setPen(QPen(QColor(255, 255, 255, borderAlpha), 1));
        p.drawRoundedRect(rect, radius, radius);
    }

    QColor iconColor;
    if (m_iconType.contains("big")) {
        iconColor = (m_currentHover > 0.01) ? QColor(255, 255, 255) : QColor(241, 245, 249);
    } else {
        iconColor = !m_isActive ? (m_currentHover > 0.01 ? QColor(255, 255, 255) : QColor(203, 213, 225)) : accent;
    }

    p.setPen(QPen(iconColor, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush((m_iconType == "play" || m_iconType == "play_big" || m_iconType == "heart_filled") ? QBrush(iconColor) : Qt::NoBrush);

    if (m_iconType == "play" || m_iconType == "play_big") {
        const qreal s = (m_iconType == "play") ? 6.0 : 8.0;
        QPainterPath path;
        path.moveTo(cx - s + 2, cy - s);
        path.lineTo(cx + s + 1, cy);
        path.lineTo(cx - s + 2, cy + s);
        path.closeSubpath();
        p.drawPath(path);
    } else if (m_iconType == "pause" || m_iconType == "pause_big") {
        const qreal s = (m_iconType == "pause") ? 3.5 : 5.5;
        const qreal h = (m_iconType == "pause") ? 5.0 : 7.5;
        p.fillRect(QRectF(cx - s - 1, cy - h, 3, h * 2), iconColor);
        p.fillRect(QRectF(cx + s - 2, cy - h, 3, h * 2), iconColor);
    } else if (m_iconType == "next") {
        QPainterPath path;
        path.moveTo(cx - 5.0, cy - 5.0); path.lineTo(cx + 1.0, cy); path.lineTo(cx - 5.0, cy + 5.0);
        p.drawPath(path);
        p.drawLine(QPointF(cx + 5.0, cy - 5.0), QPointF(cx + 5.0, cy + 5.0));
    } else if (m_iconType == "prev") {
        QPainterPath path;
        path.moveTo(cx + 5.0, cy - 5.0); path.lineTo(cx - 1.0, cy); path.lineTo(cx + 5.0, cy + 5.0);
        p.drawPath(path);
        p.drawLine(QPointF(cx - 5.0, cy - 5.0), QPointF(cx - 5.0, cy + 5.0));
    } else if (m_iconType == "shuffle") {
        p.drawLine(QPointF(cx - 6.0, cy - 3.0), QPointF(cx - 2.0, cy - 3.0));
        p.drawLine(QPointF(cx - 2.0, cy - 3.0), QPointF(cx + 2.0, cy + 3.0));
        p.drawLine(QPointF(cx + 2.0, cy + 3.0), QPointF(cx + 6.0, cy + 3.0));
        p.drawLine(QPointF(cx + 3.0, cy + 0.5), QPointF(cx + 6.0, cy + 3.0));
        p.drawLine(QPointF(cx + 3.0, cy + 5.5), QPointF(cx + 6.0, cy + 3.0));
    } else if (m_iconType == "repeat") {
        const QRectF rect_r(cx - 6.0, cy - 3.0, 12, 7.0);
        p.drawRoundedRect(rect_r, 2.0, 2.0);
        p.drawLine(QPointF(cx + 2.5, cy - 5.5), QPointF(cx + 6.0, cy - 3.0));
    } else if (m_iconType == "sidebar") {
        const QRectF rect_s(cx - 6.5, cy - 6.5, 13, 13);
        p.drawRoundedRect(rect_s, 2.5, 2.5);
        p.drawLine(QPointF(cx + 1.5, cy - 6.5), QPointF(cx + 1.5, cy + 6.5));
    } else if (m_iconType == "cast") {
        p.drawRoundedRect(QRectF(cx - 7, cy - 6, 14, 11), 2, 2);
        p.drawArc(QRectF(cx - 7, cy + 1, 6, 6), 0, 90 * 16);
        p.drawArc(QRectF(cx - 7, cy - 2, 10, 10), 0, 90 * 16);
    } else if (m_iconType == "fullscreen") {
        p.drawLine(QPointF(cx - 5.5, cy - 2.0), QPointF(cx - 5.5, cy - 5.5));
        p.drawLine(QPointF(cx - 5.5, cy - 5.5), QPointF(cx - 2.0, cy - 5.5));
        p.drawLine(QPointF(cx + 2.0, cy - 5.5), QPointF(cx + 5.5, cy - 5.5));
        p.drawLine(QPointF(cx + 5.5, cy - 5.5), QPointF(cx + 5.5, cy - 2.0));
        p.drawLine(QPointF(cx - 5.5, cy + 2.0), QPointF(cx - 5.5, cy + 5.5));
        p.drawLine(QPointF(cx - 5.5, cy + 5.5), QPointF(cx - 2.0, cy + 5.5));
        p.drawLine(QPointF(cx + 2.0, cy + 5.5), QPointF(cx + 5.5, cy + 5.5));
        p.drawLine(QPointF(cx + 5.5, cy + 5.5), QPointF(cx + 5.5, cy + 2.0));
    } else if (m_iconType == "volume" || m_iconType == "volume_mute") {
        QPainterPath spk;
        spk.moveTo(cx - 5.0, cy - 2.0);
        spk.lineTo(cx - 2.0, cy - 2.0);
        spk.lineTo(cx + 1.8, cy - 5.5);
        spk.lineTo(cx + 1.8, cy + 5.5);
        spk.lineTo(cx - 2.0, cy + 2.0);
        spk.lineTo(cx - 5.0, cy + 2.0);
        spk.closeSubpath();
        p.drawPath(spk);
        if (m_iconType == "volume") {
            p.drawArc(QRectF(cx + 2.8, cy - 4.0, 5.0, 8.0), -60 * 16, 120 * 16);
        } else {
            p.drawLine(QPointF(cx + 3.0, cy - 2.5), QPointF(cx + 7.0, cy + 2.5));
            p.drawLine(QPointF(cx + 7.0, cy - 2.5), QPointF(cx + 3.0, cy + 2.5));
        }
    } else if (m_iconType == "heart" || m_iconType == "heart_filled") {
        QPainterPath path;
        path.moveTo(cx, cy + 5.0);
        path.cubicTo(cx - 6.0, cy - 1.8, cx - 4.5, cy - 6.0, cx, cy - 2.8);
        path.cubicTo(cx + 4.5, cy - 6.0, cx + 6.0, cy - 1.8, cx, cy + 5.0);
        if (m_iconType == "heart_filled") {
            p.setBrush(QColor(244, 63, 94));
            p.setPen(Qt::NoPen);
        }
        p.drawPath(path);
    }
}

WindowControlButton::WindowControlButton(const QString& type, QWidget* parent)
    : QWidget(parent), m_type(type) {
    setFixedSize(30, 26);
    setCursor(Qt::PointingHandCursor);
}

void WindowControlButton::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) emit clicked();
}

void WindowControlButton::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (m_isHovered) {
        p.setBrush(m_type == "close" ? QColor(239, 68, 68) : QColor(255, 255, 255, 14));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(rect(), 5, 5);
    }
    p.setPen(QPen(QColor(148, 163, 184), 1.1));
    const qreal cx = width() / 2.0;
    const qreal cy = height() / 2.0;
    if (m_type == "close") {
        p.drawLine(QPointF(cx - 3.8, cy - 3.8), QPointF(cx + 3.8, cy + 3.8));
        p.drawLine(QPointF(cx + 3.8, cy - 3.8), QPointF(cx - 3.8, cy + 3.8));
    } else if (m_type == "min") {
        p.drawLine(QPointF(cx - 4.2, cy), QPointF(cx + 4.2, cy));
    } else if (m_type == "max") {
        p.drawRect(QRectF(cx - 3.8, cy - 3.8, 7.6, 7.6));
    }
}