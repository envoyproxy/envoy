#pragma once

#include "envoy/config/subscription.h"

#include "source/common/config/opaque_resource_decoder_impl.h"
#include "source/common/config/resource_name.h"

namespace Envoy {
namespace Config {

template <typename Current> struct SubscriptionBase : public Config::SubscriptionCallbacks {
public:
  SubscriptionBase(ProtobufMessage::ValidationVisitor& validation_visitor,
                   absl::string_view name_field)
      : resource_decoder_(validation_visitor, name_field) {}

  std::string getResourceName() const { return Envoy::Config::getResourceName<Current>(); }

protected:
  Config::OpaqueResourceDecoderImpl<Current> resource_decoder_;
};

} // namespace Config
} // namespace Envoy
