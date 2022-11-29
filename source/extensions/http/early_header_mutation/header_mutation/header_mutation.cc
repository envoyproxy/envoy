#include "source/extensions/http/early_header_mutation/header_mutation/header_mutation.h"

#include "envoy/config/common/mutation_rules/v3/mutation_rules.pb.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace EarlyHeaderMutation {
namespace HeaderMutation {

namespace {

// TODO(wbpcode): Inherit from Envoy::Router::HeadersToAddEntry to make sure the formatter
// has the same behavior as the router's formatter. We should try to find a more clean way
// to reuse the formatter after the router's formatter is completely removed.
class AppendMutation : public Mutation, public Envoy::Router::HeadersToAddEntry {
public:
  AppendMutation(const HeaderValueOption& header_value_option)
      : HeadersToAddEntry(header_value_option), header_name_(header_value_option.header().key()) {}

  void mutate(Envoy::Http::RequestHeaderMap& headers,
              const StreamInfo::StreamInfo& stream_info) const override {
    std::string value = formatter_->format(
        headers, *Envoy::Http::StaticEmptyHeaders::get().response_headers, stream_info);

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
      case HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD:
        headers.setReferenceKey(header_name_, value);
        break;
      }
    }
  }

private:
  Envoy::Http::LowerCaseString header_name_;
};

class RemoveMutation : public Mutation {
public:
  RemoveMutation(const std::string& header_name) : header_name_(header_name) {}

  void mutate(Envoy::Http::RequestHeaderMap& headers,
              const StreamInfo::StreamInfo&) const override {
    headers.remove(header_name_);
  }

private:
  const Envoy::Http::LowerCaseString header_name_;
};

} // namespace

HeaderMutation::HeaderMutation(const ProtoHeaderMutation& mutations) {
  for (const auto& mutation : mutations.mutations()) {
    switch (mutation.action_case()) {
    case envoy::config::common::mutation_rules::v3::HeaderMutation::ActionCase::kAppend:
      mutations_.emplace_back(std::make_unique<AppendMutation>(mutation.append()));
      break;
    case envoy::config::common::mutation_rules::v3::HeaderMutation::ActionCase::kRemove:
      mutations_.emplace_back(std::make_unique<RemoveMutation>(mutation.remove()));
      break;
    default:
      PANIC_DUE_TO_PROTO_UNSET;
    }
  }
}

bool HeaderMutation::mutate(Envoy::Http::RequestHeaderMap& headers,
                            const StreamInfo::StreamInfo& stream_info) const {
  for (const auto& mutation : mutations_) {
    mutation->mutate(headers, stream_info);
  }
  return true;
}

} // namespace HeaderMutation
} // namespace EarlyHeaderMutation
} // namespace Http
} // namespace Extensions
} // namespace Envoy
