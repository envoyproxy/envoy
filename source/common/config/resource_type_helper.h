#pragma once

#include "envoy/config/subscription.h"

#include "source/common/config/opaque_resource_decoder_impl.h"
#include "source/common/config/resource_name.h"

namespace Envoy {
namespace Config {

/**
 * Helper for resource type decoding and name identification.
 * This class is intended to be used via composition in xDS API implementations.
 */
template <typename Current> class ResourceTypeHelper {
public:
  ResourceTypeHelper(ProtobufMessage::ValidationVisitor& validation_visitor,
                     absl::string_view name_field)
      : resource_decoder_(std::make_shared<Config::OpaqueResourceDecoderImpl<Current>>(
            validation_visitor, name_field)) {}

  std::string getResourceName() const { return Envoy::Config::getResourceName<Current>(); }

  OpaqueResourceDecoderSharedPtr resourceDecoder() const { return resource_decoder_; }

private:
  const OpaqueResourceDecoderSharedPtr resource_decoder_;
};

} // namespace Config
} // namespace Envoy
