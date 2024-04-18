#pragma once

#include "envoy/common/pure.h"
#include "envoy/config/typed_config.h"
#include "envoy/protobuf/message_validator.h"

#include "source/extensions/filters/common/rbac/matchers.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {

// Matcher extension factory for RBAC filter. Matchers could be extended to support IP address,
// header value etc.
class MatcherExtensionFactory : public Envoy::Config::TypedFactory {
public:
  /**
   * Function to create Matchers from the specified config.
   * @param config supplies the matcher configuration
   * @return a new MatcherExtension
   */
  virtual MatcherConstSharedPtr create(const Protobuf::Message& config,
                                       ProtobufMessage::ValidationVisitor& validation_visitor) PURE;

  // @brief the category of the matcher extension type for factory registration.
  std::string category() const override { return "envoy.rbac.matchers"; }
};

// Base RBAC matcher extension factory. This facilitates easy creation of matcher extension
// factories. The factory is templated by:
//  M: Matcher extension implementation
//  P: Protobuf definition of the matcher.
template <typename M, typename P>
class BaseMatcherExtensionFactory : public Filters::Common::RBAC::MatcherExtensionFactory {
public:
  Filters::Common::RBAC::MatcherConstSharedPtr
  create(const Protobuf::Message& config,
         ProtobufMessage::ValidationVisitor& validation_visitor) override {
    const auto& matcher_typed_config =
        MessageUtil::downcastAndValidate<const envoy::config::core::v3::TypedExtensionConfig&>(
            config, validation_visitor);

    const auto proto_message = MessageUtil::anyConvert<P>(matcher_typed_config.typed_config());

    return std::make_shared<M>(proto_message);
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override { return std::make_unique<P>(); }
};

} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
