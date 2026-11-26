#pragma once

#include "MatrixLayout.h"

using FrameRenderer = void (*)(const matrix::Rgb (&frame)[matrix::PixelCount]);

void PollMatrixHttpServer(FrameRenderer render);
