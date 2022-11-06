#include "source/extensions/http/early_header_mutation/regex_mutation/config.h"

#include "envoy/extensions/http/early_header_mutation/regex_mutation/v3/regex_mutation.pb.h"

#include "source/common/config/utility.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace EarlyHeaderMutation {
namespace RegexMutation {

Envoy::Http::HeaderMutationPtr
Factory::createExtension(const Protobuf::Message& message,
                         Server::Configuration::FactoryContext& context) {
  auto mptr = Envoy::Config::Utility::translateAnyToFactoryConfig(
      dynamic_cast<const ProtobufWkt::Any&>(message), context.messageValidationVisitor(), *this);
  const auto& proto_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::http::early_header_mutation::regex_mutation::v3::RegexMutation&>(
      *mptr, context.messageValidationVisitor());
  return std::make_unique<RegexMutation>(proto_config.header_mutations());
}

REGISTER_FACTORY(Factory, Envoy::Http::EarlyHeaderMutationFactory);

} // namespace RegexMutation
} // namespace EarlyHeaderMutation
} // namespace Http
} // namespace Extensions
} // namespace Envoy

} // namespace RegexMutation
} // namespace EarlyHeaderMutation
} // namespace Http
} // namespace Extensions
} // namespace Envoy
