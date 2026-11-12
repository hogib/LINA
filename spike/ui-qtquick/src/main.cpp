// SPDX-License-Identifier: GPL-3.0-or-later
// LINA UI feasibility spike: Qt 6 Quick, custom scene graph items.
//   lina_ui_spike            windowed app (QQuickView)
//   lina_ui_spike --bench    headless benchmark, no window (see bench.cpp)
#include "bench.hpp"
#include "controller.hpp"

#include <QGuiApplication>
#include <QQuickView>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char **argv) {
  bool bench = false;
  BenchOptions opt;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--bench"))
      bench = true;
    else if (!std::strcmp(argv[i], "--frames") && i + 1 < argc)
      opt.phaseFrames = std::max(8, std::atoi(argv[++i]));
    else if (!std::strcmp(argv[i], "--shots") && i + 1 < argc)
      opt.shotsDir = QString::fromLocal8Bit(argv[++i]);
    else if (!std::strcmp(argv[i], "--no-naive"))
      opt.naive = false;
    else if (!std::strcmp(argv[i], "--help")) {
      std::puts("usage: lina_ui_spike [--bench [--frames N] [--shots DIR] "
                "[--no-naive]]");
      return 0;
    }
  }

  if (bench) {
    // The bench must never put a window on the desktop, so it always uses the
    // offscreen QPA plugin (which still provides a GLX context here), whatever
    // QT_QPA_PLATFORM says in the environment. LINA_BENCH_QPA overrides this
    // for experiments.
    const QByteArray qpa = qgetenv("LINA_BENCH_QPA");
    qputenv("QT_QPA_PLATFORM", qpa.isEmpty() ? QByteArray("offscreen") : qpa);
    // The offscreen plugin does not advertise RHI rendering, so Qt Quick would
    // silently pick the software adaptation and QQuickRenderControl::initialize
    // would then fail with our GL context. Asking for "rhi" by name skips that
    // platform check.
    qputenv("QT_QUICK_BACKEND", "rhi");
    QGuiApplication app(argc, argv);
    return runBench(opt);
  }

  QGuiApplication app(argc, argv);
  AppController controller(1);
  controller.startClock();
  QQuickView view;
  view.setTitle(QStringLiteral("LINA UI spike (Qt Quick)"));
  view.setResizeMode(QQuickView::SizeRootObjectToView);
  view.setColor(Qt::black);
  view.setInitialProperties({{"controller", QVariant::fromValue(&controller)}});
  view.loadFromModule("Lina", "Main");
  if (view.status() != QQuickView::Ready) {
    for (const QQmlError &e : view.errors())
      std::fprintf(stderr, "%s\n", qPrintable(e.toString()));
    return 1;
  }
  view.resize(1600, 900);
  view.show();
  return app.exec();
}
