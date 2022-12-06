#pragma once

#include "envoy/config/core/v3/substitution_format_string.pb.h"
#include "envoy/config/core/v3/substitution_format_string.pb.validate.h"
#include "envoy/formatter/substitution_formatter.h"
#include "envoy/matcher/matcher.h"
#include "envoy/server/factory_context.h"

#include "source/common/matcher/matcher.h"
#include "source/server/filter_chain_manager_impl.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace Actions {
namespace FormatString {

class ActionImpl : public Matcher::ActionBase<envoy::config::core::v3::SubstitutionFormatString,
                                              Server::FilterChainBaseAction> {
public:
  ActionImpl(const Formatter::FormatterConstSharedPtr& formatter) : formatter_(formatter) {}
  const Network::FilterChain* get(const Server::FilterChainsByName& filter_chains_by_name,
                                  const StreamInfo::StreamInfo& info) const override;

private:
  const Formatter::FormatterConstSharedPtr formatter_;
};

class ActionFactory : public Matcher::ActionFactory<Server::FilterChainActionFactoryContext> {
public:
  std::string name() const override { return "envoy.matching.actions.format_string"; }
  Matcher::ActionFactoryCb
  createActionFactoryCb(const Protobuf::Message& proto_config,
                        Server::FilterChainActionFactoryContext& context,
                        ProtobufMessage::ValidationVisitor& validator) override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::config::core::v3::SubstitutionFormatString>();
  }
};

} // namespace FormatString
} // namespace Actions
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
