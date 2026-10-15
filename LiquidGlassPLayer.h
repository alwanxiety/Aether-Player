#pragma once
#include <QMainWindow>
#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLFramebufferObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QFrame>
#include <QDateTime>
#include <QCloseEvent>
#include <QBuffer>
#include <QPointer>
#include <atomic>
#include "Widgets.h"
#include "Workers.h"
#include "SmtcManager.h"
#include "AudioTrack.h"
#include "DlnaCastService.h"
#include "miniaudio.h"

class LiquidGlassGLCanvas : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
public:
    explicit LiquidGlassGLCanvas(QWidget* parent = nullptr);
    ~LiquidGlassGLCanvas() override;

    void setParams(float phase, float mouseNormX, float mouseNormY, float mouseLocalX, float mouseLocalY,
                   const QColor& c1, const QColor& c2, const QColor& c3, float brightness);
    float lastRenderTimeMs() const { return m_lastGpuTimeMs; }

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private:
    QOpenGLShaderProgram* m_program = nullptr;
    QOpenGLShaderProgram* m_blitProgram = nullptr;
    QOpenGLBuffer m_vbo;
    QOpenGLVertexArrayObject m_vao;
    QOpenGLFramebufferObject* m_fbo = nullptr;

    float m_phase = 0.0f;
    float m_mouseNormX = 0.0f, m_mouseNormY = 0.0f;
    float m_mouseLocalX = 0.0f, m_mouseLocalY = 0.0f;
    QColor m_c1, m_c2, m_c3;
    float m_brightness = 1.0f;

    QElapsedTimer m_gpuTimer;
    float m_lastGpuTimeMs = 0.0f;
};

class LiquidGlassPlayer : public QMainWindow {
    Q_OBJECT
public:
    explicit LiquidGlassPlayer(QWidget* parent = nullptr);
    ~LiquidGlassPlayer() override;

protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private slots:
    void masterTick();
    void togglePlay();
    void prevTrack();
    void nextTrack();
    void toggleShuffle();
    void toggleRepeat();
    void toggleFavCurrent();
    void toggleMute();
    void onVolumeChanged(int val);
    void onSeekPreview(int val);
    void onSeek(int val);
    void seekToMs(qint64 ms);
    void onPositionChanged(qint64 pos);
    void onDurationChanged(qint64 dur);
    void filterTracks();
    void switchCategory(const QString& cat);
    void toggleFullscreenLyrics();
    void loadTracksDialog();
    void onTracksBatchLoaded(const QVector<AudioTrack>& tracks);
    void showTrackContextMenu(int row, const QPoint& globalPos);
    void deleteTrackAtRow(int row);
    void onBrightnessChanged(int val);
    void onDebugToggled(bool state);
    void searchVkMusic();
    void searchServerMusic();
    void loadVkMyMusic();
    void testAndSyncVk();

private:
    static void onAudioProcess(void* pUserData, float* pFramesOut, ma_uint64 frameCount);
    void fadeDspToSilence(int maxWaitMs = 24);
    void startDspFadeIn();

    void initUI();
    QWidget* buildTitlebar();
    QWidget* buildSidebar();
    QWidget* buildCenterCatalog();
    QWidget* buildRightInspector();
    QWidget* buildFullscreenLyricsPane();
    QWidget* buildSettingsPane();
    QWidget* buildPlayerbar();

    void setupNativeWindows();
    void initNativeSmtc();
    void applyGlobalFonts();
    void loadSettings();
    void saveSettings();
    void saveLibraryState();
    void loadLibraryState();
    void syncLibraryKaraoke();

    void playTrack(int idx, bool autoStart);
    void stopAndUnloadSound();
    void cancelActiveLoaders();
    void syncTableActiveRow();
    void updateAccentColors(const QColor& accent);
    void renderTable();
    void wakeEngineLoop();
    void traceEngine(const char* category, const QString& detail);

    float getProcessCpuUsage();
    float getProcessRamUsage();

    QVector<AudioTrack>& currentCollection();
    void setCoverPlaceholder(QLabel* label, int size, int radius);
    QPixmap roundPixmap(const QPixmap& src, int size, int radius);
    static QString formatTime(qint64 ms);

    ma_engine m_audioEngine;
    ma_sound m_sound;
    bool m_engineInit = false;
    bool m_soundLoaded = false;
    ma_uint32 m_engineChannels = 2;
    std::atomic<float> m_targetVolume{0.8f};
    std::atomic<float> m_dspGate{0.0f};
    std::atomic<float> m_dspCurrentGain{0.0f};
    std::atomic<bool> m_stitchPending{false};
    float m_lastOutL = 0.0f;
    float m_lastOutR = 0.0f;
    float m_stitchOffsetL = 0.0f;
    float m_stitchOffsetR = 0.0f;
    int m_prevVol = 80;

    int m_currIdx = -1;
    QString m_currentPlayingPath;
    qint64 m_currentDurationMs = 0;
    qint64 m_lastDisplayedSec = -1;
    int m_glTickParity = 0;
    bool m_isShuffled = false;
    int m_repeatMode = 0;

    DlnaDevice m_activeTvDevice;
    bool m_isCastingActive = false;

    bool m_isBoldFont = false;
    bool m_isDebugMode = false;
    bool m_singleClickPlay = false;
    float m_bgBrightness = 1.0f;
    QString m_vkToken;
    QString m_currentNav = "all";
    QString m_prevNav = "all";
    bool m_isFullscreenLyrics = false;

    QColor m_c1{79, 70, 229}, m_targetC1{79, 70, 229};
    QColor m_c2{37, 99, 235}, m_targetC2{37, 99, 235};
    QColor m_c3{14, 165, 233}, m_targetC3{14, 165, 233};

    QVector<AudioTrack> m_tracks;
    QVector<AudioTrack> m_vkTracks;
    QVector<AudioTrack> m_displayTracks;
    QVector<QPointer<TrackLoaderWorker>> m_activeLoaders;

    double m_phase = 0.0;
    double m_mouseNormX = 0.0, m_mouseNormY = 0.0;
    double m_mouseLocalX = 0.0, m_mouseLocalY = 0.0;
    QTimer* m_engineTimer = nullptr;
    QTimer* m_searchDebounceTimer = nullptr;
    QElapsedTimer m_tickTimer;
    qint64 m_lastPauseTime = 0;

    LiquidGlassGLCanvas* m_glCanvas = nullptr;
    AnimatedStackedWidget* m_mainStack = nullptr;
    QWidget* m_standardView = nullptr;
    QFrame* m_sidebarCard = nullptr;
    QFrame* m_centerCard = nullptr;
    QFrame* m_rightInspector = nullptr;
    QFrame* m_fullscreenLyricsView = nullptr;
    QFrame* m_settingsView = nullptr;
    QFrame* m_playerBarCard = nullptr;

    QLineEdit* m_searchInput = nullptr;
    QLabel* m_lblSectionMedia = nullptr;
    QLabel* m_lblSectionSystem = nullptr;
    QPushButton* m_btnNavAll = nullptr;
    QPushButton* m_btnNavFav = nullptr;
    QPushButton* m_btnNavVk = nullptr;
    QPushButton* m_btnAddFiles = nullptr;
    QLabel* m_lblDragHint = nullptr;
    QPushButton* m_btnNavSettings = nullptr;
    QLabel* m_lblStats = nullptr;

    CleanTrackTable* m_table = nullptr;
    TrackTableModel* m_tableModel = nullptr;
    PureTextDelegate* m_tableDelegate = nullptr;

    VinylCoverWidget* m_vinylWidget = nullptr;
    MarqueeLabel* m_inspTitle = nullptr;
    MarqueeLabel* m_inspArtist = nullptr;
    QLabel* m_lblInspLyrHeader = nullptr;
    SmoothSpringButton* m_btnOpenFullLyr = nullptr;
    SmoothKaraokeWidget* m_inspKaraoke = nullptr;

    QLabel* m_fsTitle = nullptr;
    SmoothKaraokeWidget* m_fsKaraoke = nullptr;

    QLabel* m_miniCover = nullptr;
    MarqueeLabel* m_lblBarTitle = nullptr;
    MarqueeLabel* m_lblBarArtist = nullptr;
    QLabel* m_lblCurTime = nullptr;
    QLabel* m_lblDurTime = nullptr;
    FluidClickSlider* m_sliderPos = nullptr;
    FluidClickSlider* m_sliderVol = nullptr;

    SmoothSpringButton* m_btnShuf = nullptr;
    SmoothSpringButton* m_btnPrev = nullptr;
    SmoothSpringButton* m_btnPlay = nullptr;
    SmoothSpringButton* m_btnNext = nullptr;
    SmoothSpringButton* m_btnRep = nullptr;
    SmoothSpringButton* m_btnFav = nullptr;
    SmoothSpringButton* m_btnToggleRight = nullptr;
    SmoothSpringButton* m_btnCast = nullptr;
    SmoothSpringButton* m_iconVol = nullptr;

    GlassToggleSwitch* m_toggleBold = nullptr;
    GlassToggleSwitch* m_toggleSingleClick = nullptr;
    GlassToggleSwitch* m_toggleDebug = nullptr;
    FluidClickSlider* m_sliderBrightness = nullptr;
    QLabel* m_lblBrightnessVal = nullptr;
    QLineEdit* m_inputVkToken = nullptr;
    QPushButton* m_btnSyncVk = nullptr;
    QLabel* m_lblVkStatus = nullptr;

    QVector<SmoothSpringButton*> m_allSpringButtons;
    DebugOverlayWidget* m_debugOverlay = nullptr;
    SmtcManager* m_smtc = nullptr;
    DlnaCastService* m_dlna = nullptr;

    int m_numProcessors = 1;
    qint64 m_lastCpuTime = 0;
    qint64 m_lastSysTime = 0;
    int m_fpsFrameCount = 0;
    qint64 m_lastFpsTime = 0;
    float m_currentFps = 60.0f;
    QString m_activityStatus = "Покой (0% CPU)";
};
