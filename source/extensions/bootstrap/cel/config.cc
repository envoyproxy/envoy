#include "source/extensions/bootstrap/cel/config.h"

#include "envoy/registry/registry.h"
#include "envoy/extensions/filters/common/expr/v3/cel.pb.validate.h"

#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace Cel {

Server::BootstrapExtensionPtr
CelFactory::createBootstrapExtension(const Protobuf::Message& config,
                                    Server::Configuration::ServerFactoryContext& context) {
  const auto& typed_config =
      MessageUtil::downcastAndValidate<const envoy::extensions::filters::common::expr::v3::CelEvaluatorConfig&>(
          config, context.messageValidationContext().staticValidationVisitor());
  return std::make_unique<CelBootstrapExtension>(typed_config);
}

REGISTER_FACTORY(CelFactory, Server::Configuration::BootstrapExtensionFactory);

} // namespace Cel
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy 