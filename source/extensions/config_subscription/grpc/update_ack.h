#pragma once

#include <string>

#include "absl/strings/string_view.h"
#include "google/rpc/status.pb.h"

namespace Envoy {
namespace Config {

struct UpdateAck {
  UpdateAck(absl::string_view nonce, absl::string_view type_url)
      : nonce_(nonce), type_url_(type_url) {}
  const std::string nonce_;
  const std::string type_url_;
  ::google::rpc::Status error_detail_;
};

} // namespace Config
} // namespace Envoy
