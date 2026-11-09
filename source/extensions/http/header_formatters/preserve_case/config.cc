#include "source/extensions/http/header_formatters/preserve_case/config.h"

#include "envoy/extensions/http/header_formatters/preserve_case/v3/preserve_case.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/protobuf/message_validator_impl.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderFormatters {
namespace PreserveCase {

absl::StatusOr<Envoy::Http::StatefulHeaderKeyFormatterFactorySharedPtr>
PreserveCaseFormatterFactoryConfig::createFactoryFromProto(
    const Protobuf::Message& message, Server::Configuration::GenericFactoryContext&) {
  auto config =
      MessageUtil::downcastAndValidate<const envoy::extensions::http::header_formatters::
                                           preserve_case::v3::PreserveCaseFormatterConfig&>(
          message, ProtobufMessage::getStrictValidationVisitor());

  return Envoy::Http::StatefulHeaderKeyFormatterFactorySharedPtr{
      std::make_shared<PreserveCaseFormatterFactory>(config.forward_reason_phrase(),
                                                     config.formatter_type_on_envoy_headers())};
}

LEGACY_REGISTER_FACTORY(PreserveCaseFormatterFactoryConfig,
                        Envoy::Http::StatefulHeaderKeyFormatterFactoryConfig, "preserve_case");

} // namespace PreserveCase
} // namespace HeaderFormatters
} // namespace Http
} // namespace Extensions
} // namespace Envoy
