#pragma once

#include "envoy/extensions/http/original_ip_detection/extracted_external_address/v3/extracted_external_address.pb.h"
#include "envoy/http/original_ip_detection.h"
#include "envoy/registry/registry.h"

#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace OriginalIPDetection {
namespace ExtractedExternalAddress {

/**
 * Config registration for the extracted external address IP detection extension.
 * @see OriginalIPDetectionFactory.
 */
class ExtractedExternalAddressIPDetectionFactory : public Envoy::Http::OriginalIPDetectionFactory {
public:
  absl::StatusOr<Envoy::Http::OriginalIPDetectionSharedPtr>
  createExtension(const Protobuf::Message& message,
                  Server::Configuration::FactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::http::original_ip_detection::
                                extracted_external_address::v3::ExtractedExternalAddressConfig>();
  }

  std::string name() const override {
    return "envoy.http.original_ip_detection.extracted_external_address";
  }
};

DECLARE_FACTORY(ExtractedExternalAddressIPDetectionFactory);

} // namespace ExtractedExternalAddress
} // namespace OriginalIPDetection
} // namespace Http
} // namespace Extensions
} // namespace Envoy
