#include "source/extensions/bootstrap/cel/config.h"

#include "envoy/registry/registry.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/bootstrap/cel/cel.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace Cel {

Server::BootstrapExtensionPtr
CelFactory::createBootstrapExtension(const Protobuf::Message& config,
                                     Server::Configuration::ServerFactoryContext& context) {
  const auto& typed_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::bootstrap::cel::CelEvaluatorConfig&>(
      config, context.messageValidationContext().staticValidationVisitor());
  return std::make_unique<CelBootstrapExtension>(typed_config);
}

REGISTER_FACTORY(CelFactory, Server::Configuration::BootstrapExtensionFactory);

} // namespace Cel
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
