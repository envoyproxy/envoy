#include "source/extensions/matching/actions/format_string/config.h"

#include "envoy/config/core/v3/substitution_format_string.pb.h"
#include "envoy/config/core/v3/substitution_format_string.pb.validate.h"
#include "envoy/formatter/substitution_formatter_base.h"
#include "envoy/registry/registry.h"

#include "source/common/formatter/substitution_format_string.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace Actions {
namespace FormatString {

class FilterChainActionImpl
    : public Matcher::ActionBase<envoy::config::core::v3::SubstitutionFormatString,
                                 Server::Configuration::FilterChainBaseAction> {
public:
  FilterChainActionImpl(Formatter::FormatterConstSharedPtr formatter)
      : formatter_(std::move(formatter)) {}
  const Network::FilterChain*
  get(const Server::Configuration::FilterChainsByName& filter_chains_by_name,
      const StreamInfo::StreamInfo& info) const override {
    const std::string name = formatter_->format({}, info);
    const auto chain_match = filter_chains_by_name.find(name);
    if (chain_match != filter_chains_by_name.end()) {
      return chain_match->second.get();
    }
    return nullptr;
  }

private:
  const Formatter::FormatterConstSharedPtr formatter_;
};

class FilterChainActionFactory : public Matcher::ActionFactory<FilterChainActionFactoryContext> {
public:
  std::string name() const override { return "envoy.matching.actions.format_string"; }
  Matcher::ActionConstSharedPtr
  createAction(const Protobuf::Message& proto_config, FilterChainActionFactoryContext& context,
               ProtobufMessage::ValidationVisitor& validator) override {
    const auto& config =
        MessageUtil::downcastAndValidate<const envoy::config::core::v3::SubstitutionFormatString&>(
            proto_config, validator);

    Server::GenericFactoryContextImpl generic_context(context, validator);
    Formatter::FormatterConstSharedPtr formatter = THROW_OR_RETURN_VALUE(
        Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config, generic_context),
        Formatter::FormatterPtr);
    return std::make_shared<FilterChainActionImpl>(std::move(formatter));
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::config::core::v3::SubstitutionFormatString>();
  }
};

class StringReturningFormatterActionImpl
    : public Matcher::ActionBase<envoy::config::core::v3::SubstitutionFormatString,
                                 StringReturningAction> {
public:
  explicit StringReturningFormatterActionImpl(Formatter::FormatterConstSharedPtr formatter)
      : formatter_(std::move(formatter)) {}
  std::string string(const StreamInfo::StreamInfo& stream_info) const override {
    return formatter_->format({}, stream_info);
  }

private:
  const Formatter::FormatterConstSharedPtr formatter_;
};

class StringReturningFormatterActionFactory
    : public Matcher::ActionFactory<StringReturningActionFactoryContext> {
public:
  std::string name() const override { return "envoy.matching.actions.substitution_format_string"; }
  Matcher::ActionConstSharedPtr
  createAction(const Protobuf::Message& proto_config, StringReturningActionFactoryContext& context,
               ProtobufMessage::ValidationVisitor& validator) override {
    const auto& config =
        MessageUtil::downcastAndValidate<const envoy::config::core::v3::SubstitutionFormatString&>(
            proto_config, validator);

    Server::GenericFactoryContextImpl generic_context(context.server_factory_context_, validator);
    Formatter::FormatterConstSharedPtr formatter = THROW_OR_RETURN_VALUE(
        Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config, generic_context),
        Formatter::FormatterPtr);
    return std::make_shared<StringReturningFormatterActionImpl>(std::move(formatter));
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::config::core::v3::SubstitutionFormatString>();
  }
};

class StringReturningDirectActionImpl
    : public Matcher::ActionBase<Protobuf::StringValue, StringReturningAction> {
public:
  explicit StringReturningDirectActionImpl(const Protobuf::StringValue& config)
      : value_(config.value()) {}
  std::string string(const StreamInfo::StreamInfo&) const override { return value_; }

private:
  const std::string value_;
};

class StringReturningDirectActionFactory
    : public Matcher::ActionFactory<StringReturningActionFactoryContext> {
public:
  std::string name() const override { return "envoy.matching.actions.direct_string"; }
  Matcher::ActionConstSharedPtr createAction(const Protobuf::Message& proto_config,
                                             StringReturningActionFactoryContext&,
                                             ProtobufMessage::ValidationVisitor&) override {
    const auto& config = dynamic_cast<const Protobuf::StringValue&>(proto_config);

    return std::make_shared<StringReturningDirectActionImpl>(config);
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Protobuf::StringValue>();
  }
};

REGISTER_FACTORY(FilterChainActionFactory, Matcher::ActionFactory<FilterChainActionFactoryContext>);
REGISTER_FACTORY(StringReturningFormatterActionFactory,
                 Matcher::ActionFactory<StringReturningActionFactoryContext>);
REGISTER_FACTORY(StringReturningDirectActionFactory,
                 Matcher::ActionFactory<StringReturningActionFactoryContext>);

} // namespace FormatString
} // namespace Actions
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
