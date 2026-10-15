#include <QApplication>
#include <QSurfaceFormat>
#include "LiquidGlassPlayer.h"

int main(int argc, char *argv[]) {
    qputenv("QT_ENABLE_HIGHDPI_SCALING", "1");

    QSurfaceFormat format;
    format.setDepthBufferSize(0);
    format.setStencilBufferSize(0);
    format.setSwapInterval(1);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication app(argc, argv);

    LiquidGlassPlayer player;
    player.show();

    return app.exec();
}