#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/http/codes.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Server {

struct ClustersParams {
  enum class Format {
    Unknown,
    Text,
    Json,
  };

  ClustersParams() = default;

  Http::Code parse(absl::string_view url, Buffer::Instance& response);

  Format format_{Format::Text};
};

} // namespace Server
} // namespace Envoy
