#pragma once

#include <string>

#include "envoy/config/core/v3/config_source.pb.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/common/macros.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Grpc {

struct VersionOptions {
  const Protobuf::MethodDescriptor& getMethodDescriptor() const {
    std::string method_name;
    switch (api_version_) {
    case envoy::config::core::v3::ApiVersion::AUTO:
      FALLTHRU;
    case envoy::config::core::v3::ApiVersion::V2:
      method_name = fmt::format(method_name_template_, use_alpha_ ? "v2alpha" : "v2");
      break;

    case envoy::config::core::v3::ApiVersion::V3:
      method_name = fmt::format(method_name_template_, "v3");
      break;
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
    const auto* method_descriptor =
        Protobuf::DescriptorPool::generated_pool()->FindMethodByName(method_name);
    ASSERT(method_descriptor != nullptr);
    return *method_descriptor;
  }

  envoy::config::core::v3::ApiVersion api_version_;

  bool use_alpha_{false};

  std::string method_name_template_{""};
};

} // namespace Grpc
} // namespace Envoy
