#pragma once

#include <string>

#include "envoy/api/v2/discovery.pb.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Config {

struct UpdateAck {
  UpdateAck(absl::string_view nonce, absl::string_view type_url)
      : nonce_(nonce), type_url_(type_url) {}
  std::string nonce_;
  std::string type_url_;
  ::google::rpc::Status error_detail_;
};

} // namespace Config
} // namespace Envoy
