#pragma once

#include <string>

#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Http {

class QuicCodecNameValues {
public:
  const std::string Client = "quiche_client";
  const std::string Server = "quiche_server";
};

using QuicCodecNames = ConstSingleton<QuicCodecNameValues>;

} // namespace Http
} // namespace Envoy
