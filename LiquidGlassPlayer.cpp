#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "LiquidGlassPlayer.h"
#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QFileDialog>
#include <QWindow>
#include <QHeaderView>
#include <QMimeData>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QMenu>
#include <QDebug>
#include <QtConcurrent>
#include <QRandomGenerator>
#include <QPointer>
#include <QThread>
#include <cmath>
#include <algorithm>

#if defined(Q_OS_WIN)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#include <psapi.h>
#endif

static const QString FONT_FAM = "Segoe UI";

static const char* vertexShaderSource = R"(
    #version 330 core
    layout(location = 0) in vec2 aPos;
    out vec2 vUV;
    void main() {
        vUV = (aPos + 1.0) * 0.5;
        vUV.y = 1.0 - vUV.y;
        gl_Position = vec4(aPos, 0.0, 1.0);
    }
)";

static const char* fragmentShaderSource = R"(
    #version 330 core
    in vec2 vUV;
    out vec4 fragColor;

    uniform vec2 u_resolution;
    uniform float u_phase;
    uniform vec2 u_mouseNorm;
    uniform vec2 u_mouseLocal;
    uniform vec3 u_c1;
    uniform vec3 u_c2;
    uniform vec3 u_c3;
    uniform float u_brightness;

    void main() {
        vec2 pixel = vUV * u_resolution;
        vec3 baseBg = vec3(12.0 / 255.0, 14.0 / 255.0, 20.0 / 255.0);

        float diag = length(u_resolution);
        vec3 glow = vec3(0.0);

        vec2 dispPos = vec2(u_resolution.x * 0.22 + (sin(u_phase) * (u_resolution.x * 0.05) + u_mouseNorm.x * (u_resolution.x * 0.08)),
                            u_resolution.y * 0.28 + (cos(u_phase * 0.9) * (u_resolution.y * 0.05) + u_mouseNorm.y * (u_resolution.y * 0.08)));
        float rDisp = diag * 0.36;
        float d0 = length(pixel - dispPos) / rDisp;
        if (d0 < 1.0) {
            float f = smoothstep(1.0, 0.0, d0);
            glow += vec3(67.0/255.0, 56.0/255.0, 202.0/255.0) * (pow(f, 2.2) * 0.35);
        }

        float rad1 = diag * 0.34;
        float d1 = length(pixel - dispPos) / rad1;
        if (d1 < 1.0) {
            float f = smoothstep(1.0, 0.0, d1);
            glow += u_c1 * (pow(f, 1.8) * 0.55);
        }

        vec2 pos2 = vec2(u_resolution.x * 0.78 + (cos(u_phase * 0.75) * (u_resolution.x * 0.06) - u_mouseNorm.x * (u_resolution.x * 0.08)),
                         u_resolution.y * 0.72 + (sin(u_phase * 0.85) * (u_resolution.y * 0.06) - u_mouseNorm.y * (u_resolution.y * 0.09)));
        float rad2 = diag * 0.36;
        float d2 = length(pixel - pos2) / rad2;
        if (d2 < 1.0) {
            float f = smoothstep(1.0, 0.0, d2);
            glow += u_c2 * (pow(f, 1.7) * 0.48);
        }

        vec2 pos3 = vec2(u_resolution.x * 0.50 + (sin(u_phase * 1.3) * (u_resolution.x * 0.05) + u_mouseNorm.x * (u_resolution.x * 0.05)),
                         u_resolution.y * 0.48 + (cos(u_phase * 1.1) * (u_resolution.y * 0.05) + u_mouseNorm.y * (u_resolution.y * 0.06)));
        float rad3 = diag * 0.31;
        float d3 = length(pixel - pos3) / rad3;
        if (d3 < 1.0) {
            float f = smoothstep(1.0, 0.0, d3);
            glow += u_c3 * (pow(f, 1.8) * 0.42);
        }

        vec2 pos4 = vec2(u_resolution.x * 0.80 + (sin(u_phase * 0.6) * (u_resolution.x * 0.06) + u_mouseNorm.x * (u_resolution.x * 0.05)),
                         u_resolution.y * 0.20 + (cos(u_phase * 0.7) * (u_resolution.y * 0.05) + u_mouseNorm.y * (u_resolution.y * 0.05)));
        float rad4 = diag * 0.32;
        float d4 = length(pixel - pos4) / rad4;
        if (d4 < 1.0) {
            float f = smoothstep(1.0, 0.0, d4);
            vec3 c4 = mix(u_c1, u_c3, 0.5);
            glow += c4 * (pow(f, 1.8) * 0.38);
        }

        float radCur = diag * 0.18;
        float dCur = length(pixel - u_mouseLocal) / radCur;
        if (dCur < 1.0) {
            float f = smoothstep(1.0, 0.0, dCur);
            glow += u_c1 * (pow(f, 2.0) * 0.20);
        }

        vec3 finalRgb = baseBg + glow * (u_brightness * 1.1);
        finalRgb = finalRgb / (finalRgb + vec3(0.55)) * 1.45;

        float vignette = (1.0 - length((vUV - 0.5) * 1.2));
        finalRgb *= clamp(vignette + 0.15, 0.75, 1.0);

        fragColor = vec4(clamp(finalRgb, 0.0, 1.0), 1.0);
    }
)";

static const char* blitFragmentShaderSource = R"(
    #version 330 core
    in vec2 vUV;
    out vec4 fragColor;
    uniform sampler2D u_texture;
    void main() {
        fragColor = texture(u_texture, vUV);
    }
)";

LiquidGlassGLCanvas::LiquidGlassGLCanvas(QWidget* parent) : QOpenGLWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
}

LiquidGlassGLCanvas::~LiquidGlassGLCanvas() {
    makeCurrent();
    delete m_fbo;
    m_vbo.destroy();
    m_vao.destroy();
    delete m_program;
    delete m_blitProgram;
    doneCurrent();
}

void LiquidGlassGLCanvas::initializeGL() {
    initializeOpenGLFunctions();

    m_program = new QOpenGLShaderProgram(this);
    m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShaderSource);
    m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShaderSource);
    m_program->link();

    m_blitProgram = new QOpenGLShaderProgram(this);
    m_blitProgram->addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShaderSource);
    m_blitProgram->addShaderFromSourceCode(QOpenGLShader::Fragment, blitFragmentShaderSource);
    m_blitProgram->link();

    static const float vertices[] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
        -1.0f,  1.0f,
        -1.0f,  1.0f,
         1.0f, -1.0f,
         1.0f,  1.0f
    };

    m_vao.create();
    m_vao.bind();

    m_vbo.create();
    m_vbo.bind();
    m_vbo.allocate(vertices, sizeof(vertices));

    m_program->enableAttributeArray(0);
    m_program->setAttributeBuffer(0, GL_FLOAT, 0, 2, 2 * sizeof(float));

    m_vbo.release();
    m_vao.release();
}

void LiquidGlassGLCanvas::resizeGL(int w, int h) {
    delete m_fbo;
    const int fboW = std::max(1, w / 5);
    const int fboH = std::max(1, h / 5);
    m_fbo = new QOpenGLFramebufferObject(fboW, fboH, QOpenGLFramebufferObject::NoAttachment, GL_TEXTURE_2D, GL_RGB);
    glBindTexture(GL_TEXTURE_2D, m_fbo->texture());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void LiquidGlassGLCanvas::setParams(float phase, float mouseNormX, float mouseNormY, float mouseLocalX, float mouseLocalY,
                                   const QColor& c1, const QColor& c2, const QColor& c3, float brightness) {
    m_phase = phase;
    m_mouseNormX = mouseNormX;
    m_mouseNormY = mouseNormY;
    m_mouseLocalX = mouseLocalX;
    m_mouseLocalY = mouseLocalY;
    m_c1 = c1;
    m_c2 = c2;
    m_c3 = c3;
    m_brightness = brightness;
    update();
}

void LiquidGlassGLCanvas::paintGL() {
    if (!m_program || !m_fbo || !m_blitProgram) return;

    m_gpuTimer.start();

    m_fbo->bind();
    glViewport(0, 0, m_fbo->width(), m_fbo->height());

    m_program->bind();
    m_program->setUniformValue("u_resolution", QVector2D(m_fbo->width(), m_fbo->height()));
    m_program->setUniformValue("u_phase", m_phase);
    m_program->setUniformValue("u_mouseNorm", QVector2D(m_mouseNormX, m_mouseNormY));

    const float scaleX = static_cast<float>(m_fbo->width()) / static_cast<float>(std::max(1, width()));
    const float scaleY = static_cast<float>(m_fbo->height()) / static_cast<float>(std::max(1, height()));
    m_program->setUniformValue("u_mouseLocal", QVector2D(m_mouseLocalX * scaleX, (height() - m_mouseLocalY) * scaleY));

    m_program->setUniformValue("u_c1", QVector3D(m_c1.redF(), m_c1.greenF(), m_c1.blueF()));
    m_program->setUniformValue("u_c2", QVector3D(m_c2.redF(), m_c2.greenF(), m_c2.blueF()));
    m_program->setUniformValue("u_c3", QVector3D(m_c3.redF(), m_c3.greenF(), m_c3.blueF()));
    m_program->setUniformValue("u_brightness", m_brightness);

    m_vao.bind();
    glDrawArrays(GL_TRIANGLES, 0, 6);
    m_program->release();
    m_fbo->release();

    glViewport(0, 0, width(), height());
    m_blitProgram->bind();
    glBindTexture(GL_TEXTURE_2D, m_fbo->texture());
    m_blitProgram->setUniformValue("u_texture", 0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindTexture(GL_TEXTURE_2D, 0);
    m_vao.release();
    m_blitProgram->release();

    m_lastGpuTimeMs = m_gpuTimer.nsecsElapsed() / 1000000.0f;
}

static void extractColorsFromPixmap(const QPixmap& pm, QColor& c1, QColor& c2, QColor& c3) {
    if (pm.isNull()) {
        c1 = QColor(79, 70, 229);
        c2 = QColor(37, 99, 235);
        c3 = QColor(14, 165, 233);
        return;
    }

    const QImage img = pm.toImage().scaled(32, 32, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    QMap<int, QVector<QColor>> buckets;

    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const QColor col = img.pixelColor(x, y);
            if (col.lightness() > 25 && col.lightness() < 235 && col.hslSaturation() > 15) {
                const int key = col.hslHue() >= 0 ? col.hslHue() / 25 : 0;
                buckets[key].append(col);
            }
        }
    }

    QVector<QVector<QColor>> sortedBuckets;
    for (auto it = buckets.begin(); it != buckets.end(); ++it) sortedBuckets.append(it.value());
    std::sort(sortedBuckets.begin(), sortedBuckets.end(), [](const auto& a, const auto& b) { return a.size() > b.size(); });

    QVector<QColor> vibrantColors;
    for (const auto& b : sortedBuckets) {
        int r = 0, g = 0, bl = 0;
        for (const auto& c : b) { r += c.red(); g += c.green(); bl += c.blue(); }
        r /= b.size(); g /= b.size(); bl /= b.size();
        const QColor col(r, g, bl);

        const int h = col.hsvHue() >= 0 ? col.hsvHue() : 210;
        const int s = std::clamp(static_cast<int>(col.hsvSaturation() * 1.3), 130, 240);
        const int v = std::clamp(static_cast<int>(col.value() * 1.1), 110, 185);

        vibrantColors.append(QColor::fromHsv(h, s, v));
        if (vibrantColors.size() >= 3) break;
    }

    if (vibrantColors.size() >= 3) {
        c1 = vibrantColors[0]; c2 = vibrantColors[1]; c3 = vibrantColors[2];
    } else if (vibrantColors.size() == 2) {
        c1 = vibrantColors[0]; c2 = vibrantColors[1];
        c3 = QColor::fromHsv((c1.hslHue() + 180) % 360, 150, 160);
    } else if (!vibrantColors.isEmpty()) {
        c1 = vibrantColors[0];
        c2 = QColor::fromHsv((c1.hslHue() + 60) % 360, 150, 160);
        c3 = QColor::fromHsv((c1.hslHue() + 180) % 360, 140, 160);
    } else {
        c1 = QColor(79, 70, 229);
        c2 = QColor(37, 99, 235);
        c3 = QColor(14, 165, 233);
    }
}

LiquidGlassPlayer::LiquidGlassPlayer(QWidget* parent) : QMainWindow(parent) {
    loadSettings();
    setWindowTitle("Aether Audio");
    resize(1200, 780);
    setMinimumSize(960, 620);
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    setAttribute(Qt::WA_TranslucentBackground);
    setAcceptDrops(true);

#if defined(Q_OS_WIN)
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    m_numProcessors = std::max<int>(1, sysInfo.dwNumberOfProcessors);
#endif

    ma_engine_config engineConfig = ma_engine_config_init();
    engineConfig.periodSizeInMilliseconds = 10;
    engineConfig.onProcess = &LiquidGlassPlayer::onAudioProcess;
    engineConfig.pProcessUserData = this;
    if (ma_engine_init(&engineConfig, &m_audioEngine) == MA_SUCCESS) {
        m_engineInit = true;
        m_engineChannels = std::max<ma_uint32>(1, ma_engine_get_channels(&m_audioEngine));
        ma_engine_set_volume(&m_audioEngine, 1.0f);
        traceEngine("AUDIO", "miniaudio WASAPI engine initialized with DSP De-Click processor");
    }

    m_dlna = new DlnaCastService(this);
    connect(m_dlna, &DlnaCastService::castStatusChanged, this, [this](const QString& msg) {
        m_activityStatus = msg;
    });

    m_searchDebounceTimer = new QTimer(this);
    m_searchDebounceTimer->setSingleShot(true);
    m_searchDebounceTimer->setInterval(350);
    connect(m_searchDebounceTimer, &QTimer::timeout, this, [this]() {
        if (m_currentNav == "vk") {
            searchVkMusic();
        }
    });

    initUI();
    initNativeSmtc();
    setupNativeWindows();
    applyGlobalFonts();
    loadLibraryState();

    m_tickTimer.start();
    m_engineTimer = new QTimer(this);
    m_engineTimer->setInterval(16);
    connect(m_engineTimer, &QTimer::timeout, this, &LiquidGlassPlayer::masterTick);
    m_engineTimer->start();
}

void LiquidGlassPlayer::cancelActiveLoaders() {
    for (auto& loader : m_activeLoaders) {
        if (loader) {
            loader->cancel();
            if (loader->isRunning()) {
                loader->wait(2000);
            }
        }
    }
    m_activeLoaders.clear();
}

LiquidGlassPlayer::~LiquidGlassPlayer() {
    if (m_engineTimer) m_engineTimer->stop();
    cancelActiveLoaders();
    stopAndUnloadSound();
    if (m_engineInit) {
        ma_engine_uninit(&m_audioEngine);
    }
}

void LiquidGlassPlayer::closeEvent(QCloseEvent* event) {
    if (m_engineTimer) m_engineTimer->stop();
    cancelActiveLoaders();
    stopAndUnloadSound();
    if (m_engineInit) {
        ma_engine_uninit(&m_audioEngine);
        m_engineInit = false;
    }
    QMainWindow::closeEvent(event);
    qApp->quit();
}

void LiquidGlassPlayer::traceEngine(const char* category, const QString& detail) {
    const QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    qDebug().noquote() << QString("[%1] [%2] %3").arg(timestamp, QString(category).leftJustified(9, ' '), detail);
}

void LiquidGlassPlayer::onAudioProcess(void* pUserData, float* pFramesOut, ma_uint64 frameCount) {
    auto* self = static_cast<LiquidGlassPlayer*>(pUserData);
    if (!self || !pFramesOut || frameCount == 0) return;

    const ma_uint32 channels = self->m_engineChannels;
    const float target = self->m_dspGate.load(std::memory_order_relaxed) * self->m_targetVolume.load(std::memory_order_relaxed);
    float cur = self->m_dspCurrentGain.load(std::memory_order_relaxed);

    float lastL = self->m_lastOutL;
    float lastR = self->m_lastOutR;
    float stitchL = self->m_stitchOffsetL;
    float stitchR = self->m_stitchOffsetR;

    if (self->m_stitchPending.exchange(false, std::memory_order_acquire)) {
        const float firstL = pFramesOut[0] * cur;
        const float firstR = (channels > 1 ? pFramesOut[1] : pFramesOut[0]) * cur;
        stitchL = lastL - firstL;
        stitchR = lastR - firstR;
    }

    constexpr float kGainSmooth = 0.0055f;
    constexpr float kStitchDecay = 0.990f;

    for (ma_uint64 i = 0; i < frameCount; ++i) {
        cur += (target - cur) * kGainSmooth;
        if (target == 0.0f && cur < 0.00015f) cur = 0.0f;
        else if (std::abs(target - cur) < 0.00015f) cur = target;

        if (channels == 2) {
            const float outL = pFramesOut[i * 2 + 0] * cur + stitchL;
            const float outR = pFramesOut[i * 2 + 1] * cur + stitchR;
            pFramesOut[i * 2 + 0] = outL;
            pFramesOut[i * 2 + 1] = outR;
            lastL = outL;
            lastR = outR;
        } else {
            for (ma_uint32 c = 0; c < channels; ++c) {
                pFramesOut[i * channels + c] = pFramesOut[i * channels + c] * cur + stitchL;
            }
            lastL = pFramesOut[i * channels];
            lastR = lastL;
        }

        stitchL *= kStitchDecay;
        stitchR *= kStitchDecay;
        if (std::abs(stitchL) < 0.00005f) stitchL = 0.0f;
        if (std::abs(stitchR) < 0.00005f) stitchR = 0.0f;
    }

    self->m_dspCurrentGain.store(cur, std::memory_order_release);
    self->m_lastOutL = lastL;
    self->m_lastOutR = lastR;
    self->m_stitchOffsetL = stitchL;
    self->m_stitchOffsetR = stitchR;
}

void LiquidGlassPlayer::fadeDspToSilence(int maxWaitMs) {
    m_dspGate.store(0.0f, std::memory_order_release);
    if (!m_engineInit || !m_soundLoaded || !ma_sound_is_playing(&m_sound)) {
        m_dspCurrentGain.store(0.0f, std::memory_order_release);
        return;
    }
    QElapsedTimer waitTimer;
    waitTimer.start();
    while (m_dspCurrentGain.load(std::memory_order_acquire) > 0.0005f && waitTimer.elapsed() < maxWaitMs) {
        QThread::msleep(1);
    }
    QThread::msleep(2);
    m_stitchPending.store(true, std::memory_order_release);
}

void LiquidGlassPlayer::startDspFadeIn() {
    m_stitchPending.store(true, std::memory_order_release);
    m_dspCurrentGain.store(0.0f, std::memory_order_release);
    m_dspGate.store(1.0f, std::memory_order_release);
}

void LiquidGlassPlayer::stopAndUnloadSound() {
    if (!m_soundLoaded) return;
    fadeDspToSilence();
    ma_sound_stop(&m_sound);
    ma_sound_uninit(&m_sound);
    m_soundLoaded = false;
}

void LiquidGlassPlayer::syncTableActiveRow() {
    if (!m_table) return;
    int displayIdx = -1;
    if (!m_currentPlayingPath.isEmpty()) {
        for (int i = 0; i < m_displayTracks.size(); ++i) {
            if (m_displayTracks[i].path == m_currentPlayingPath) {
                displayIdx = i;
                break;
            }
        }
    }
    m_table->setActiveRow(displayIdx);
}

void LiquidGlassPlayer::updateAccentColors(const QColor& accent) {
    for (auto* btn : m_allSpringButtons) {
        if (btn) btn->setAccentColor(accent);
    }
    if (m_tableDelegate) {
        m_tableDelegate->setAccentColor(accent);
    }
    if (m_tableModel) {
        m_tableModel->setAccentColor(accent);
    }
    if (m_table) {
        m_table->viewport()->update();
    }
    if (m_sliderPos) {
        m_sliderPos->setAccentColor(accent);
    }
    if (m_sliderVol) {
        m_sliderVol->setAccentColor(accent);
    }
    if (m_sliderBrightness) {
        m_sliderBrightness->setAccentColor(accent);
    }

    if (m_toggleBold) m_toggleBold->setAccentColor(accent);
    if (m_toggleSingleClick) m_toggleSingleClick->setAccentColor(accent);
    if (m_toggleDebug) m_toggleDebug->setAccentColor(accent);

    const QString hex = accent.name();

    if (m_lblBrightnessVal) {
        m_lblBrightnessVal->setStyleSheet(QString("color: %1; background: transparent; border: none; font-weight: 600;").arg(hex));
    }

    if (m_btnSyncVk) {
        m_btnSyncVk->setStyleSheet(QString(
            "QPushButton { background: rgba(%1, %2, %3, 0.20); border: 1px solid rgba(%1, %2, %3, 0.45); border-radius: 8px; color: %4; font-size: 11px; font-weight: 600; padding: 6px 12px; }"
            "QPushButton:hover { background: rgba(%1, %2, %3, 0.35); color: #FFFFFF; }"
        ).arg(accent.red()).arg(accent.green()).arg(accent.blue()).arg(hex));
    }

    if (m_lblCurTime) m_lblCurTime->setStyleSheet(QString("color: %1; background: transparent; border: none;").arg(hex));
    if (m_lblInspLyrHeader) m_lblInspLyrHeader->setStyleSheet(QString("color: %1; font-size: 9px; letter-spacing: 0.7px; background: transparent; border: none;").arg(hex));
    if (m_lblBarTitle) m_lblBarTitle->setColor(QColor("#FFFFFF"));
}

QVector<AudioTrack>& LiquidGlassPlayer::currentCollection() {
    return (m_currentNav == "vk") ? m_vkTracks : m_tracks;
}

void LiquidGlassPlayer::wakeEngineLoop() {
    if (m_engineTimer && m_engineTimer->interval() != 16) {
        m_engineTimer->setInterval(16);
    }
}

void LiquidGlassPlayer::initNativeSmtc() {
    if (!m_smtc) {
        m_smtc = new SmtcManager(this, this);
        if (m_smtc->init()) {
            connect(m_smtc, &SmtcManager::playRequested, this, &LiquidGlassPlayer::togglePlay);
            connect(m_smtc, &SmtcManager::pauseRequested, this, &LiquidGlassPlayer::togglePlay);
            connect(m_smtc, &SmtcManager::nextRequested, this, &LiquidGlassPlayer::nextTrack);
            connect(m_smtc, &SmtcManager::prevRequested, this, &LiquidGlassPlayer::prevTrack);
        }
    }
}

void LiquidGlassPlayer::setupNativeWindows() {
#if defined(Q_OS_WIN)
    HWND hwnd = reinterpret_cast<HWND>(winId());
    LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    SetWindowLongW(hwnd, GWL_STYLE, style | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_CAPTION | WS_THICKFRAME);
    MARGINS margins = {1, 1, 1, 1};
    DwmExtendFrameIntoClientArea(hwnd, &margins);
#endif
}

void LiquidGlassPlayer::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    if (m_glCanvas && centralWidget()) {
        m_glCanvas->setGeometry(centralWidget()->rect());
    }
    if (m_debugOverlay) {
        m_debugOverlay->move(24, height() - 175);
    }
    wakeEngineLoop();
}

void LiquidGlassPlayer::loadSettings() {
    QFile f("aether_settings.json");
    if (f.open(QIODevice::ReadOnly)) {
        const auto doc = QJsonDocument::fromJson(f.readAll());
        const auto obj = doc.object();
        m_isBoldFont = obj.value("bold_font").toBool(false);
        m_isDebugMode = obj.value("debug_mode").toBool(false);
        m_singleClickPlay = obj.value("single_click_play").toBool(false);
        m_bgBrightness = static_cast<float>(obj.value("bg_brightness").toDouble(1.0));
        m_vkToken = obj.value("vk_token").toString("");
    }
}

void LiquidGlassPlayer::saveSettings() {
    QJsonObject obj;
    obj["bold_font"] = m_isBoldFont;
    obj["debug_mode"] = m_isDebugMode;
    obj["single_click_play"] = m_singleClickPlay;
    obj["bg_brightness"] = m_bgBrightness;
    obj["vk_token"] = m_vkToken;
    QFile f("aether_settings.json");
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(obj).toJson());
    }
}

void LiquidGlassPlayer::onBrightnessChanged(int val) {
    m_bgBrightness = val / 100.0f;
    if (m_lblBrightnessVal) {
        m_lblBrightnessVal->setText(QString("%1%").arg(val));
    }
    saveSettings();
    wakeEngineLoop();
}

void LiquidGlassPlayer::onDebugToggled(bool state) {
    m_isDebugMode = state;
    if (m_debugOverlay) m_debugOverlay->setVisible(state);
    saveSettings();
}

float LiquidGlassPlayer::getProcessCpuUsage() {
#if defined(Q_OS_WIN)
    FILETIME ftCreation, ftExit, ftKernel, ftUser;
    if (GetProcessTimes(GetCurrentProcess(), &ftCreation, &ftExit, &ftKernel, &ftUser)) {
        ULARGE_INTEGER k, u;
        k.LowPart = ftKernel.dwLowDateTime; k.HighPart = ftKernel.dwHighDateTime;
        u.LowPart = ftUser.dwLowDateTime; u.HighPart = ftUser.dwHighDateTime;
        const qint64 totalProcTime = k.QuadPart + u.QuadPart;

        FILETIME ftNow;
        GetSystemTimeAsFileTime(&ftNow);
        ULARGE_INTEGER now;
        now.LowPart = ftNow.dwLowDateTime; now.HighPart = ftNow.dwHighDateTime;
        const qint64 nowTime = now.QuadPart;

        if (m_lastSysTime > 0 && nowTime > m_lastSysTime) {
            const double totalDelta = static_cast<double>(nowTime - m_lastSysTime) * static_cast<double>(m_numProcessors);
            const double usage = static_cast<double>(totalProcTime - m_lastCpuTime) / totalDelta * 100.0;
            m_lastCpuTime = totalProcTime;
            m_lastSysTime = nowTime;
            return static_cast<float>(std::clamp(usage, 0.0, 100.0));
        }
        m_lastCpuTime = totalProcTime;
        m_lastSysTime = nowTime;
    }
#endif
    return 0.1f;
}

float LiquidGlassPlayer::getProcessRamUsage() {
#if defined(Q_OS_WIN)
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return static_cast<float>(pmc.WorkingSetSize / (1024.0 * 1024.0));
    }
#endif
    return 0.0f;
}

void LiquidGlassPlayer::searchVkMusic() {
    const QString q = m_searchInput->text().trimmed();
    if (q.isEmpty() || m_vkToken.trimmed().isEmpty()) return;

    traceEngine("NET", QString("VK Audio Search: '%1'").arg(q));
    auto* worker = new VkMusicWorker(m_vkToken, this);
    connect(worker, &VkMusicWorker::vkTracksLoaded, this, [this, worker, q](const QVector<AudioTrack>& tracks) {
        if (m_searchInput->text().trimmed() == q) {
            m_vkTracks = tracks;
            filterTracks();
            traceEngine("NET", QString("VK Audio: %1 tracks loaded").arg(tracks.size()));
            wakeEngineLoop();
        }
        worker->deleteLater();
    });
    connect(worker, &VkMusicWorker::errorOccurred, this, [this, worker](const QString& err) {
        traceEngine("NET", "VK Search Error: " + err);
        worker->deleteLater();
    });
    worker->search(q);
}

void LiquidGlassPlayer::searchServerMusic() {
    const QString q = m_searchInput->text().trimmed();
    if (q.isEmpty()) return;

    traceEngine("NET", QString("Server Music Search: '%1'").arg(q));
    auto* worker = new ServerMusicWorker("http://87.251.66.219:8080", this);

    connect(worker, &ServerMusicWorker::serverTracksLoaded, this, [this, worker, q](const QVector<AudioTrack>& serverTracks) {
        if (m_searchInput->text().trimmed() == q) {
            bool addedNew = false;
            for (const auto& sTrack : serverTracks) {
                const bool exists = std::any_of(m_tracks.begin(), m_tracks.end(), [&sTrack](const AudioTrack& local) {
                    return local.title.compare(sTrack.title, Qt::CaseInsensitive) == 0 &&
                           local.artist.compare(sTrack.artist, Qt::CaseInsensitive) == 0;
                });

                if (!exists) {
                    m_tracks.append(sTrack);
                    addedNew = true;
                }
            }

            if (addedNew) {
                filterTracks();
                traceEngine("NET", QString("Server Music: added %1 new tracks").arg(serverTracks.size()));
            }
            wakeEngineLoop();
        }
        worker->deleteLater();
    });

    connect(worker, &ServerMusicWorker::errorOccurred, this, [this, worker](const QString& err) {
        traceEngine("NET", "Server Error: " + err);
        worker->deleteLater();
    });

    worker->search(q);
}

void LiquidGlassPlayer::loadVkMyMusic() {
    testAndSyncVk();
}

void LiquidGlassPlayer::testAndSyncVk() {
    if (m_vkToken.trimmed().isEmpty()) {
        m_lblVkStatus->setText("✕ Вставьте токен");
        m_lblVkStatus->setStyleSheet("color: #EF4444; font-size: 11px;");
        return;
    }

    m_lblVkStatus->setText("Синхронизация...");
    m_lblVkStatus->setStyleSheet("color: #93C5FD; font-size: 11px;");
    traceEngine("NET", "VK Sync: Requesting audio.get");

    auto* worker = new VkMusicWorker(m_vkToken, this);
    connect(worker, &VkMusicWorker::vkTracksLoaded, this, [this, worker](const QVector<AudioTrack>& tracks) {
        m_vkTracks = tracks;
        m_lblVkStatus->setText(QString("✓ Найдено %1 треков").arg(tracks.size()));
        m_lblVkStatus->setStyleSheet("color: #10B981; font-size: 11px;");
        filterTracks();
        switchCategory("vk");
        traceEngine("NET", QString("VK Sync Success: %1 tracks parsed").arg(tracks.size()));
        worker->deleteLater();
        wakeEngineLoop();
    });
    connect(worker, &VkMusicWorker::errorOccurred, this, [this, worker](const QString& err) {
        m_lblVkStatus->setText("✕ " + err);
        m_lblVkStatus->setStyleSheet("color: #EF4444; font-size: 11px;");
        traceEngine("NET", "VK Sync Error: " + err);
        worker->deleteLater();
    });
    worker->getMyTracks();
}

void LiquidGlassPlayer::applyGlobalFonts() {
    const auto wNorm = m_isBoldFont ? QFont::Bold : QFont::Normal;
    const auto wSemi = m_isBoldFont ? QFont::ExtraBold : QFont::DemiBold;
    const auto wBold = m_isBoldFont ? QFont::ExtraBold : QFont::Bold;
    const QString fontCssWeight = m_isBoldFont ? "bold" : "600";

    QFont baseFont(FONT_FAM, 10, wNorm);
    baseFont.setStyleStrategy(static_cast<QFont::StyleStrategy>(QFont::PreferAntialias | QFont::NoSubpixelAntialias));
    QApplication::setFont(baseFont);

    auto makeFont = [](int size, QFont::Weight weight) {
        QFont f(FONT_FAM, size, weight);
        f.setStyleStrategy(static_cast<QFont::StyleStrategy>(QFont::PreferAntialias | QFont::NoSubpixelAntialias));
        return f;
    };

    m_lblSectionMedia->setFont(makeFont(10, wSemi));
    m_lblSectionSystem->setFont(makeFont(10, wSemi));
    m_btnNavAll->setFont(makeFont(12, wSemi));
    m_btnNavFav->setFont(makeFont(12, wSemi));
    m_btnNavVk->setFont(makeFont(12, wSemi));
    m_btnAddFiles->setFont(makeFont(12, wSemi));
    m_lblDragHint->setFont(makeFont(10, wNorm));
    m_btnNavSettings->setFont(makeFont(12, wSemi));
    m_lblStats->setFont(makeFont(10, wNorm));

    m_sidebarCard->setStyleSheet(QString(
        "QFrame { background: rgba(18, 22, 32, 0.45); border: 1px solid rgba(255, 255, 255, 0.10); border-radius: 18px; }"
        "QLabel { color: #CBD5E1; font-weight: %1; font-size: 10px; text-transform: uppercase; background: transparent; border: none; }"
        "QPushButton { background: transparent; border: none; border-radius: 8px; color: #CBD5E1; text-align: left; padding: 9px 11px; font-size: 12px; font-weight: %1; }"
        "QPushButton:hover { background: rgba(255, 255, 255, 0.08); color: #F8FAFC; }"
        "QPushButton:checked { background: rgba(255, 255, 255, 0.14); border: 1px solid rgba(255, 255, 255, 0.16); color: #FFFFFF; font-weight: %1; }"
    ).arg(fontCssWeight));

    m_table->setStyleSheet(QString(
        "QTableView { background: transparent; border: none; outline: none; }"
        "QHeaderView { background: transparent; border: none; }"
        "QHeaderView::section {"
        "   background: transparent; border: none; color: #CBD5E1; font-family: 'Segoe UI'; font-size: 10px; font-weight: %1; letter-spacing: 1.5px; padding-left: 12px; padding-bottom: 8px; border-bottom: 1px solid rgba(255, 255, 255, 0.06); text-align: left;"
        "}"
        "QHeaderView::section:first { padding-left: 0px; padding-right: 0px; text-align: center; }"
        "QHeaderView::section:last { padding-left: 0px; padding-right: 12px; text-align: right; }"
    ).arg(fontCssWeight));

    m_inspTitle->setFont(makeFont(12, wBold));
    m_inspArtist->setFont(makeFont(10, wNorm));
    m_lblInspLyrHeader->setFont(makeFont(9, wSemi));
    m_inspKaraoke->setBold(m_isBoldFont);

    m_fsTitle->setFont(makeFont(14, wBold));
    m_fsKaraoke->setBold(m_isBoldFont);

    m_lblBarTitle->setFont(makeFont(10, wBold));
    m_lblBarArtist->setFont(makeFont(9, wNorm));
    m_lblCurTime->setFont(makeFont(8, wNorm));
    m_lblDurTime->setFont(makeFont(8, wNorm));

    m_tableModel->setBold(m_isBoldFont);
}

void LiquidGlassPlayer::initUI() {
    auto* root = new QWidget(this);
    setCentralWidget(root);

    m_glCanvas = new LiquidGlassGLCanvas(root);
    m_glCanvas->setGeometry(rect());
    m_glCanvas->lower();

    auto* rootLay = new QVBoxLayout(root);
    rootLay->setContentsMargins(14, 12, 14, 14);
    rootLay->setSpacing(10);

    rootLay->addWidget(buildTitlebar(), 0);

    auto* middleLay = new QHBoxLayout();
    middleLay->setContentsMargins(0, 0, 0, 0);
    middleLay->setSpacing(12);

    middleLay->addWidget(buildSidebar(), 0);

    m_mainStack = new AnimatedStackedWidget(this);

    m_standardView = new QWidget(this);
    auto* stdLay = new QHBoxLayout(m_standardView);
    stdLay->setContentsMargins(0, 0, 0, 0);
    stdLay->setSpacing(12);
    stdLay->addWidget(buildCenterCatalog(), 1);
    m_rightInspector = qobject_cast<QFrame*>(buildRightInspector());
    stdLay->addWidget(m_rightInspector, 0);
    m_mainStack->addWidget(m_standardView);

    m_fullscreenLyricsView = qobject_cast<QFrame*>(buildFullscreenLyricsPane());
    m_mainStack->addWidget(m_fullscreenLyricsView);

    m_settingsView = qobject_cast<QFrame*>(buildSettingsPane());
    m_mainStack->addWidget(m_settingsView);

    middleLay->addWidget(m_mainStack, 1);
    rootLay->addLayout(middleLay, 1);

    rootLay->addWidget(buildPlayerbar(), 0);

    m_debugOverlay = new DebugOverlayWidget(this);
    m_debugOverlay->setVisible(m_isDebugMode);
    m_debugOverlay->raise();
}

QWidget* LiquidGlassPlayer::buildTitlebar() {
    auto* bar = new QWidget(this);
    bar->setFixedHeight(30);
    auto* lay = new QHBoxLayout(bar);
    lay->setContentsMargins(6, 0, 2, 0);
    lay->setSpacing(6);

    auto* logo = new QLabel("✦ AETHER MUSIC", this);
    logo->setFont(QFont(FONT_FAM, 10, QFont::Bold));
    logo->setStyleSheet("color: #E2E8F0; letter-spacing: 2px;");
    lay->addWidget(logo);
    lay->addStretch();

    auto* btnMin = new WindowControlButton("min", this);
    connect(btnMin, &WindowControlButton::clicked, this, &QMainWindow::showMinimized);
    auto* btnMax = new WindowControlButton("max", this);
    connect(btnMax, &WindowControlButton::clicked, this, [this]() { isMaximized() ? showNormal() : showMaximized(); });
    auto* btnClose = new WindowControlButton("close", this);
    connect(btnClose, &WindowControlButton::clicked, this, &QMainWindow::close);

    lay->addWidget(btnMin);
    lay->addWidget(btnMax);
    lay->addWidget(btnClose);
    return bar;
}

QWidget* LiquidGlassPlayer::buildSidebar() {
    m_sidebarCard = new QFrame(this);
    m_sidebarCard->setFixedWidth(200);
    m_sidebarCard->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

    auto* lay = new QVBoxLayout(m_sidebarCard);
    lay->setContentsMargins(10, 12, 10, 12);
    lay->setSpacing(4);

    m_searchInput = new QLineEdit(this);
    m_searchInput->setPlaceholderText("Поиск...");
    m_searchInput->setStyleSheet("QLineEdit { background: rgba(255, 255, 255, 0.05); border: 1px solid rgba(255, 255, 255, 0.10); border-radius: 8px; padding: 7px 10px; color: #F8FAFC; font-size: 12px; } QLineEdit:focus { border-color: rgba(147, 197, 253, 0.55); }");
    connect(m_searchInput, &QLineEdit::textChanged, this, [this]() {
        filterTracks();
        if (m_currentNav == "vk" && m_searchDebounceTimer) {
            m_searchDebounceTimer->start();
        }
    });
    connect(m_searchInput, &QLineEdit::returnPressed, this, [this]() {
        if (m_currentNav == "vk") {
            if (m_searchDebounceTimer) m_searchDebounceTimer->stop();
            searchVkMusic();
        } else {
            searchServerMusic();
        }
    });
    lay->addWidget(m_searchInput);

    lay->addSpacing(8);
    m_lblSectionMedia = new QLabel("Медиатека", this);
    lay->addWidget(m_lblSectionMedia);

    m_btnNavAll = new QPushButton("Все треки", this);
    m_btnNavAll->setCheckable(true);
    m_btnNavAll->setChecked(true);
    connect(m_btnNavAll, &QPushButton::clicked, this, [this]() { switchCategory("all"); });
    lay->addWidget(m_btnNavAll);

    m_btnNavFav = new QPushButton("Любимое", this);
    m_btnNavFav->setCheckable(true);
    connect(m_btnNavFav, &QPushButton::clicked, this, [this]() { switchCategory("fav"); });
    lay->addWidget(m_btnNavFav);

    m_btnNavVk = new QPushButton("Музыка VK", this);
    m_btnNavVk->setCheckable(true);
    connect(m_btnNavVk, &QPushButton::clicked, this, [this]() {
        switchCategory("vk");
        if (m_vkTracks.isEmpty()) {
            loadVkMyMusic();
        }
    });
    lay->addWidget(m_btnNavVk);

    m_btnAddFiles = new QPushButton("+ Добавить треки", this);
    m_btnAddFiles->setStyleSheet("QPushButton { background: transparent; border: none; border-radius: 8px; color: #CBD5E1; text-align: left; padding: 9px 11px; font-size: 12px; } QPushButton:hover { background: rgba(255, 255, 255, 0.08); color: #F8FAFC; }");
    connect(m_btnAddFiles, &QPushButton::clicked, this, &LiquidGlassPlayer::loadTracksDialog);
    lay->addWidget(m_btnAddFiles);

    m_lblDragHint = new QLabel("или перетащите файлы", this);
    m_lblDragHint->setStyleSheet("color: #94A3B8; padding: 2px 11px; font-size: 10px; text-transform: none;");
    lay->addWidget(m_lblDragHint);

    lay->addSpacing(8);
    m_lblSectionSystem = new QLabel("Система", this);
    lay->addWidget(m_lblSectionSystem);

    m_btnNavSettings = new QPushButton("Настройки", this);
    m_btnNavSettings->setCheckable(true);
    connect(m_btnNavSettings, &QPushButton::clicked, this, [this]() { switchCategory("settings"); });
    lay->addWidget(m_btnNavSettings);

    lay->addStretch();
    m_lblStats = new QLabel("0 треков", this);
    m_lblStats->setStyleSheet("color: #CBD5E1; font-size: 10px; text-transform: none;");
    lay->addWidget(m_lblStats);

    return m_sidebarCard;
}

QWidget* LiquidGlassPlayer::buildCenterCatalog() {
    m_centerCard = new QFrame(this);
    m_centerCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_centerCard->setStyleSheet("QFrame { background: rgba(14, 18, 26, 0.40); border: 1px solid rgba(255, 255, 255, 0.08); border-radius: 18px; }");

    auto* lay = new QVBoxLayout(m_centerCard);
    lay->setContentsMargins(8, 8, 8, 8);

    m_table = new CleanTrackTable(this);
    m_tableModel = new TrackTableModel(this);
    m_tableDelegate = new PureTextDelegate(this);
    m_table->setModel(m_tableModel);
    m_table->setItemDelegate(m_tableDelegate);
    m_table->setSingleClickPlay(m_singleClickPlay);

    auto* h = m_table->horizontalHeader();
    h->setFixedHeight(32);
    h->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    h->setSectionResizeMode(0, QHeaderView::Fixed); h->resizeSection(0, 42);
    h->setSectionResizeMode(1, QHeaderView::Stretch);
    h->setSectionResizeMode(2, QHeaderView::Interactive); h->resizeSection(2, 180);
    h->setSectionResizeMode(3, QHeaderView::Interactive); h->resizeSection(3, 150);
    h->setSectionResizeMode(4, QHeaderView::Fixed); h->resizeSection(4, 64);

    connect(m_table, &CleanTrackTable::rowActivated, this, [this](int r) {
        if (r >= 0 && r < m_displayTracks.size()) {
            auto& activeColl = currentCollection();
            const int realIdx = activeColl.indexOf(m_displayTracks[r]);
            if (realIdx != -1) playTrack(realIdx, true);
        }
    });

    connect(m_table, &CleanTrackTable::contextMenuRequested, this, &LiquidGlassPlayer::showTrackContextMenu);

    lay->addWidget(m_table);
    return m_centerCard;
}

void LiquidGlassPlayer::showTrackContextMenu(int row, const QPoint& globalPos) {
    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu {"
        "   background-color: rgba(24, 27, 38, 0.96); border: 1px solid rgba(255, 255, 255, 0.12); border-radius: 10px; padding: 6px; color: #F8FAFC; font-family: 'Segoe UI'; font-size: 12px;"
        "}"
        "QMenu::item { padding: 8px 24px 8px 12px; border-radius: 6px; }"
        "QMenu::item:selected { background-color: rgba(239, 68, 68, 0.25); color: #FCA5A5; }"
    );

    QAction* deleteAction = menu.addAction("🗑 Удалить из медиатеки");
    QAction* selectedAction = menu.exec(globalPos);

    if (selectedAction == deleteAction) {
        deleteTrackAtRow(row);
    }
}

void LiquidGlassPlayer::deleteTrackAtRow(int row) {
    if (row < 0 || row >= m_displayTracks.size()) return;

    const AudioTrack target = m_displayTracks[row];
    auto& activeColl = currentCollection();
    const int realIdx = activeColl.indexOf(target);
    if (realIdx != -1) {
        activeColl.removeAt(realIdx);
    }

    if (m_currentNav != "vk") saveLibraryState();

    if (m_currIdx == realIdx) {
        if (activeColl.isEmpty()) {
            stopAndUnloadSound();
            m_currIdx = -1;
            m_currentPlayingPath.clear();
            m_lblBarTitle->setText("Не играет");
            m_lblBarArtist->setText("—");
            m_inspTitle->setText("Трек не выбран");
            m_inspArtist->setText("—");
            m_inspKaraoke->setLyrics("");
            m_fsKaraoke->setLyrics("");
            setCoverPlaceholder(m_miniCover, 50, 10);
            m_vinylWidget->setCover(QPixmap());
            m_vinylWidget->setPlaying(false);
            if (m_smtc) m_smtc->updatePlaybackState(false);
        } else {
            m_currIdx = std::min<int>(m_currIdx, static_cast<int>(activeColl.size() - 1));
            playTrack(m_currIdx, true);
        }
    } else if (m_currIdx > realIdx) {
        m_currIdx--;
    }

    filterTracks();
    wakeEngineLoop();
}

QWidget* LiquidGlassPlayer::buildRightInspector() {
    auto* card = new QFrame(this);
    card->setFixedWidth(290);
    card->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    card->setStyleSheet("QFrame { background: rgba(18, 22, 32, 0.45); border: 1px solid rgba(255, 255, 255, 0.10); border-radius: 18px; }");

    auto* lay = new QVBoxLayout(card);
    lay->setContentsMargins(14, 14, 14, 14);
    lay->setSpacing(6);

    m_vinylWidget = new VinylCoverWidget(this);
    lay->addWidget(m_vinylWidget, 0);

    m_inspTitle = new MarqueeLabel("Трек не выбран", this);
    m_inspTitle->setFixedHeight(22);
    m_inspTitle->setColor(QColor("#F1F5F9"));

    m_inspArtist = new MarqueeLabel("—", this);
    m_inspArtist->setFixedHeight(18);
    m_inspArtist->setColor(QColor("#CBD5E1"));

    lay->addWidget(m_inspTitle);
    lay->addWidget(m_inspArtist);
    lay->addSpacing(4);

    auto* lyrHeader = new QHBoxLayout();
    m_lblInspLyrHeader = new QLabel("ТЕКСТ ПЕСНИ", this);
    m_lblInspLyrHeader->setStyleSheet("color: #CBD5E1; font-size: 9px; letter-spacing: 0.7px; background: transparent; border: none;");

    m_btnOpenFullLyr = new SmoothSpringButton("fullscreen", 28, false, this);
    m_allSpringButtons.append(m_btnOpenFullLyr);
    connect(m_btnOpenFullLyr, &SmoothSpringButton::clicked, this, &LiquidGlassPlayer::toggleFullscreenLyrics);

    lyrHeader->addWidget(m_lblInspLyrHeader);
    lyrHeader->addStretch();
    lyrHeader->addWidget(m_btnOpenFullLyr);
    lay->addLayout(lyrHeader);

    m_inspKaraoke = new SmoothKaraokeWidget(false, m_isBoldFont, this);
    connect(m_inspKaraoke, &SmoothKaraokeWidget::seekRequested, this, &LiquidGlassPlayer::seekToMs);
    lay->addWidget(m_inspKaraoke, 1);

    return card;
}

QWidget* LiquidGlassPlayer::buildFullscreenLyricsPane() {
    auto* card = new QFrame(this);
    card->setStyleSheet("QFrame { background: rgba(18, 22, 32, 0.55); border: 1px solid rgba(255, 255, 255, 0.12); border-radius: 20px; }");
    auto* lay = new QVBoxLayout(card);
    lay->setContentsMargins(36, 20, 36, 20);

    auto* top = new QHBoxLayout();
    m_fsTitle = new QLabel("Не играет", this);
    m_fsTitle->setStyleSheet("color: #FFFFFF; background: transparent; border: none;");

    auto* btnClose = new QPushButton("✕ Закрыть", this);
    btnClose->setStyleSheet("QPushButton { background: rgba(255, 255, 255, 0.08); border: 1px solid rgba(255, 255, 255, 0.14); border-radius: 10px; color: #E2E8F0; font-size: 11px; font-weight: 600; padding: 6px 14px; } QPushButton:hover { background: rgba(255, 255, 255, 0.16); }");
    connect(btnClose, &QPushButton::clicked, this, &LiquidGlassPlayer::toggleFullscreenLyrics);

    top->addWidget(m_fsTitle);
    top->addStretch();
    top->addWidget(btnClose);
    lay->addLayout(top);

    m_fsKaraoke = new SmoothKaraokeWidget(true, m_isBoldFont, this);
    connect(m_fsKaraoke, &SmoothKaraokeWidget::seekRequested, this, &LiquidGlassPlayer::seekToMs);
    lay->addWidget(m_fsKaraoke, 1);

    return card;
}

QWidget* LiquidGlassPlayer::buildSettingsPane() {
    auto* card = new QFrame(this);
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    card->setStyleSheet("QFrame { background: rgba(18, 22, 32, 0.45); border: 1px solid rgba(255, 255, 255, 0.10); border-radius: 18px; }");
    auto* lay = new QVBoxLayout(card);
    lay->setContentsMargins(32, 28, 32, 28);
    lay->setSpacing(14);

    auto* top = new QHBoxLayout();
    auto* btnBack = new QPushButton("← Назад", this);
    btnBack->setStyleSheet("QPushButton { background: rgba(255, 255, 255, 0.06); border: 1px solid rgba(255, 255, 255, 0.10); border-radius: 10px; color: #E2E8F0; font-size: 12px; font-weight: 600; padding: 7px 16px; } QPushButton:hover { background: rgba(255, 255, 255, 0.12); }");
    connect(btnBack, &QPushButton::clicked, this, [this]() { switchCategory(m_prevNav != "settings" ? m_prevNav : "all"); });

    auto* title = new QLabel("Настройки", this);
    title->setFont(QFont(FONT_FAM, 16, QFont::Bold));
    title->setStyleSheet("color: #FFFFFF; background: transparent; border: none;");

    top->addWidget(btnBack);
    top->addWidget(title);
    top->addStretch();
    lay->addLayout(top);
    lay->addSpacing(4);

    auto makeRow = [this](const QString& h, const QString& d, QWidget* ctrl) {
        auto* rowCard = new QFrame(this);
        rowCard->setFixedHeight(68);
        rowCard->setStyleSheet("QFrame { background: rgba(255, 255, 255, 0.05); border: 1px solid rgba(255, 255, 255, 0.08); border-radius: 14px; }");
        auto* rowLay = new QHBoxLayout(rowCard);
        rowLay->setContentsMargins(18, 0, 18, 0);

        auto* infoV = new QVBoxLayout();
        infoV->setSpacing(2);
        infoV->setAlignment(Qt::AlignVCenter);

        auto* lblH = new QLabel(h, this);
        lblH->setFont(QFont(FONT_FAM, 12, QFont::DemiBold));
        lblH->setStyleSheet("color: #F8FAFC; background: transparent; border: none;");
        auto* lblD = new QLabel(d, this);
        lblD->setStyleSheet("color: #CBD5E1; font-size: 10px; background: transparent; border: none;");
        infoV->addWidget(lblH);
        infoV->addWidget(lblD);

        rowLay->addLayout(infoV);
        rowLay->addStretch();
        rowLay->addWidget(ctrl);
        return rowCard;
    };

    m_toggleBold = new GlassToggleSwitch(m_isBoldFont, this);
    connect(m_toggleBold, &GlassToggleSwitch::toggled, this, [this](bool state) {
        m_isBoldFont = state;
        saveSettings();
        applyGlobalFonts();
    });
    lay->addWidget(makeRow("Жирный шрифт для всего интерфейса", "Применяет жирное начертание ко всем текстам и спискам", m_toggleBold));

    m_toggleSingleClick = new GlassToggleSwitch(m_singleClickPlay, this);
    connect(m_toggleSingleClick, &GlassToggleSwitch::toggled, this, [this](bool state) {
        m_singleClickPlay = state;
        if (m_table) m_table->setSingleClickPlay(state);
        saveSettings();
    });
    lay->addWidget(makeRow("Воспроизведение в один клик", "Запускает трек по одиночному нажатию вместо двойного", m_toggleSingleClick));

    auto* brightControls = new QWidget(this);
    auto* brightControlsLay = new QHBoxLayout(brightControls);
    brightControlsLay->setContentsMargins(0, 0, 0, 0);
    brightControlsLay->setSpacing(10);
    brightControlsLay->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_sliderBrightness = new FluidClickSlider(Qt::Horizontal, this);
    m_sliderBrightness->setRange(0, 100);
    m_sliderBrightness->setValue(static_cast<int>(m_bgBrightness * 100));
    m_sliderBrightness->setFixedWidth(130);
    connect(m_sliderBrightness, &FluidClickSlider::valueChanged, this, &LiquidGlassPlayer::onBrightnessChanged);

    m_lblBrightnessVal = new QLabel(QString("%1%").arg(static_cast<int>(m_bgBrightness * 100)), this);
    m_lblBrightnessVal->setFont(QFont(FONT_FAM, 11, QFont::DemiBold));
    m_lblBrightnessVal->setStyleSheet("color: #93C5FD; background: transparent; border: none;");
    m_lblBrightnessVal->setFixedWidth(40);
    m_lblBrightnessVal->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    brightControlsLay->addWidget(m_sliderBrightness);
    brightControlsLay->addWidget(m_lblBrightnessVal);
    lay->addWidget(makeRow("Яркость фона", "Регулирует насыщенность и свечение световых градиентов", brightControls));

    m_toggleDebug = new GlassToggleSwitch(m_isDebugMode, this);
    connect(m_toggleDebug, &GlassToggleSwitch::toggled, this, &LiquidGlassPlayer::onDebugToggled);
    lay->addWidget(makeRow("Режим отладки (Debug HUD)", "Отображает плавающее окно со счетчиками FPS, CPU и GPU", m_toggleDebug));

    auto* rowVkCard = new QFrame(this);
    rowVkCard->setFixedHeight(76);
    rowVkCard->setStyleSheet("QFrame { background: rgba(255, 255, 255, 0.05); border: 1px solid rgba(255, 255, 255, 0.08); border-radius: 14px; }");
    auto* rowVkLay = new QHBoxLayout(rowVkCard);
    rowVkLay->setContentsMargins(18, 0, 18, 0);

    auto* infoVkV = new QVBoxLayout();
    infoVkV->setSpacing(2);
    infoVkV->setAlignment(Qt::AlignVCenter);
    auto* lblVk = new QLabel("VK Access Token (Kate Mobile)", this);
    lblVk->setFont(QFont(FONT_FAM, 12, QFont::DemiBold));
    lblVk->setStyleSheet("color: #F8FAFC; background: transparent; border: none;");

    m_lblVkStatus = new QLabel(m_vkToken.isEmpty() ? "Токен не указан" : "Токен сохранен", this);
    m_lblVkStatus->setStyleSheet("color: #CBD5E1; font-size: 11px; background: transparent; border: none;");
    infoVkV->addWidget(lblVk);
    infoVkV->addWidget(m_lblVkStatus);

    auto* vkControls = new QWidget(this);
    auto* vkControlsLay = new QHBoxLayout(vkControls);
    vkControlsLay->setContentsMargins(0, 0, 0, 0);
    vkControlsLay->setSpacing(10);
    vkControlsLay->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_inputVkToken = new QLineEdit(this);
    m_inputVkToken->setText(m_vkToken);
    m_inputVkToken->setPlaceholderText("Вставьте access_token...");
    m_inputVkToken->setEchoMode(QLineEdit::Password);
    m_inputVkToken->setFixedWidth(160);
    m_inputVkToken->setStyleSheet("QLineEdit { background: rgba(255, 255, 255, 0.08); border: 1px solid rgba(255, 255, 255, 0.12); border-radius: 8px; padding: 6px 10px; color: #FFFFFF; font-size: 11px; }");
    connect(m_inputVkToken, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_vkToken = text.trimmed();
        saveSettings();
    });

    m_btnSyncVk = new QPushButton("Синхронизировать", this);
    m_btnSyncVk->setStyleSheet("QPushButton { background: rgba(147, 197, 253, 0.20); border: 1px solid rgba(147, 197, 253, 0.40); border-radius: 8px; color: #93C5FD; font-size: 11px; font-weight: 600; padding: 6px 12px; } QPushButton:hover { background: rgba(147, 197, 253, 0.30); color: #FFFFFF; }");
    connect(m_btnSyncVk, &QPushButton::clicked, this, &LiquidGlassPlayer::testAndSyncVk);

    vkControlsLay->addWidget(m_inputVkToken);
    vkControlsLay->addWidget(m_btnSyncVk);

    rowVkLay->addLayout(infoVkV);
    rowVkLay->addStretch();
    rowVkLay->addWidget(vkControls);
    lay->addWidget(rowVkCard);

    lay->addStretch();

    auto* lblVersionInfo = new QLabel("C++ WASAPI Direct Engine • LRCLIB Auto-Karaoke", this);
    lblVersionInfo->setFont(QFont(FONT_FAM, 10, QFont::DemiBold));
    lblVersionInfo->setStyleSheet("color: #CBD5E1; background: transparent; border: none;");
    lblVersionInfo->setAlignment(Qt::AlignCenter);
    lay->addWidget(lblVersionInfo);

    return card;
}

QWidget* LiquidGlassPlayer::buildPlayerbar() {
    m_playerBarCard = new QFrame(this);
    m_playerBarCard->setFixedHeight(88);
    m_playerBarCard->setStyleSheet("QFrame { background: rgba(18, 22, 32, 0.65); border: 1px solid rgba(255, 255, 255, 0.14); border-radius: 24px; }");

    auto* lay = new QHBoxLayout(m_playerBarCard);
    lay->setContentsMargins(18, 8, 18, 8);
    lay->setSpacing(14);
    lay->setAlignment(Qt::AlignVCenter);

    auto* leftBox = new QWidget(this);
    leftBox->setFixedWidth(260);
    auto* leftLay = new QHBoxLayout(leftBox);
    leftLay->setContentsMargins(0, 0, 0, 0);
    leftLay->setSpacing(12);
    leftLay->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    m_miniCover = new QLabel(this);
    m_miniCover->setFixedSize(50, 50);
    setCoverPlaceholder(m_miniCover, 50, 10);
    leftLay->addWidget(m_miniCover);

    auto* metaV = new QVBoxLayout();
    metaV->setSpacing(2);
    metaV->setContentsMargins(0, 0, 0, 0);
    metaV->setAlignment(Qt::AlignVCenter);

    m_lblBarTitle = new MarqueeLabel("Не играет", this);
    m_lblBarTitle->setFixedHeight(18);
    m_lblBarTitle->setColor(QColor("#FFFFFF"));

    m_lblBarArtist = new MarqueeLabel("—", this);
    m_lblBarArtist->setFixedHeight(16);
    m_lblBarArtist->setColor(QColor("#CBD5E1"));

    metaV->addWidget(m_lblBarTitle);
    metaV->addWidget(m_lblBarArtist);
    leftLay->addLayout(metaV);
    lay->addWidget(leftBox, 0);

    auto* centerBox = new QWidget(this);
    centerBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* centerV = new QVBoxLayout(centerBox);
    centerV->setContentsMargins(0, 0, 0, 0);
    centerV->setSpacing(4);
    centerV->setAlignment(Qt::AlignCenter);

    auto* btnRow = new QHBoxLayout();
    btnRow->setAlignment(Qt::AlignCenter);
    btnRow->setSpacing(12);

    m_btnShuf = new SmoothSpringButton("shuffle", 32, true, this);
    m_allSpringButtons.append(m_btnShuf);
    connect(m_btnShuf, &SmoothSpringButton::clicked, this, &LiquidGlassPlayer::toggleShuffle);

    m_btnPrev = new SmoothSpringButton("prev", 34, false, this);
    m_allSpringButtons.append(m_btnPrev);
    connect(m_btnPrev, &SmoothSpringButton::clicked, this, &LiquidGlassPlayer::prevTrack);

    m_btnPlay = new SmoothSpringButton("play_big", 44, false, this);
    m_allSpringButtons.append(m_btnPlay);
    connect(m_btnPlay, &SmoothSpringButton::clicked, this, &LiquidGlassPlayer::togglePlay);

    m_btnNext = new SmoothSpringButton("next", 34, false, this);
    m_allSpringButtons.append(m_btnNext);
    connect(m_btnNext, &SmoothSpringButton::clicked, this, &LiquidGlassPlayer::nextTrack);

    m_btnRep = new SmoothSpringButton("repeat", 32, true, this);
    m_allSpringButtons.append(m_btnRep);
    connect(m_btnRep, &SmoothSpringButton::clicked, this, &LiquidGlassPlayer::toggleRepeat);

    btnRow->addWidget(m_btnShuf, 0, Qt::AlignVCenter);
    btnRow->addWidget(m_btnPrev, 0, Qt::AlignVCenter);
    btnRow->addWidget(m_btnPlay, 0, Qt::AlignVCenter);
    btnRow->addWidget(m_btnNext, 0, Qt::AlignVCenter);
    btnRow->addWidget(m_btnRep, 0, Qt::AlignVCenter);
    centerV->addLayout(btnRow);

    auto* timeRow = new QHBoxLayout();
    timeRow->setContentsMargins(0, 0, 0, 0);
    timeRow->setSpacing(10);

    m_lblCurTime = new QLabel("0:00", this);
    m_lblCurTime->setStyleSheet("color: #CBD5E1; background: transparent; border: none;");

    m_sliderPos = new FluidClickSlider(Qt::Horizontal, this);
    m_sliderPos->setRange(0, 1000);
    connect(m_sliderPos, &FluidClickSlider::sliderMoved, this, &LiquidGlassPlayer::onSeekPreview);
    connect(m_sliderPos, &FluidClickSlider::sliderReleased, this, &LiquidGlassPlayer::onSeek);

    m_lblDurTime = new QLabel("0:00", this);
    m_lblDurTime->setStyleSheet("color: #CBD5E1; background: transparent; border: none;");

    timeRow->addWidget(m_lblCurTime, 0, Qt::AlignVCenter);
    timeRow->addWidget(m_sliderPos, 1, Qt::AlignVCenter);
    timeRow->addWidget(m_lblDurTime, 0, Qt::AlignVCenter);
    centerV->addLayout(timeRow);
    lay->addWidget(centerBox, 1);

    auto* rightBox = new QWidget(this);
    rightBox->setFixedWidth(290);
    auto* rightRow = new QHBoxLayout(rightBox);
    rightRow->setContentsMargins(0, 0, 0, 0);
    rightRow->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    rightRow->setSpacing(10);

    m_btnFav = new SmoothSpringButton("heart", 32, true, this);
    m_allSpringButtons.append(m_btnFav);
    connect(m_btnFav, &SmoothSpringButton::clicked, this, &LiquidGlassPlayer::toggleFavCurrent);

    m_btnToggleRight = new SmoothSpringButton("sidebar", 32, true, this);
    m_btnToggleRight->setChecked(true);
    m_allSpringButtons.append(m_btnToggleRight);
    connect(m_btnToggleRight, &SmoothSpringButton::clicked, this, [this]() {
        m_rightInspector->setVisible(m_btnToggleRight->isChecked());
    });

    m_btnCast = new SmoothSpringButton("cast", 32, true, this);
    m_allSpringButtons.append(m_btnCast);
    connect(m_btnCast, &SmoothSpringButton::clicked, this, [this]() {
        if (m_isCastingActive) {
            auto* menu = new QMenu(this);
            menu->setAttribute(Qt::WA_DeleteOnClose);
            menu->setStyleSheet(
                "QMenu {"
                "   background-color: rgba(24, 27, 38, 0.96); border: 1px solid rgba(255, 255, 255, 0.12); border-radius: 10px; padding: 6px; color: #F8FAFC; font-family: 'Segoe UI'; font-size: 12px;"
                "}"
                "QMenu::item { padding: 8px 20px; border-radius: 6px; }"
                "QMenu::item:selected { background-color: rgba(239, 68, 68, 0.25); color: #FCA5A5; }"
            );

            menu->addAction("⏹ Отключить трансляцию (" + m_activeTvDevice.name + ")", [this]() {
                m_dlna->stopCasting(m_activeTvDevice);
                m_isCastingActive = false;
                m_btnCast->setChecked(false);
                m_activityStatus = "Покой (0% CPU)";
            });
            menu->popup(QCursor::pos());
            return;
        }

        m_dlna->discoverDevices();

        auto* menu = new QMenu(this);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->setStyleSheet(
            "QMenu {"
            "   background-color: rgba(24, 27, 38, 0.96); border: 1px solid rgba(255, 255, 255, 0.12); border-radius: 10px; padding: 6px; color: #F8FAFC; font-family: 'Segoe UI'; font-size: 12px;"
            "}"
            "QMenu::item { padding: 8px 20px; border-radius: 6px; }"
            "QMenu::item:selected { background-color: rgba(147, 197, 253, 0.20); color: #93C5FD; }"
        );

        auto populateMenu = [this, menu]() {
            menu->clear();
            const auto& devices = m_dlna->foundDevices();
            if (devices.isEmpty()) {
                QAction* searching = menu->addAction("🔍 Поиск Smart TV в сети...");
                searching->setEnabled(false);
            } else {
                for (const auto& dev : devices) {
                    menu->addAction("📺 " + dev.name, [this, dev]() {
                        m_activeTvDevice = dev;
                        m_isCastingActive = true;
                        m_btnCast->setChecked(true);

                        auto& coll = currentCollection();
                        if (m_currIdx >= 0 && m_currIdx < coll.size()) {
                            if (m_soundLoaded && ma_sound_is_playing(&m_sound)) {
                                fadeDspToSilence();
                                ma_sound_stop(&m_sound);
                            }
                            m_btnPlay->setIconType("pause_big");
                            m_vinylWidget->setPlaying(true);

                            const auto& tr = coll[m_currIdx];
                            const qreal dpr = devicePixelRatioF();
                            QPixmap pm = CoverCache::instance().get(tr.path);
                            if (pm.isNull() && !tr.path.startsWith("http")) {
                                pm = TrackLoaderWorker::loadCoverDirect(tr.path, 500, dpr);
                            }

                            QByteArray coverBytes;
                            if (!pm.isNull()) {
                                QBuffer buf(&coverBytes);
                                buf.open(QIODevice::WriteOnly);
                                pm.save(&buf, "JPG", 85);
                            }
                            m_dlna->castTrack(m_activeTvDevice, tr.path, tr.title, tr.artist, coverBytes);
                        }
                    });
                }
            }
        };

        populateMenu();

        auto conn = connect(m_dlna, &DlnaCastService::deviceFound, menu, [populateMenu](const DlnaDevice&) {
            populateMenu();
        });
        connect(menu, &QMenu::destroyed, this, [conn]() {
            QObject::disconnect(conn);
        });

        menu->popup(QCursor::pos());
    });

    m_iconVol = new SmoothSpringButton("volume", 30, false, this);
    m_allSpringButtons.append(m_iconVol);
    connect(m_iconVol, &SmoothSpringButton::clicked, this, &LiquidGlassPlayer::toggleMute);

    m_sliderVol = new FluidClickSlider(Qt::Horizontal, this);
    m_sliderVol->setRange(0, 100);
    m_sliderVol->setValue(80);
    m_sliderVol->setFixedWidth(80);
    connect(m_sliderVol, &FluidClickSlider::valueChanged, this, &LiquidGlassPlayer::onVolumeChanged);

    rightRow->addWidget(m_btnFav);
    rightRow->addWidget(m_btnToggleRight);
    rightRow->addWidget(m_btnCast);
    rightRow->addWidget(m_iconVol);
    rightRow->addWidget(m_sliderVol);
    lay->addWidget(rightBox, 0);

    return m_playerBarCard;
}

void LiquidGlassPlayer::masterTick() {
    double dt = m_tickTimer.restart() / 1000.0;
    if (dt > 0.1) dt = 0.016;

    bool isPlaying = false;
    if (m_soundLoaded) {
        isPlaying = ma_sound_is_playing(&m_sound);
        const bool isAtEnd = ma_sound_at_end(&m_sound);

        if (isAtEnd) {
            if (m_repeatMode == 1) {
                m_sound.processingCacheFramesRemaining = 0;
                ma_sound_seek_to_pcm_frame(&m_sound, 0);
                startDspFadeIn();
                ma_sound_start(&m_sound);
                m_btnPlay->setIconType("pause_big");
                m_vinylWidget->setPlaying(true);
                if (m_smtc) m_smtc->updatePlaybackState(true);
                if (m_currIdx >= 0 && m_currIdx < currentCollection().size()) {
                    m_tableModel->setCurrentPlayingPath(currentCollection()[m_currIdx].path, true);
                }
            } else {
                nextTrack();
            }
            return;
        } else if (isPlaying) {
            m_activityStatus = "Воспроизведение (WASAPI Direct)";

            ma_uint64 cursorFrame;
            ma_uint32 sampleRate;
            ma_sound_get_data_format(&m_sound, nullptr, nullptr, &sampleRate, nullptr, 0);
            if (ma_sound_get_cursor_in_pcm_frames(&m_sound, &cursorFrame) == MA_SUCCESS && sampleRate > 0) {
                const qint64 posMs = (cursorFrame * 1000) / sampleRate;
                onPositionChanged(posMs);
            }
        } else {
            m_activityStatus = m_isCastingActive ? ("Трансляция на: " + m_activeTvDevice.name) : "Покой (0% CPU)";
        }
    } else {
        m_activityStatus = m_isCastingActive ? ("Трансляция на: " + m_activeTvDevice.name) : "Покой (0% CPU)";
    }

    if (isHidden() || isMinimized()) {
        if (m_engineTimer->interval() != 150) m_engineTimer->setInterval(150);
        return;
    }

    m_phase += dt;

    if (m_tableDelegate) m_tableDelegate->setPhase(m_phase);
    if (isPlaying && m_table && m_tableModel) {
        const int pRow = m_tableModel->playingRow();
        if (pRow >= 0) {
            m_table->viewport()->update(m_table->visualRect(m_tableModel->index(pRow, 0)));
        }
    }

    const QPoint localPos = mapFromGlobal(QCursor::pos());
    const double clampedX = std::clamp(static_cast<double>(localPos.x()), 0.0, static_cast<double>(width()));
    const double clampedY = std::clamp(static_cast<double>(localPos.y()), 0.0, static_cast<double>(height()));
    const double cx = width() / 2.0;
    const double cy = height() / 2.0;

    const double targetNormX = (cx > 0) ? (clampedX - cx) / cx : 0.0;
    const double targetNormY = (cy > 0) ? (clampedY - cy) / cy : 0.0;

    const double lerpFactor = 1.0 - std::exp(-14.0 * dt);
    const double dNormX = (targetNormX - m_mouseNormX) * lerpFactor;
    const double dNormY = (targetNormY - m_mouseNormY) * lerpFactor;
    const double dLocalX = (clampedX - m_mouseLocalX) * lerpFactor;
    const double dLocalY = (clampedY - m_mouseLocalY) * lerpFactor;

    m_mouseNormX += dNormX;
    m_mouseNormY += dNormY;
    m_mouseLocalX += dLocalX;
    m_mouseLocalY += dLocalY;

    auto lerpCol = [dt](const QColor& cur, const QColor& tar) {
        const double cLerp = 1.0 - std::exp(-6.0 * dt);
        return QColor(cur.red() + (tar.red() - cur.red()) * cLerp,
                      cur.green() + (tar.green() - cur.green()) * cLerp,
                      cur.blue() + (tar.blue() - cur.blue()) * cLerp);
    };

    const QColor oldC1 = m_c1, oldC2 = m_c2, oldC3 = m_c3;
    m_c1 = lerpCol(m_c1, m_targetC1);
    m_c2 = lerpCol(m_c2, m_targetC2);
    m_c3 = lerpCol(m_c3, m_targetC3);

    const bool colorsChanged = (oldC1 != m_c1 || oldC2 != m_c2 || oldC3 != m_c3);
    const bool mouseMoved = (std::abs(dLocalX) > 0.01 || std::abs(dLocalY) > 0.01);

    if (colorsChanged) {
        updateAccentColors(m_c1);
    }

    bool buttonsAnimating = false;
    for (auto* b : m_allSpringButtons) {
        if (b->updatePhysics()) buttonsAnimating = true;
    }

    const bool tableAnimating = (m_table && m_table->isVisible()) ? m_table->updatePillPhysics() : false;
    const bool vinylAnimating = (m_vinylWidget && m_vinylWidget->isVisible()) ? m_vinylWidget->updatePhysics(dt) : false;
    if (vinylAnimating) m_vinylWidget->update();

    const bool stackAnimating = m_mainStack && m_mainStack->isAnimating();
    m_glTickParity = (m_glTickParity + 1) & 1;

    const bool shouldUpdateGl = colorsChanged || mouseMoved || (isPlaying && m_glTickParity == 0);
    if (m_glCanvas && !stackAnimating && shouldUpdateGl) {
        m_glCanvas->setParams(static_cast<float>(m_phase),
                              static_cast<float>(m_mouseNormX),
                              static_cast<float>(m_mouseNormY),
                              static_cast<float>(m_mouseLocalX),
                              static_cast<float>(m_mouseLocalY),
                              m_c1, m_c2, m_c3, m_bgBrightness);
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (isPlaying || buttonsAnimating || tableAnimating || vinylAnimating || stackAnimating || underMouse() || (nowMs - m_lastPauseTime < 1500)) {
        if (m_engineTimer->interval() != 16) m_engineTimer->setInterval(16);
    } else {
        if (m_engineTimer->interval() != 150) m_engineTimer->setInterval(150);
    }

    m_fpsFrameCount++;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastFpsTime >= 350) {
        m_currentFps = (m_fpsFrameCount * 1000.0f) / (now - m_lastFpsTime);
        m_fpsFrameCount = 0;
        m_lastFpsTime = now;

        if (m_isDebugMode && m_debugOverlay) {
            const float cpu = getProcessCpuUsage();
            const float gpuMs = m_glCanvas ? m_glCanvas->lastRenderTimeMs() : 0.00f;
            const QString status = QString("CPU: %1% | FPS: %2").arg(QString::number(cpu, 'f', 1)).arg(static_cast<int>(m_currentFps));
            m_debugOverlay->updateMetrics(m_currentFps, cpu, gpuMs, status);
        }
    }
}

void LiquidGlassPlayer::renderTable() {
    m_lblStats->setText(QString("%1 треков").arg(m_displayTracks.size()));
    m_tableModel->setTracks(m_displayTracks);
    syncTableActiveRow();
}

void LiquidGlassPlayer::playTrack(int idx, bool autoStart) {
    auto& activeColl = currentCollection();
    if (idx < 0 || idx >= activeColl.size() || !m_engineInit) return;
    m_currIdx = idx;
    auto& tr = activeColl[idx];
    const QString originalPath = tr.path;
    m_currentPlayingPath = originalPath;

    traceEngine("AUDIO", QString("Track select: #%1 '%2 - %3'").arg(idx + 1).arg(tr.artist, tr.title));

    stopAndUnloadSound();
    m_sliderPos->setProgress(0.0);
    m_lastDisplayedSec = 0;
    m_lblCurTime->setText("0:00");
    m_lblBarTitle->setText(tr.title);
    m_lblBarArtist->setText(tr.artist);
    m_inspTitle->setText(tr.title);
    m_inspArtist->setText(tr.artist);
    m_fsTitle->setText(tr.artist + " — " + tr.title);
    m_tableModel->setCurrentPlayingPath(tr.path, false);
    syncTableActiveRow();

    if (tr.path.startsWith("http://") || tr.path.startsWith("https://")) {
        traceEngine("NET", "Streaming remote audio resource");
        m_lblDurTime->setText("Загрузка...");
        m_btnPlay->setIconType("play_big");
        m_vinylWidget->setPlaying(false);

        auto* dlWorker = new StreamDownloadWorker(tr.path, this);
        QPointer<LiquidGlassPlayer> self(this);

        connect(dlWorker, &StreamDownloadWorker::downloaded, this, [self, originalPath, autoStart, dlWorker](const QString& localPath) {
            if (!self) {
                dlWorker->deleteLater();
                return;
            }
            auto& coll = self->currentCollection();
            int resolvedIdx = -1;
            for (int i = 0; i < coll.size(); ++i) {
                if (coll[i].path == originalPath) {
                    resolvedIdx = i;
                    break;
                }
            }

            if (resolvedIdx != -1) {
                coll[resolvedIdx].path = localPath;
                if (self->m_currentPlayingPath == originalPath) {
                    self->m_currentPlayingPath = localPath;
                }

                (void)QtConcurrent::run([self, localPath, resolvedIdx, autoStart, dlWorker]() {
                    const AudioTrack meta = TrackLoaderWorker::parseFileFast(localPath);
                    QMetaObject::invokeMethod(qApp, [self, localPath, resolvedIdx, meta, autoStart, dlWorker]() {
                        if (self) {
                            auto& currentColl = self->currentCollection();
                            if (resolvedIdx < currentColl.size() && currentColl[resolvedIdx].path == localPath) {
                                if (!meta.title.isEmpty() && meta.title != QFileInfo(localPath).baseName()) {
                                    currentColl[resolvedIdx].title = meta.title;
                                    currentColl[resolvedIdx].artist = meta.artist;
                                    currentColl[resolvedIdx].album = meta.album;
                                }
                                currentColl[resolvedIdx].durationMs = meta.durationMs;
                                currentColl[resolvedIdx].durationStr = meta.durationStr;
                                currentColl[resolvedIdx].lyrics = meta.lyrics;

                                self->filterTracks();
                                if (self->m_currentNav != "vk") self->saveLibraryState();

                                if (self->m_currIdx == resolvedIdx && self->m_currentPlayingPath == localPath) {
                                    self->playTrack(resolvedIdx, autoStart);
                                }
                            }
                        }
                        dlWorker->deleteLater();
                    });
                });
            } else {
                dlWorker->deleteLater();
            }
        });

        connect(dlWorker, &StreamDownloadWorker::errorOccurred, this, [this, dlWorker](const QString& err) {
            traceEngine("NET", "Stream download failed: " + err);
            m_lblDurTime->setText("Ошибка");
            dlWorker->deleteLater();
        });

        dlWorker->start();
        return;
    }

    const ma_uint32 soundFlags = MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION | MA_SOUND_FLAG_NO_PITCH;
    const std::wstring wPath = QDir::toNativeSeparators(tr.path).toStdWString();
#if defined(Q_OS_WIN)
    const ma_result initRes = ma_sound_init_from_file_w(&m_audioEngine, wPath.c_str(), soundFlags, nullptr, nullptr, &m_sound);
#else
    const QByteArray localPath = QDir::toNativeSeparators(tr.path).toUtf8();
    const ma_result initRes = ma_sound_init_from_file(&m_audioEngine, localPath.constData(), soundFlags, nullptr, nullptr, &m_sound);
#endif

    if (initRes == MA_SUCCESS) {
        ma_sound_set_volume(&m_sound, 1.0f);

        if (tr.durationMs > 0) {
            m_currentDurationMs = tr.durationMs;
        } else {
            ma_uint64 lengthInFrames = 0;
            ma_uint32 sampleRate = 0;
            ma_sound_get_data_format(&m_sound, nullptr, nullptr, &sampleRate, nullptr, 0);
            if (ma_sound_get_length_in_pcm_frames(&m_sound, &lengthInFrames) == MA_SUCCESS && sampleRate > 0) {
                m_currentDurationMs = (lengthInFrames * 1000) / sampleRate;
            }
        }

        onDurationChanged(m_currentDurationMs);
        m_soundLoaded = true;

        if (!m_isCastingActive && autoStart) {
            startDspFadeIn();
            ma_sound_start(&m_sound);
        } else {
            m_dspGate.store(0.0f, std::memory_order_release);
            m_dspCurrentGain.store(0.0f, std::memory_order_release);
        }

        if (autoStart) {
            m_btnPlay->setIconType("pause_big");
            m_vinylWidget->setPlaying(true);
        } else {
            m_btnPlay->setIconType("play_big");
            m_vinylWidget->setPlaying(false);
        }
        m_soundLoaded = true;
    } else {
        traceEngine("AUDIO", QString("miniaudio open failure: %1 on '%2'").arg(initRes).arg(tr.path));
    }

    m_btnFav->setIconType(tr.isFav ? "heart_filled" : "heart");
    m_btnFav->setChecked(tr.isFav);

    const qreal dpr = devicePixelRatioF();
    QPixmap pm = CoverCache::instance().get(tr.path);
    if (pm.isNull() && !tr.path.startsWith("http")) {
        pm = TrackLoaderWorker::loadCoverDirect(tr.path, 500, dpr);
        if (!pm.isNull()) CoverCache::instance().put(tr.path, pm);
    }

    m_vinylWidget->resetAnimation();

    if (!pm.isNull()) {
        m_miniCover->setPixmap(roundPixmap(pm, 50, 10));
        m_vinylWidget->setCover(pm);

        QColor c1, c2, c3;
        extractColorsFromPixmap(pm, c1, c2, c3);
        m_targetC1 = c1;
        m_targetC2 = c2;
        m_targetC3 = c3;
    } else {
        setCoverPlaceholder(m_miniCover, 50, 10);
        m_vinylWidget->setCover(QPixmap());
        m_targetC1 = QColor(100, 116, 139);
        m_targetC2 = QColor(71, 85, 105);
        m_targetC3 = QColor(148, 163, 184);
    }

    if (m_smtc) {
        m_smtc->updateMetadata(tr.title, tr.artist, tr.album, pm);
        m_smtc->updatePlaybackState(autoStart);
        m_smtc->updateTimeline(0, m_currentDurationMs);
    }

    m_inspKaraoke->setLyrics(tr.lyrics);
    m_fsKaraoke->setLyrics(tr.lyrics);

    static const QRegularExpression syncCheckRegex(R"(\[\d{1,2}:\d{2})");
    const bool hasSync = tr.lyrics.contains(syncCheckRegex);

    if (!hasSync && !tr.title.isEmpty()) {
        const int durSec = static_cast<int>(m_currentDurationMs / 1000);
        const bool hadExistingText = !tr.lyrics.trimmed().isEmpty();
        traceEngine("LYRICS", QString("Querying LRCLIB караоке for '%1 - %2' (duration: %3s)").arg(tr.artist, tr.title).arg(durSec));

        auto* fetcher = new LyricsFetcherWorker(tr.path, tr.title, tr.artist, durSec, this);
        connect(fetcher, &LyricsFetcherWorker::lyricsFound, this, [this, fetcher, hadExistingText](const QString& trackPath, const QString& lyrics) {
            fetcher->deleteLater();
            if (lyrics.trimmed().isEmpty()) return;

            static const QRegularExpression syncRegex(R"(\[\d{1,2}:\d{2})");
            const bool isSynced = lyrics.contains(syncRegex);

            if (hadExistingText && !isSynced) {
                return;
            }

            auto& coll = currentCollection();
            for (int i = 0; i < coll.size(); ++i) {
                if (coll[i].path == trackPath) {
                    coll[i].lyrics = lyrics;
                    break;
                }
            }

            if (m_currIdx >= 0 && m_currIdx < coll.size() && coll[m_currIdx].path == trackPath) {
                m_inspKaraoke->setLyrics(lyrics);
                m_fsKaraoke->setLyrics(lyrics);
                traceEngine("LYRICS", "LRCLIB lyrics applied to current view");
            }

            if (m_currentNav != "vk") {
                saveLibraryState();
            }
        });
        fetcher->start();
    }

    if (m_isCastingActive) {
        QByteArray coverBytes;
        if (!pm.isNull()) {
            QBuffer buf(&coverBytes);
            buf.open(QIODevice::WriteOnly);
            pm.save(&buf, "JPG", 85);
        }
        m_dlna->castTrack(m_activeTvDevice, tr.path, tr.title, tr.artist, coverBytes);
    }

    m_tableModel->setCurrentPlayingPath(tr.path, autoStart);
    syncTableActiveRow();
    wakeEngineLoop();
}

void LiquidGlassPlayer::syncLibraryKaraoke() {
    static const QRegularExpression syncRegex(R"(\[\d{1,2}:\d{2})");
    int queueDelay = 0;

    for (int i = 0; i < m_tracks.size(); ++i) {
        const auto& tr = m_tracks[i];
        if (!tr.lyrics.contains(syncRegex) && !tr.title.isEmpty()) {
            const QString path = tr.path;
            const QString title = tr.title;
            const QString artist = tr.artist;
            const int durSec = static_cast<int>(tr.durationMs / 1000);
            const bool hadExistingText = !tr.lyrics.trimmed().isEmpty();

            QTimer::singleShot(queueDelay, this, [this, path, title, artist, durSec, hadExistingText]() {
                if (!m_engineInit) return;

                auto* fetcher = new LyricsFetcherWorker(path, title, artist, durSec, this);
                connect(fetcher, &LyricsFetcherWorker::lyricsFound, this, [this, fetcher, hadExistingText](const QString& trackPath, const QString& lyrics) {
                    fetcher->deleteLater();
                    if (lyrics.trimmed().isEmpty()) return;

                    static const QRegularExpression sRegex(R"(\[\d{1,2}:\d{2})");
                    const bool isSynced = lyrics.contains(sRegex);

                    if (hadExistingText && !isSynced) return;

                    if (isSynced) {
                        bool updated = false;
                        for (auto& track : m_tracks) {
                            if (track.path == trackPath) {
                                track.lyrics = lyrics;
                                updated = true;
                                break;
                            }
                        }

                        if (updated) {
                            if (m_currIdx >= 0 && m_currIdx < m_tracks.size() && m_tracks[m_currIdx].path == trackPath) {
                                m_inspKaraoke->setLyrics(lyrics);
                                m_fsKaraoke->setLyrics(lyrics);
                            }
                            saveLibraryState();
                            traceEngine("LYRICS", QString("Batch sync: updated karaoke for [%1]").arg(trackPath));
                        }
                    }
                });
                fetcher->start();
            });

            queueDelay += 350;
        }
    }
}

void LiquidGlassPlayer::togglePlay() {
    auto& activeColl = currentCollection();
    if (!m_soundLoaded) {
        if (!m_displayTracks.isEmpty()) {
            const int realIdx = activeColl.indexOf(m_displayTracks.first());
            if (realIdx != -1) playTrack(realIdx, true);
        } else if (!activeColl.isEmpty()) {
            playTrack(0, true);
        }
        return;
    }

    if (m_isCastingActive) {
        if (m_btnPlay->iconType() == "pause_big") {
            m_dlna->stopCasting(m_activeTvDevice);
            m_btnPlay->setIconType("play_big");
            m_vinylWidget->setPlaying(false);
        } else {
            if (m_currIdx >= 0 && m_currIdx < activeColl.size()) {
                playTrack(m_currIdx, true);
            }
            m_btnPlay->setIconType("pause_big");
            m_vinylWidget->setPlaying(true);
        }
        return;
    }

    if (ma_sound_is_playing(&m_sound)) {
        fadeDspToSilence();
        ma_sound_stop(&m_sound);
        m_lastPauseTime = QDateTime::currentMSecsSinceEpoch();
        m_btnPlay->setIconType("play_big");
        m_vinylWidget->setPlaying(false);
        if (m_smtc) m_smtc->updatePlaybackState(false);

        if (m_currIdx >= 0 && m_currIdx < activeColl.size()) {
            m_tableModel->setCurrentPlayingPath(activeColl[m_currIdx].path, false);
        }
    } else {
        startDspFadeIn();
        ma_sound_start(&m_sound);
        m_btnPlay->setIconType("pause_big");
        m_vinylWidget->setPlaying(true);
        if (m_smtc) m_smtc->updatePlaybackState(true);

        if (m_currIdx >= 0 && m_currIdx < activeColl.size()) {
            m_tableModel->setCurrentPlayingPath(activeColl[m_currIdx].path, true);
        }
    }
    wakeEngineLoop();
}

void LiquidGlassPlayer::prevTrack() {
    auto& activeColl = currentCollection();
    if (activeColl.isEmpty()) return;
    if (m_soundLoaded && !m_isCastingActive) {
        ma_uint64 cursorFrame;
        ma_uint32 sampleRate;
        ma_sound_get_data_format(&m_sound, nullptr, nullptr, &sampleRate, nullptr, 0);
        if (ma_sound_get_cursor_in_pcm_frames(&m_sound, &cursorFrame) == MA_SUCCESS && sampleRate > 0) {
            const qint64 posMs = (cursorFrame * 1000) / sampleRate;
            if (posMs > 3000) {
                seekToMs(0);
                return;
            }
        }
    }

    if (!m_displayTracks.isEmpty() && m_currIdx >= 0 && m_currIdx < activeColl.size()) {
        const int curDispIdx = m_displayTracks.indexOf(activeColl[m_currIdx]);
        if (curDispIdx != -1) {
            const int prevDispIdx = (curDispIdx - 1 + m_displayTracks.size()) % m_displayTracks.size();
            const int realIdx = activeColl.indexOf(m_displayTracks[prevDispIdx]);
            if (realIdx != -1) {
                playTrack(realIdx, true);
                return;
            }
        }
    }

    const int newIdx = (m_currIdx - 1 + activeColl.size()) % activeColl.size();
    playTrack(newIdx, true);
}

void LiquidGlassPlayer::nextTrack() {
    auto& activeColl = currentCollection();
    if (activeColl.isEmpty()) return;

    if (!m_displayTracks.isEmpty()) {
        int nextDispIdx = 0;
        if (m_isShuffled) {
            nextDispIdx = QRandomGenerator::global()->bounded(static_cast<int>(m_displayTracks.size()));
        } else if (m_currIdx >= 0 && m_currIdx < activeColl.size()) {
            const int curDispIdx = m_displayTracks.indexOf(activeColl[m_currIdx]);
            nextDispIdx = (curDispIdx != -1) ? ((curDispIdx + 1) % m_displayTracks.size()) : 0;
        }
        const int realIdx = activeColl.indexOf(m_displayTracks[nextDispIdx]);
        if (realIdx != -1) {
            playTrack(realIdx, true);
            return;
        }
    }

    const int newIdx = m_isShuffled
        ? QRandomGenerator::global()->bounded(static_cast<int>(activeColl.size()))
        : (m_currIdx + 1) % activeColl.size();
    playTrack(newIdx, true);
}

void LiquidGlassPlayer::toggleShuffle() { m_isShuffled = m_btnShuf->isChecked(); }
void LiquidGlassPlayer::toggleRepeat() {
    m_repeatMode = (m_repeatMode + 1) % 2;
    m_btnRep->setChecked(m_repeatMode == 1);
}

void LiquidGlassPlayer::toggleFavCurrent() {
    auto& activeColl = currentCollection();
    if (m_currIdx >= 0 && m_currIdx < activeColl.size()) {
        activeColl[m_currIdx].isFav = !activeColl[m_currIdx].isFav;
        m_btnFav->setIconType(activeColl[m_currIdx].isFav ? "heart_filled" : "heart");
        m_btnFav->setChecked(activeColl[m_currIdx].isFav);
        if (m_currentNav != "vk") saveLibraryState();
        filterTracks();
    }
}

void LiquidGlassPlayer::toggleMute() {
    if (m_sliderVol->value() > 0) {
        m_prevVol = m_sliderVol->value();
        m_sliderVol->setValue(0);
    } else {
        const int restoreVol = m_prevVol > 0 ? m_prevVol : 80;
        m_sliderVol->setValue(restoreVol);
    }
}

void LiquidGlassPlayer::onVolumeChanged(int val) {
    m_targetVolume.store(val / 100.0f, std::memory_order_relaxed);
    m_iconVol->setIconType(val == 0 ? "volume_mute" : "volume");
    wakeEngineLoop();
}

void LiquidGlassPlayer::seekToMs(qint64 ms) {
    if (!m_soundLoaded) return;

    qint64 totalDur = m_currentDurationMs;
    auto& activeColl = currentCollection();
    if (totalDur <= 0 && m_currIdx >= 0 && m_currIdx < activeColl.size()) {
        totalDur = activeColl[m_currIdx].durationMs;
    }

    if (totalDur > 0) {
        const qint64 clampedMs = std::clamp<qint64>(ms, 0, totalDur);
        ma_uint32 sampleRate = 0;
        ma_sound_get_data_format(&m_sound, nullptr, nullptr, &sampleRate, nullptr, 0);
        if (sampleRate > 0) {
            const bool wasPlaying = ma_sound_is_playing(&m_sound);
            if (wasPlaying) {
                fadeDspToSilence();
                ma_sound_stop(&m_sound);
            }
            m_sound.processingCacheFramesRemaining = 0;
            const ma_uint64 targetFrame = (clampedMs * sampleRate) / 1000;
            ma_sound_seek_to_pcm_frame(&m_sound, targetFrame);
            if (wasPlaying) {
                startDspFadeIn();
                ma_sound_start(&m_sound);
            }
            onPositionChanged(clampedMs);
            if (m_smtc) m_smtc->updateTimeline(clampedMs, totalDur);
        }
    }
    wakeEngineLoop();
}

void LiquidGlassPlayer::onSeekPreview(int val) {
    qint64 totalDur = m_currentDurationMs;
    auto& activeColl = currentCollection();
    if (totalDur <= 0 && m_currIdx >= 0 && m_currIdx < activeColl.size()) {
        totalDur = activeColl[m_currIdx].durationMs;
    }
    if (totalDur > 0) {
        const auto previewMs = static_cast<qint64>((val / 1000.0) * totalDur);
        m_lblCurTime->setText(formatTime(previewMs));
    }
}

void LiquidGlassPlayer::onSeek(int val) {
    qint64 totalDur = m_currentDurationMs;
    auto& activeColl = currentCollection();
    if (totalDur <= 0 && m_currIdx >= 0 && m_currIdx < activeColl.size()) {
        totalDur = activeColl[m_currIdx].durationMs;
    }
    if (totalDur > 0) {
        const auto targetMs = static_cast<qint64>((val / 1000.0) * totalDur);
        seekToMs(targetMs);
    }
}

void LiquidGlassPlayer::onPositionChanged(qint64 pos) {
    if (!m_sliderPos->isSliderDown() && m_currentDurationMs > 0) {
        const double progress = static_cast<double>(pos) / static_cast<double>(m_currentDurationMs);
        m_sliderPos->setProgress(progress);
    }
    const qint64 curSec = pos / 1000;
    if (!m_sliderPos->isSliderDown() && curSec != m_lastDisplayedSec) {
        m_lastDisplayedSec = curSec;
        m_lblCurTime->setText(formatTime(pos));
    }
    m_inspKaraoke->updatePosition(pos);
    m_fsKaraoke->updatePosition(pos);
}

void LiquidGlassPlayer::onDurationChanged(qint64 dur) {
    m_lblDurTime->setText(formatTime(dur));
    auto& activeColl = currentCollection();
    if (m_currIdx >= 0 && m_currIdx < activeColl.size()) {
        activeColl[m_currIdx].durationMs = dur;
        activeColl[m_currIdx].durationStr = formatTime(dur);
    }
}

void LiquidGlassPlayer::filterTracks() {
    const QString q = m_searchInput->text().trimmed().toLower();
    const bool favOnly = m_btnNavFav->isChecked();

    m_displayTracks.clear();
    const auto& activeColl = currentCollection();
    for (const auto& t : activeColl) {
        if (favOnly && !t.isFav) continue;
        if (!q.isEmpty() && !t.title.toLower().contains(q) && !t.artist.toLower().contains(q) && !t.album.toLower().contains(q)) {
            continue;
        }
        m_displayTracks.append(t);
    }
    renderTable();
}

void LiquidGlassPlayer::switchCategory(const QString& cat) {
    if (m_currentNav != "settings") m_prevNav = m_currentNav;
    m_currentNav = cat;
    m_btnNavAll->setChecked(cat == "all");
    m_btnNavFav->setChecked(cat == "fav");
    m_btnNavVk->setChecked(cat == "vk");
    m_btnNavSettings->setChecked(cat == "settings");

    if (cat == "settings") {
        m_mainStack->slideToPage(2);
    } else {
        m_mainStack->slideToPage(0);
        filterTracks();
    }
    wakeEngineLoop();
}

void LiquidGlassPlayer::toggleFullscreenLyrics() {
    m_isFullscreenLyrics = !m_isFullscreenLyrics;
    m_mainStack->slideToPage(m_isFullscreenLyrics ? 1 : 0);
    wakeEngineLoop();
}

void LiquidGlassPlayer::loadTracksDialog() {
    const QStringList files = QFileDialog::getOpenFileNames(this, "Добавить треки", "", "Audio Files (*.mp3 *.wav *.flac)");
    if (!files.isEmpty()) {
        traceEngine("IO", QString("Importing %1 audio files...").arg(files.size()));
        auto* loader = new TrackLoaderWorker(files, this);
        m_activeLoaders.append(loader);
        connect(loader, &TrackLoaderWorker::tracksLoaded, this, &LiquidGlassPlayer::onTracksBatchLoaded);
        connect(loader, &QThread::finished, loader, &QObject::deleteLater);
        loader->start();
        wakeEngineLoop();
    }
}

void LiquidGlassPlayer::onTracksBatchLoaded(const QVector<AudioTrack>& tracks) {
    for (const auto& tr : tracks) {
        if (!m_tracks.contains(tr)) {
            m_tracks.append(tr);
        } else {
            const int existingIdx = m_tracks.indexOf(tr);
            if (existingIdx != -1) {
                const bool wasFav = m_tracks[existingIdx].isFav;
                m_tracks[existingIdx] = tr;
                m_tracks[existingIdx].isFav = wasFav;
            }
        }
    }
    saveLibraryState();
    filterTracks();
    syncLibraryKaraoke();
    if (m_currIdx == -1 && !m_tracks.isEmpty()) playTrack(0, false);
    wakeEngineLoop();
}

void LiquidGlassPlayer::saveLibraryState() {
    QJsonArray arr;
    for (const auto& t : m_tracks) {
        QJsonObject obj;
        obj["path"] = t.path;
        obj["title"] = t.title;
        obj["artist"] = t.artist;
        obj["album"] = t.album;
        obj["duration_ms"] = t.durationMs;
        obj["duration_str"] = t.durationStr;
        obj["lyrics"] = t.lyrics;
        obj["is_fav"] = t.isFav;
        arr.append(obj);
    }
    QFile file("aether_library.json");
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(arr).toJson());
    }
}

void LiquidGlassPlayer::loadLibraryState() {
    QFile file("aether_library.json");
    if (file.open(QIODevice::ReadOnly)) {
        const auto doc = QJsonDocument::fromJson(file.readAll());
        if (doc.isArray()) {
            QVector<AudioTrack> loadedTracks;
            QStringList missingPaths;

            for (const auto val : doc.array()) {
                const auto obj = val.toObject();
                const QString p = obj.value("path").toString();
                if (QFile::exists(p) || p.startsWith("http")) {
                    AudioTrack tr;
                    tr.path = p;
                    tr.title = obj.value("title").toString(QFileInfo(p).baseName());
                    tr.artist = obj.value("artist").toString("Неизвестный исполнитель");
                    tr.album = obj.value("album").toString("Сингл");
                    tr.durationMs = obj.value("duration_ms").toInteger(0);
                    tr.durationStr = obj.value("duration_str").toString("0:00");
                    tr.lyrics = obj.value("lyrics").toString("");
                    tr.isFav = obj.value("is_fav").toBool(false);

                    tr.title.remove(QChar(0x0000));
                    tr.artist.remove(QChar(0x0000));

                    if (tr.durationStr == "0:00" && !p.startsWith("http")) {
                        missingPaths.append(p);
                    } else {
                        loadedTracks.append(tr);
                    }
                }
            }

            if (!loadedTracks.isEmpty()) {
                m_tracks = loadedTracks;
                filterTracks();
                syncLibraryKaraoke();
            }

            if (!missingPaths.isEmpty()) {
                auto* loader = new TrackLoaderWorker(missingPaths, this);
                m_activeLoaders.append(loader);
                connect(loader, &TrackLoaderWorker::tracksLoaded, this, &LiquidGlassPlayer::onTracksBatchLoaded);
                connect(loader, &QThread::finished, loader, &QObject::deleteLater);
                loader->start();
            }
        }
    }
}

void LiquidGlassPlayer::setCoverPlaceholder(QLabel* label, int size, int radius) {
    const qreal ratio = devicePixelRatioF();
    const int physSize = static_cast<int>(size * ratio);
    QPixmap pm(physSize, physSize);
    pm.setDevicePixelRatio(ratio);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(0, 0, size, size, radius, radius);
    p.setClipPath(path);

    QLinearGradient grad(0, 0, size, size);
    grad.setColorAt(0.0, QColor(42, 48, 64));
    grad.setColorAt(1.0, QColor(24, 28, 38));
    p.fillRect(0, 0, size, size, grad);

    p.setPen(QColor(226, 232, 240));
    p.setFont(QFont(FONT_FAM, static_cast<int>(size / 3.4), m_isBoldFont ? QFont::ExtraBold : QFont::Bold));
    p.drawText(QRect(0, 0, size, size), Qt::AlignCenter, "♪");
    p.end();
    label->setPixmap(pm);
}

QPixmap LiquidGlassPlayer::roundPixmap(const QPixmap& src, int size, int radius) {
    if (src.isNull()) return {};
    const qreal ratio = devicePixelRatioF();
    const int physSize = static_cast<int>(size * ratio);

    const QPixmap scaled = src.scaled(physSize, physSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    QPixmap out(physSize, physSize);
    out.setDevicePixelRatio(ratio);
    out.fill(Qt::transparent);

    QPainter p(&out);
    p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    QPainterPath path;
    path.addRoundedRect(0, 0, size, size, radius, radius);
    p.setClipPath(path);
    p.drawPixmap(0, 0, size, size, scaled);
    p.end();
    return out;
}

QString LiquidGlassPlayer::formatTime(qint64 ms) {
    const int s = static_cast<int>((ms / 1000) % 60);
    const int m = static_cast<int>(ms / 60000);
    return QString("%1:%2").arg(m).arg(s, 2, 10, QChar('0'));
}

void LiquidGlassPlayer::enterEvent(QEnterEvent*) {
    wakeEngineLoop();
}

void LiquidGlassPlayer::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
        wakeEngineLoop();
    }
}

void LiquidGlassPlayer::dragLeaveEvent(QDragLeaveEvent*) {}

void LiquidGlassPlayer::dropEvent(QDropEvent* event) {
    QStringList paths;
    const auto urls = event->mimeData()->urls();
    for (const auto& url : urls) {
        const QString local = url.toLocalFile();
        if (QFile::exists(local)) paths.append(local);
    }
    if (!paths.isEmpty()) {
        traceEngine("IO", QString("Drag-and-Drop: %1 files imported").arg(paths.size()));
        auto* loader = new TrackLoaderWorker(paths, this);
        m_activeLoaders.append(loader);
        connect(loader, &TrackLoaderWorker::tracksLoaded, this, &LiquidGlassPlayer::onTracksBatchLoaded);
        connect(loader, &QThread::finished, loader, &QObject::deleteLater);
        loader->start();
        wakeEngineLoop();
    }
}

void LiquidGlassPlayer::mousePressEvent(QMouseEvent* event) {
    wakeEngineLoop();
    if (event->button() == Qt::LeftButton && event->pos().y() < 42) {
        if (auto* handle = windowHandle()) handle->startSystemMove();
    }
}

void LiquidGlassPlayer::mouseMoveEvent(QMouseEvent* event) {
    wakeEngineLoop();
    QMainWindow::mouseMoveEvent(event);
}