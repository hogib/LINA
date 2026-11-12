// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

struct BenchOptions {
  int phaseFrames = 100;    ///< frames per scripted gesture phase
  int playheadFrames = 200; ///< frames of the playhead-only phase
  QString shotsDir; ///< if set, PNG frames are written here for inspection
  bool naive = true;
};

/// Headless benchmark: QQuickRenderControl into an offscreen FBO, no window is
/// shown.
int runBench(const BenchOptions &opt);
