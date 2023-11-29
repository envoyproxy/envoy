#include "source/extensions/matching/actions/format_string/config.h"

#include "envoy/registry/registry.h"

#include "source/common/formatter/substitution_format_string.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace Actions {
namespace FormatString {

const Network::FilterChain*
ActionImpl::get(const Server::Configuration::FilterChainsByName& filter_chains_by_name,
                const StreamInfo::StreamInfo& info) const {
  const std::string name = formatter_->formatWithContext({}, info);
  const auto chain_match = filter_chains_by_name.find(name);
  if (chain_match != filter_chains_by_name.end()) {
    return chain_match->second.get();
  }
  return nullptr;
}

Matcher::ActionFactoryCb
ActionFactory::createActionFactoryCb(const Protobuf::Message& proto_config,
                                     FilterChainActionFactoryContext& context,
                                     ProtobufMessage::ValidationVisitor& validator) {
  const auto& config =
      MessageUtil::downcastAndValidate<const envoy::config::core::v3::SubstitutionFormatString&>(
          proto_config, validator);

  const Server::GenericFactoryContextImpl generic_context(context, validator);

  Formatter::FormatterConstSharedPtr formatter =
      Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config, generic_context);
  return [formatter]() { return std::make_unique<ActionImpl>(formatter); };
}

REGISTER_FACTORY(ActionFactory, Matcher::ActionFactory<FilterChainActionFactoryContext>);

} // namespace FormatString
} // namespace Actions
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
