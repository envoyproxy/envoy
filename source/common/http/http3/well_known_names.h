#pragma once

#include <string>

#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Http {

class QuicCodecNameValues {
public:
  // QUICHE is the only QUIC implementation for now.
  const std::string Quiche = "quiche";
};

using QuicCodecNames = ConstSingleton<QuicCodecNameValues>;

} // namespace Http
} // namespace Envoy
