#pragma once

#include "MatrixLayout.h"

using FrameRenderer = void (*)(const matrix::Rgb (&frame)[matrix::PixelCount]);

bool StartFrameHttpServer();
void PollFrameHttpServer(FrameRenderer render);
