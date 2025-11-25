#include "source/common/http/header_mutation.h"

#include "source/common/common/matchers.h"
#include "source/common/router/header_parser.h"

namespace Envoy {
namespace Http {

namespace {

using HeaderAppendAction = envoy::config::core::v3::HeaderValueOption::HeaderAppendAction;
using HeaderValueOption = envoy::config::core::v3::HeaderValueOption;

// TODO(wbpcode): Inherit from Envoy::Router::HeadersToAddEntry to make sure the formatter
// has the same behavior as the router's formatter. We should try to find a more clean way
// to reuse the formatter after the router's formatter is completely removed.
class AppendMutation : public HeaderEvaluator, public Envoy::Router::HeadersToAddEntry {
public:
  AppendMutation(const HeaderValueOption& header_value_option,
                 const Formatter::CommandParserPtrVector& command_parsers,
                 absl::Status& creation_status)
      : HeadersToAddEntry(header_value_option, command_parsers, creation_status),
        header_name_(header_value_option.header().key()) {}

  void evaluateHeaders(Http::HeaderMap& headers, const Formatter::Context& context,
                       const StreamInfo::StreamInfo& stream_info) const override {
    const std::string value = formatter_->format(context, stream_info);

    if (!value.empty() || add_if_empty_) {
      switch (append_action_) {
        PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
      case HeaderValueOption::APPEND_IF_EXISTS_OR_ADD:
        headers.addReferenceKey(header_name_, value);
        return;
      case HeaderValueOption::ADD_IF_ABSENT: {
        auto header = headers.get(header_name_);
        if (!header.empty()) {
          return;
        }
        headers.addReferenceKey(header_name_, value);
        break;
      }
      case HeaderValueOption::OVERWRITE_IF_EXISTS:
        if (headers.get(header_name_).empty()) {
          return;
        }
        FALLTHRU;
      case HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD:
        headers.setReferenceKey(header_name_, value);
        break;
      }
    }
  }

private:
  Envoy::Http::LowerCaseString header_name_;
};

class RemoveMutation : public HeaderEvaluator {
public:
  RemoveMutation(const std::string& header_name) : header_name_(header_name) {}

  void evaluateHeaders(Http::HeaderMap& headers, const Formatter::Context&,
                       const StreamInfo::StreamInfo&) const override {
    headers.remove(header_name_);
  }

private:
  const Envoy::Http::LowerCaseString header_name_;
};

class RemoveOnMatchMutation : public HeaderEvaluator {
public:
  RemoveOnMatchMutation(const envoy::type::matcher::v3::StringMatcher& key_matcher,
                        Server::Configuration::CommonFactoryContext& context)
      : key_matcher_(key_matcher, context) {}

  void evaluateHeaders(Http::HeaderMap& headers, const Formatter::Context&,
                       const StreamInfo::StreamInfo&) const override {
    headers.removeIf([this](const Http::HeaderEntry& header) {
      return key_matcher_.match(header.key().getStringView());
    });
  }

private:
  const Matchers::StringMatcherImpl key_matcher_;
};

} // namespace

absl::StatusOr<std::unique_ptr<HeaderMutations>>
HeaderMutations::create(const ProtoHeaderMutatons& header_mutations,
                        Server::Configuration::CommonFactoryContext& context,
                        const Formatter::CommandParserPtrVector& command_parsers) {
  absl::Status creation_status = absl::OkStatus();
  auto ret = std::unique_ptr<HeaderMutations>(
      new HeaderMutations(header_mutations, context, command_parsers, creation_status));
  RETURN_IF_NOT_OK(creation_status);
  return ret;
}

HeaderMutations::HeaderMutations(const ProtoHeaderMutatons& header_mutations,
                                 Server::Configuration::CommonFactoryContext& context,
                                 const Formatter::CommandParserPtrVector& command_parsers,
                                 absl::Status& creation_status) {
  for (const auto& mutation : header_mutations) {
    switch (mutation.action_case()) {
    case envoy::config::common::mutation_rules::v3::HeaderMutation::ActionCase::kAppend:
      header_mutations_.emplace_back(
          std::make_unique<AppendMutation>(mutation.append(), command_parsers, creation_status));
      if (!creation_status.ok()) {
        return;
      }
      break;
    case envoy::config::common::mutation_rules::v3::HeaderMutation::ActionCase::kRemove:
      header_mutations_.emplace_back(std::make_unique<RemoveMutation>(mutation.remove()));
      break;
    case envoy::config::common::mutation_rules::v3::HeaderMutation::ActionCase::kRemoveOnMatch:
      header_mutations_.emplace_back(std::make_unique<RemoveOnMatchMutation>(
          mutation.remove_on_match().key_matcher(), context));
      break;
    default:
      PANIC_DUE_TO_PROTO_UNSET;
    }
  }
}

void HeaderMutations::evaluateHeaders(Http::HeaderMap& headers, const Formatter::Context& context,
                                      const StreamInfo::StreamInfo& stream_info) const {
  for (const auto& mutation : header_mutations_) {
    mutation->evaluateHeaders(headers, context, stream_info);
  }
}

} // namespace Http
} // namespace Envoy
