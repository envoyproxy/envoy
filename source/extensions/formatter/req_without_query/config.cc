#include "source/extensions/formatter/req_without_query/config.h"

#include "envoy/extensions/formatter/req_without_query/v3/req_without_query.pb.h"

#include "source/extensions/formatter/req_without_query/req_without_query.h"

namespace Envoy {
namespace Extensions {
namespace Formatter {

::Envoy::Formatter::CommandParserPtr ReqWithoutQueryFactory::createCommandParserFromProto(
    const Protobuf::Message&, Server::Configuration::GenericFactoryContext&) {
  return std::make_unique<ReqWithoutQueryCommandParser>();
}

ProtobufTypes::MessagePtr ReqWithoutQueryFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::formatter::req_without_query::v3::ReqWithoutQuery>();
}

std::string ReqWithoutQueryFactory::name() const { return "envoy.formatter.req_without_query"; }

REGISTER_FACTORY(ReqWithoutQueryFactory, ::Envoy::Formatter::CommandParserFactory);

} // namespace Formatter
} // namespace Extensions
} // namespace Envoy
