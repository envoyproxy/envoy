#include <algorithm>

#include "common/common/base64.h"
#include "common/jwt_authn/status.h"

namespace Envoy {
namespace JwtAuthn {

namespace {

// Conversion table is taken from
// https://opensource.apple.com/source/QuickTimeStreamingServer/QuickTimeStreamingServer-452/CommonUtilitiesLib/base64.c
//
// and modified the position of 62 ('+' to '-') and 63 ('/' to '_')
const uint8_t kReverseLookupTableBase64Url[256] = {
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 64,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64, 64, 0,  1,  2,  3,  4,  5,  6,
    7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 63,
    64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48,
    49, 50, 51, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64};

bool IsNotBase64UrlChar(int8_t c) {
  return kReverseLookupTableBase64Url[static_cast<int32_t>(c)] & 64;
}

} // namespace

std::string decodeBase64Url(std::string input) {
  // allow at most 2 padding letters at the end of the input, only if input
  // length is divisible by 4
  int len = input.length();
  if (len % 4 == 0) {
    if (input[len - 1] == '=') {
      input.pop_back();
      if (input[len - 2] == '=') {
        input.pop_back();
      }
    }
  }
  // if input contains non-base64url character, return empty string
  // Note: padding letter must not be contained
  if (std::find_if(input.begin(), input.end(), IsNotBase64UrlChar) != input.end()) {
    return "";
  }

  // base64url is using '-', '_' instead of '+', '/' in base64 string.
  std::replace(input.begin(), input.end(), '-', '+');
  std::replace(input.begin(), input.end(), '_', '/');

  // base64 string should be padded with '=' so as to the length of the string
  // is divisible by 4.
  switch (input.length() % 4) {
  case 0:
    break;
  case 2:
    input += "==";
    break;
  case 3:
    input += "=";
    break;
  default:
    // * an invalid base64url input. return empty string.
    return "";
  }
  return Base64::decode(input);
}

} // namespace JwtAuthn
} // namespace Envoy
