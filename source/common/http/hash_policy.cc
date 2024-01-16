#include "source/common/http/hash_policy.h"

#include <string>

#include "envoy/common/hashable.h"
#include "envoy/config/route/v3/route_components.pb.h"

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
                   bool terminal)
      : HashMethodImplBase(terminal), header_name_(header.header_name()) {
    if (header.has_regex_rewrite()) {
      const auto& rewrite_spec = header.regex_rewrite();
      regex_rewrite_ = Regex::Utility::parseRegex(rewrite_spec.pattern());
      regex_rewrite_substitution_ = rewrite_spec.substitution();
    }
  }

  absl::optional<uint64_t> evaluate(const Network::Address::Instance*,
                                    const RequestHeaderMap& headers,
                                    const HashPolicy::AddCookieCallback,
                                    const StreamInfo::FilterStateSharedPtr) const override {
    absl::optional<uint64_t> hash;

    const auto header = headers.get(header_name_);
    if (!header.empty()) {
      absl::InlinedVector<absl::string_view, 1> header_values;
      size_t num_headers_to_hash = header.size();
      header_values.reserve(num_headers_to_hash);

      for (size_t i = 0; i < num_headers_to_hash; i++) {
        header_values.push_back(header[i]->value().getStringView());
      }

      absl::InlinedVector<std::string, 1> rewritten_header_values;
      if (regex_rewrite_ != nullptr) {
        rewritten_header_values.reserve(num_headers_to_hash);
        for (auto& value : header_values) {
          rewritten_header_values.push_back(
              regex_rewrite_->replaceAll(value, regex_rewrite_substitution_));
          value = rewritten_header_values.back();
        }
      }

      // Ensure generating same hash value for different order header values.
      // For example, generates the same hash value for {"foo","bar"} and {"bar","foo"}
      std::sort(header_values.begin(), header_values.end());
      hash = HashUtil::xxHash64(absl::MakeSpan(header_values));
    }
    return hash;
  }

private:
  const LowerCaseString header_name_;
  Regex::CompiledMatcherPtr regex_rewrite_{};
  std::string regex_rewrite_substitution_{};
};

class CookieHashMethod : public HashMethodImplBase {
public:
  CookieHashMethod(const std::string& key, const std::string& path,
                   const absl::optional<std::chrono::seconds>& ttl, bool terminal,
                   const CookieAttributeRefVector attributes)
      : HashMethodImplBase(terminal), key_(key), path_(path), ttl_(ttl), attributes_(attributes) {}

  absl::optional<uint64_t> evaluate(const Network::Address::Instance*,
                                    const RequestHeaderMap& headers,
                                    const HashPolicy::AddCookieCallback add_cookie,
                                    const StreamInfo::FilterStateSharedPtr) const override {
    absl::optional<uint64_t> hash;
    std::string value = Utility::parseCookieValue(headers, key_);
    if (value.empty() && ttl_.has_value()) {
      value = add_cookie(key_, path_, ttl_.value(), attributes_);
      hash = HashUtil::xxHash64(value);

    } else if (!value.empty()) {
      hash = HashUtil::xxHash64(value);
    }
    return hash;
  }

private:
  const std::string key_;
  const std::string path_;
  const absl::optional<std::chrono::seconds> ttl_;
  const CookieAttributeRefVector attributes_;
};

class IpHashMethod : public HashMethodImplBase {
public:
  IpHashMethod(bool terminal) : HashMethodImplBase(terminal) {}

  absl::optional<uint64_t> evaluate(const Network::Address::Instance* downstream_addr,
                                    const RequestHeaderMap&, const HashPolicy::AddCookieCallback,
                                    const StreamInfo::FilterStateSharedPtr) const override {
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

  absl::optional<uint64_t> evaluate(const Network::Address::Instance*,
                                    const RequestHeaderMap& headers,
                                    const HashPolicy::AddCookieCallback,
                                    const StreamInfo::FilterStateSharedPtr) const override {
    absl::optional<uint64_t> hash;

    const HeaderEntry* header = headers.Path();
    if (header) {
      Http::Utility::QueryParamsMulti query_parameters =
          Http::Utility::QueryParamsMulti::parseQueryString(header->value().getStringView());
      const auto val = query_parameters.getFirstValue(parameter_name_);
      if (val.has_value()) {
        hash = HashUtil::xxHash64(val.value());
      }
    }
    return hash;
  }

private:
  const std::string parameter_name_;
};

class FilterStateHashMethod : public HashMethodImplBase {
public:
  FilterStateHashMethod(const std::string& key, bool terminal)
      : HashMethodImplBase(terminal), key_(key) {}

  absl::optional<uint64_t>
  evaluate(const Network::Address::Instance*, const RequestHeaderMap&,
           const HashPolicy::AddCookieCallback,
           const StreamInfo::FilterStateSharedPtr filter_state) const override {
    if (auto typed_state = filter_state->getDataReadOnly<Hashable>(key_); typed_state != nullptr) {
      return typed_state->hash();
    }
    return absl::nullopt;
  }

private:
  const std::string key_;
};

HashPolicyImpl::HashPolicyImpl(
    absl::Span<const envoy::config::route::v3::RouteAction::HashPolicy* const> hash_policies) {

  hash_impls_.reserve(hash_policies.size());
  for (auto* hash_policy : hash_policies) {
    switch (hash_policy->policy_specifier_case()) {
    case envoy::config::route::v3::RouteAction::HashPolicy::PolicySpecifierCase::kHeader:
      hash_impls_.emplace_back(
          new HeaderHashMethod(hash_policy->header(), hash_policy->terminal()));
      break;
    case envoy::config::route::v3::RouteAction::HashPolicy::PolicySpecifierCase::kCookie: {
      absl::optional<std::chrono::seconds> ttl;
      if (hash_policy->cookie().has_ttl()) {
        ttl = std::chrono::seconds(hash_policy->cookie().ttl().seconds());
      }
      std::vector<CookieAttribute> attributes;
      for (const auto& attribute : hash_policy->cookie().attributes()) {
        attributes.push_back({attribute.name(), attribute.value()});
      }
      CookieAttributeRefVector ref_attributes;
      for (const auto& attribute : attributes) {
        ref_attributes.push_back(attribute);
      }
      hash_impls_.emplace_back(new CookieHashMethod(hash_policy->cookie().name(),
                                                    hash_policy->cookie().path(), ttl,
                                                    hash_policy->terminal(), ref_attributes));
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
HashPolicyImpl::generateHash(const Network::Address::Instance* downstream_addr,
                             const RequestHeaderMap& headers, const AddCookieCallback add_cookie,
                             const StreamInfo::FilterStateSharedPtr filter_state) const {
  absl::optional<uint64_t> hash;
  for (const HashMethodPtr& hash_impl : hash_impls_) {
    const absl::optional<uint64_t> new_hash =
        hash_impl->evaluate(downstream_addr, headers, add_cookie, filter_state);
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
