#pragma once

#include "MatrixLayout.h"

class String;
class __FlashStringHelper;

namespace model_api {

struct Result {
  int status;
  const __FlashStringHelper* message;
  int providerStatus = 0;
};

bool IsConfigured();
Result Configure(const char* body, unsigned int length);
Result ParsePrompt(const char* body, unsigned int length, String& prompt);
Result Draw(const String& prompt, matrix::Rgb (&frame)[matrix::PixelCount]);

}  // namespace model_api
