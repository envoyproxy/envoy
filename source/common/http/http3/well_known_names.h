#pragma once

#include <string>

#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Http {

class QuicCodecNameValues {
public:
  const std::string Client = "client_codec";
  const std::string Server = "server_codec";
};

using QuicCodecNames = ConstSingleton<QuicCodecNameValues>;

} // namespace Http
} // namespace Envoy
