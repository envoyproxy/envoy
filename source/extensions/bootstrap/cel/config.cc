#include "source/extensions/bootstrap/cel/config.h"

#include "envoy/extensions/bootstrap/cel/v3/cel.pb.h"
#include "envoy/extensions/bootstrap/cel/v3/cel.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace Cel {

Server::BootstrapExtensionPtr
CelFactory::createBootstrapExtension(const Protobuf::Message& config,
                                     Server::Configuration::ServerFactoryContext& context) {
  const auto& typed_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::bootstrap::cel::v3::CelEvaluatorConfig&>(
      config, context.messageValidationContext().staticValidationVisitor());

  // Prepare a snapshot of runtime options and publish via singleton for consumers.
  return std::make_unique<CelBootstrapExtension>(context, typed_config);
}

REGISTER_FACTORY(CelFactory, Server::Configuration::BootstrapExtensionFactory);

} // namespace Cel
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
