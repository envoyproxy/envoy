#pragma once

#include "envoy/extensions/original_ip_detection/custom_header/v3/custom_header.pb.h"
#include "envoy/http/original_ip_detection.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace OriginalIPDetection {
namespace CustomHeader {

class CustomHeaderIPDetectionFactory : public Http::OriginalIPDetectionFactory {
public:
  Http::OriginalIPDetectionSharedPtr
  createExtension(const Protobuf::Message& message) const override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::original_ip_detection::custom_header::v3::CustomHeaderConfig>();
  }

  std::string name() const override { return "envoy.original_ip_detection.custom_header"; }
};

} // namespace CustomHeader
} // namespace OriginalIPDetection
} // namespace Extensions
} // namespace Envoy
