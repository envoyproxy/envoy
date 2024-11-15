#pragma once

#include "envoy/extensions/http/original_ip_detection/xff/v3/xff.pb.h"
#include "envoy/http/original_ip_detection.h"
#include "envoy/registry/registry.h"

#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace OriginalIPDetection {
namespace Xff {

/**
 * Config registration for the x-forwarded-for IP detection extension.
 * @see OriginalIPDetectionFactory.
 */
class XffIPDetectionFactory : public Envoy::Http::OriginalIPDetectionFactory {
public:
  absl::StatusOr<Envoy::Http::OriginalIPDetectionSharedPtr>
  createExtension(const Protobuf::Message& message,
                  Server::Configuration::FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::http::original_ip_detection::xff::v3::XffConfig>();
  }

  std::string name() const override { return "envoy.http.original_ip_detection.xff"; }
};

DECLARE_FACTORY(XffIPDetectionFactory);

} // namespace Xff
} // namespace OriginalIPDetection
} // namespace Http
} // namespace Extensions
} // namespace Envoy
