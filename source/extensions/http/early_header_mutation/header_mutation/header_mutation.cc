#include "source/extensions/http/early_header_mutation/header_mutation/header_mutation.h"

#include <utility>

#include "envoy/config/common/mutation_rules/v3/mutation_rules.pb.h"

#include "source/common/formatter/substitution_format_string.h"
#include "source/common/http/header_map_impl.h"
#include "source/server/generic_factory_context.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace EarlyHeaderMutation {
namespace HeaderMutation {

namespace {

absl::StatusOr<std::unique_ptr<Envoy::Http::HeaderMutations>>
createHeaderMutations(const ProtoHeaderMutation& mutations,
                      Server::Configuration::ServerFactoryContext& context,
                      ProtobufMessage::ValidationVisitor& validation_visitor) {
  Formatter::CommandParserPtrVector command_parsers;
  if (!mutations.formatters().empty()) {
    Server::GenericFactoryContextImpl generic_context(context, validation_visitor);
    auto command_parsers_or_error = Formatter::SubstitutionFormatStringUtils::parseFormatters(
        mutations.formatters(), generic_context);
    if (!command_parsers_or_error.ok()) {
      return command_parsers_or_error.status();
    }
    command_parsers = std::move(command_parsers_or_error.value());
  }
  return Envoy::Http::HeaderMutations::create(mutations.mutations(), context, command_parsers);
}

} // namespace

HeaderMutation::HeaderMutation(const ProtoHeaderMutation& mutations,
                               Server::Configuration::ServerFactoryContext& context,
                               ProtobufMessage::ValidationVisitor& validation_visitor)
    : mutations_(
          THROW_OR_RETURN_VALUE(createHeaderMutations(mutations, context, validation_visitor),
                                std::unique_ptr<Envoy::Http::HeaderMutations>)) {}

bool HeaderMutation::mutate(Envoy::Http::RequestHeaderMap& headers,
                            const StreamInfo::StreamInfo& stream_info) const {
  mutations_->evaluateHeaders(headers, {&headers}, stream_info);
  return true;
}

} // namespace HeaderMutation
} // namespace EarlyHeaderMutation
} // namespace Http
} // namespace Extensions
} // namespace Envoy
