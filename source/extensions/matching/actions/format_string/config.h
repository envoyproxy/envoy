#pragma once

#include "envoy/config/core/v3/substitution_format_string.pb.h"
#include "envoy/config/core/v3/substitution_format_string.pb.validate.h"
#include "envoy/matcher/matcher.h"
#include "envoy/server/factory_context.h"

#include "source/common/formatter/substitution_format_string.h"
#include "source/common/protobuf/utility.h"
#include "source/server/filter_chain_manager_impl.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace Actions {
namespace FormatString {

class ActionFactory : public Matcher::ActionFactory<Server::FilterChainActionFactoryContext>,
                                     Logger::Loggable<Logger::Id::config> {
public:
  std::string name() const override { return "envoy.matching.actions.format_string"; }
  Matcher::ActionFactoryCb createActionFactoryCb(const Protobuf::Message& config,
                                                 Server::FilterChainActionFactoryContext& filter_chains,
                                                 ProtobufMessage::ValidationVisitor&) override {
    const auto& config =
        MessageUtil::downcastAndValidate<const envoy::config::core::v3::SubstitutionFormatString&>(
            proto_config, factory_context.messageValidationVisitor());
    Formatter::FormatterConstSharedPtr formatter =
        Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config, factory_context);
    return [formatter]() { return std::make_unique<Action>(formatter); };
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::config::core::v3::SubstitutionFormatString>();
  }
};

} // namespace FormatString
} // namespace Actions
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
