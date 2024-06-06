#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/http/codes.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Server {

class ClustersParams {
  enum class Format {
    Text,
    Json,
  };

  Http::Code parse(absl::string_view url, Buffer::Instance& response);

  Format format_;
};

} // namespace Server
} // namespace Envoy
