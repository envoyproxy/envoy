#pragma once

#include "envoy/config/subscription.h"

#include "common/config/opaque_resource_decoder_impl.h"
#include "common/config/resource_name.h"

namespace Envoy {
namespace Config {

template <typename Current> struct SubscriptionBase : public Config::SubscriptionCallbacks {
public:
  SubscriptionBase(const envoy::config::core::v3::ApiVersion api_version,
                   ProtobufMessage::ValidationVisitor& validation_visitor,
                   absl::string_view name_field)
      : resource_decoder_(validation_visitor, name_field), api_version_(api_version) {}

  std::string getResourceName() const {
    return Envoy::Config::getResourceName<Current>(api_version_);
  }

protected:
  Config::OpaqueResourceDecoderImpl<Current> resource_decoder_;

private:
  const envoy::config::core::v3::ApiVersion api_version_;
};

} // namespace Config
} // namespace Envoy
