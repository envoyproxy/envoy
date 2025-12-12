#pragma once

#include "envoy/common/pure.h"
#include "envoy/config/typed_config.h"

#include "source/common/config/utility.h"
#include "source/extensions/filters/common/rbac/matcher_interface.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {

// Principal extension factory for RBAC filter.
class PrincipalExtensionFactory : public Envoy::Config::TypedFactory {
public:
  /**
   * Function to create Matchers from the specified config.
   * @param config supplies the matcher configuration
   * @return a new MatcherExtension
   */
  virtual MatcherConstPtr create(const envoy::config::core::v3::TypedExtensionConfig& config,
                                 Server::Configuration::CommonFactoryContext& context) PURE;

  // @brief the category of the matcher extension type for factory registration.
  std::string category() const override { return "envoy.rbac.principals"; }
};

// Base RBAC principal extension factory. This facilitates easy creation of principal extension
// factories. The factory is templated by:
//   PrincipalType: Principal extension type.
//   ConfigProto: Protobuf message for the principal configuration.
template <typename PrincipalType, typename ConfigProto>
class BasePrincipalExtensionFactory : public PrincipalExtensionFactory {
public:
  Filters::Common::RBAC::MatcherConstPtr
  create(const envoy::config::core::v3::TypedExtensionConfig& config,
         Server::Configuration::CommonFactoryContext& context) override {
    ConfigProto typed_config;
    MessageUtil::anyConvertAndValidate(config.typed_config(), typed_config,
                                       context.messageValidationVisitor());
    return std::make_unique<PrincipalType>(typed_config, context);
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ConfigProto>();
  }
};

} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
