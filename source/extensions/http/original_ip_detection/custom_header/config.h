#pragma once

#include "envoy/extensions/http/original_ip_detection/custom_header/v3/custom_header.pb.h"
#include "envoy/http/original_ip_detection.h"
#include "envoy/registry/registry.h"

#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace OriginalIPDetection {
namespace CustomHeader {

/**
 * Config registration for the custom header IP detection extension.
 * @see OriginalIPDetectionFactory.
 */
class CustomHeaderIPDetectionFactory : public Envoy::Http::OriginalIPDetectionFactory {
public:
  absl::StatusOr<Envoy::Http::OriginalIPDetectionSharedPtr>
  createExtension(const Protobuf::Message& message,
                  Server::Configuration::FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::http::original_ip_detection::custom_header::v3::CustomHeaderConfig>();
  }

  std::string name() const override { return "envoy.http.original_ip_detection.custom_header"; }
};

DECLARE_FACTORY(CustomHeaderIPDetectionFactory);

} // namespace CustomHeader
} // namespace OriginalIPDetection
} // namespace Http
} // namespace Extensions
} // namespace Envoy
