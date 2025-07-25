#include "source/common/http/hash_policy.h"

#include <string>

#include "envoy/common/hashable.h"
#include "envoy/config/route/v3/route_components.pb.h"

#include "source/common/common/hex.h"
#include "source/common/common/matchers.h"
#include "source/common/common/regex.h"
#include "source/common/http/utility.h"
#include "source/common/runtime/runtime_features.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Http {

class HashMethodImplBase : public HashPolicyImpl::HashMethod {
public:
  explicit HashMethodImplBase(bool terminal) : terminal_(terminal) {}

  bool terminal() const override { return terminal_; }

private:
  const bool terminal_;
};

class HeaderHashMethod : public HashMethodImplBase {
public:
  HeaderHashMethod(const envoy::config::route::v3::RouteAction::HashPolicy::Header& header,
                   bool terminal, Regex::Engine& regex_engine, absl::Status& creation_status)
      : HashMethodImplBase(terminal), header_name_(header.header_name()),
        regex_rewrite_substitution_(header.regex_rewrite().substitution()) {
    if (header.has_regex_rewrite()) {
      auto regex_or_error =
          Regex::Utility::parseRegex(header.regex_rewrite().pattern(), regex_engine);
      SET_AND_RETURN_IF_NOT_OK(regex_or_error.status(), creation_status);
      regex_rewrite_ = std::move(*regex_or_error);
    }
  }

  absl::optional<uint64_t> evaluate(OptRef<const RequestHeaderMap> headers,
                                    OptRef<const StreamInfo::StreamInfo>,
                                    HashPolicy::AddCookieCallback) const override {
    if (!headers.has_value()) {
      return absl::nullopt;
    }

    const auto header = headers->get(header_name_);
    if (header.empty()) {
      return absl::nullopt;
    }

    absl::InlinedVector<absl::string_view, 1> header_values;
    const size_t num_headers_to_hash = header.size();
    header_values.reserve(num_headers_to_hash);

    for (size_t i = 0; i < num_headers_to_hash; i++) {
      header_values.push_back(header[i]->value().getStringView());
    }

    absl::InlinedVector<std::string, 1> rewritten_header_values;
    if (regex_rewrite_ != nullptr) {
      rewritten_header_values.reserve(num_headers_to_hash);
      for (absl::string_view& value : header_values) {
        rewritten_header_values.push_back(
            regex_rewrite_->replaceAll(value, regex_rewrite_substitution_));
        value = rewritten_header_values.back();
      }
    }

    // Ensure generating same hash value for different order header values.
    // For example, generates the same hash value for {"foo","bar"} and {"bar","foo"}
    std::sort(header_values.begin(), header_values.end());
    return HashUtil::xxHash64(absl::MakeSpan(header_values));
  }

private:
  const LowerCaseString header_name_;
  Regex::CompiledMatcherPtr regex_rewrite_;
  const std::string regex_rewrite_substitution_;
};

class CookieHashMethod : public HashMethodImplBase {
public:
  CookieHashMethod(const envoy::config::route::v3::RouteAction::HashPolicy::Cookie& cookie,
                   bool terminal)
      : HashMethodImplBase(terminal), name_(cookie.name()), path_(cookie.path()),
        ttl_(cookie.has_ttl() ? absl::optional<std::chrono::seconds>(cookie.ttl().seconds())
                              : absl::nullopt) {
    attributes_.reserve(cookie.attributes().size());
    for (const auto& attribute : cookie.attributes()) {
      attributes_.push_back(CookieAttribute{attribute.name(), attribute.value()});
    }
  }

  absl::optional<uint64_t> evaluate(OptRef<const RequestHeaderMap> headers,
                                    OptRef<const StreamInfo::StreamInfo>,
                                    HashPolicy::AddCookieCallback add_cookie) const override {
    if (!headers.has_value()) {
      return absl::nullopt;
    }

    const std::string exist_value = Utility::parseCookieValue(*headers, name_);
    if (!exist_value.empty()) {
      return HashUtil::xxHash64(exist_value);
    }

    // If the cookie is not found, try to generate a new cookie.

    // If one of the conditions happens, skip generating a new cookie:
    // 1. The cookie has no TTL.
    // 2. The cookie generation callback is null.
    if (!ttl_.has_value() || add_cookie == nullptr) {
      return absl::nullopt;
    }

    const std::string new_value = add_cookie(name_, path_, ttl_.value(), attributes_);
    return new_value.empty() ? absl::nullopt
                             : absl::optional<uint64_t>(HashUtil::xxHash64(new_value));
  }

private:
  const std::string name_;
  const std::string path_;
  const absl::optional<std::chrono::seconds> ttl_;
  std::vector<CookieAttribute> attributes_;
};

class IpHashMethod : public HashMethodImplBase {
public:
  IpHashMethod(bool terminal) : HashMethodImplBase(terminal) {}

  absl::optional<uint64_t> evaluate(OptRef<const RequestHeaderMap>,
                                    OptRef<const StreamInfo::StreamInfo> info,
                                    HashPolicy::AddCookieCallback) const override {
    if (!info.has_value()) {
      return absl::nullopt;
    }

    const auto& conn = info->downstreamAddressProvider();
    const auto& downstream_addr = conn.remoteAddress();

    if (downstream_addr == nullptr) {
      return absl::nullopt;
    }
    auto* downstream_ip = downstream_addr->ip();
    if (downstream_ip == nullptr) {
      return absl::nullopt;
    }
    const auto& downstream_addr_str = downstream_ip->addressAsString();
    if (downstream_addr_str.empty()) {
      return absl::nullopt;
    }
    return HashUtil::xxHash64(downstream_addr_str);
  }
};

class QueryParameterHashMethod : public HashMethodImplBase {
public:
  QueryParameterHashMethod(const std::string& parameter_name, bool terminal)
      : HashMethodImplBase(terminal), parameter_name_(parameter_name) {}

  absl::optional<uint64_t> evaluate(OptRef<const RequestHeaderMap> headers,
                                    OptRef<const StreamInfo::StreamInfo>,
                                    HashPolicy::AddCookieCallback) const override {
    if (!headers.has_value()) {
      return absl::nullopt;
    }

    const Utility::QueryParamsMulti query_parameters =
        Utility::QueryParamsMulti::parseQueryString(headers->getPathValue());
    const auto val = query_parameters.getFirstValue(parameter_name_);
    if (val.has_value()) {
      return HashUtil::xxHash64(val.value());
    }
    return absl::nullopt;
  }

private:
  const std::string parameter_name_;
};

class FilterStateHashMethod : public HashMethodImplBase {
public:
  FilterStateHashMethod(const std::string& key, bool terminal)
      : HashMethodImplBase(terminal), key_(key) {}

  absl::optional<uint64_t> evaluate(OptRef<const RequestHeaderMap>,
                                    OptRef<const StreamInfo::StreamInfo> info,
                                    HashPolicy::AddCookieCallback) const override {
    if (!info.has_value()) {
      return absl::nullopt;
    }

    auto filter_state = info->filterState().getDataReadOnly<Hashable>(key_);
    return filter_state != nullptr ? filter_state->hash() : absl::nullopt;
  }

private:
  const std::string key_;
};

absl::StatusOr<std::unique_ptr<HashPolicyImpl>> HashPolicyImpl::create(
    absl::Span<const envoy::config::route::v3::RouteAction::HashPolicy* const> hash_policy,
    Regex::Engine& regex_engine) {
  absl::Status creation_status = absl::OkStatus();
  std::unique_ptr<HashPolicyImpl> ret = std::unique_ptr<HashPolicyImpl>(
      new HashPolicyImpl(hash_policy, regex_engine, creation_status));
  RETURN_IF_NOT_OK(creation_status);
  return ret;
}

HashPolicyImpl::HashPolicyImpl(
    absl::Span<const envoy::config::route::v3::RouteAction::HashPolicy* const> hash_policies,
    Regex::Engine& regex_engine, absl::Status& creation_status) {

  hash_impls_.reserve(hash_policies.size());
  for (auto* hash_policy : hash_policies) {
    switch (hash_policy->policy_specifier_case()) {
    case envoy::config::route::v3::RouteAction::HashPolicy::PolicySpecifierCase::kHeader:
      hash_impls_.emplace_back(new HeaderHashMethod(hash_policy->header(), hash_policy->terminal(),
                                                    regex_engine, creation_status));
      if (!creation_status.ok()) {
        return;
      }
      break;
    case envoy::config::route::v3::RouteAction::HashPolicy::PolicySpecifierCase::kCookie: {
      hash_impls_.emplace_back(
          new CookieHashMethod(hash_policy->cookie(), hash_policy->terminal()));
      break;
    }
    case envoy::config::route::v3::RouteAction::HashPolicy::PolicySpecifierCase::
        kConnectionProperties:
      if (hash_policy->connection_properties().source_ip()) {
        hash_impls_.emplace_back(new IpHashMethod(hash_policy->terminal()));
      }
      break;
    case envoy::config::route::v3::RouteAction::HashPolicy::PolicySpecifierCase::kQueryParameter:
      hash_impls_.emplace_back(new QueryParameterHashMethod(hash_policy->query_parameter().name(),
                                                            hash_policy->terminal()));
      break;
    case envoy::config::route::v3::RouteAction::HashPolicy::PolicySpecifierCase::kFilterState:
      hash_impls_.emplace_back(
          new FilterStateHashMethod(hash_policy->filter_state().key(), hash_policy->terminal()));
      break;
    case envoy::config::route::v3::RouteAction::HashPolicy::PolicySpecifierCase::
        POLICY_SPECIFIER_NOT_SET:
      PANIC("hash policy not set");
    }
  }
}

absl::optional<uint64_t>
HashPolicyImpl::generateHash(OptRef<const RequestHeaderMap> headers,
                             OptRef<const StreamInfo::StreamInfo> info,
                             HashPolicy::AddCookieCallback add_cookie) const {
  absl::optional<uint64_t> hash;
  for (const HashMethodPtr& hash_impl : hash_impls_) {
    const absl::optional<uint64_t> new_hash = hash_impl->evaluate(headers, info, add_cookie);
    if (new_hash) {
      // Rotating the old value prevents duplicate hash rules from cancelling each other out
      // and preserves all of the entropy
      const uint64_t old_value = hash ? ((hash.value() << 1) | (hash.value() >> 63)) : 0;
      hash = old_value ^ new_hash.value();
    }
    // If the policy is a terminal policy and a hash has been generated, ignore
    // the rest of the hash policies.
    if (hash_impl->terminal() && hash) {
      break;
    }
  }
  return hash;
}

} // namespace Http
} // namespace Envoy
