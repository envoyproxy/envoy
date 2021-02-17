#pragma once

#include "envoy/extensions/original_ip_detection/xff/v3/xff.pb.h"
#include "envoy/http/original_ip_detection.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace OriginalIPDetection {
namespace Xff {

class XffIPDetectionFactory : public Http::OriginalIPDetectionFactory {
public:
  Http::OriginalIPDetectionSharedPtr
  createExtension(const Protobuf::Message& message) const override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::original_ip_detection::xff::v3::XffConfig>();
  }

  std::string name() const override { return "envoy.original_ip_detection.xff"; }
};

} // namespace Xff
} // namespace OriginalIPDetection
} // namespace Extensions
} // namespace Envoy
