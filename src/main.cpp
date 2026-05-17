#include <QApplication>
#include <QSurfaceFormat>
#include <QTimer>
#include "core/Types.h"
#include "ui/MainWindow.h"

int main(int argc, char* argv[])
{
    qRegisterMetaType<PlayerState>("PlayerState");

    QApplication app(argc, argv);
    app.setApplicationName("MediaPlayer");
    app.setApplicationVersion("1.0.0");

    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setSwapInterval(1);
    QSurfaceFormat::setDefaultFormat(fmt);

    MainWindow window;
    window.resize(1280, 720);
    window.show();

    const QStringList args = app.arguments();
    if (args.size() > 1) {
        QString path = args.mid(1).join(' ');
        if ((path.startsWith('"') && path.endsWith('"')) ||
            (path.startsWith('\'') && path.endsWith('\''))) {
            path = path.mid(1, path.size() - 2);
        }

        QTimer::singleShot(0, [&window, path] {
            window.openFile(path);
        });
    }

    return app.exec();
}
