#include "common/http/hash_policy.h"

#include "envoy/config/route/v3/route_components.pb.h"

#include "common/http/utility.h"

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
  HeaderHashMethod(const std::string& header_name, bool terminal)
      : HashMethodImplBase(terminal), header_name_(header_name) {}

  absl::optional<uint64_t> evaluate(const Network::Address::Instance*,
                                    const RequestHeaderMap& headers,
                                    const HashPolicy::AddCookieCallback,
                                    const StreamInfo::FilterStateSharedPtr, int) const override {
    absl::optional<uint64_t> hash;

    const HeaderEntry* header = headers.get(header_name_);
    if (header) {
      hash = HashUtil::xxHash64(header->value().getStringView());
    }
    return hash;
  }

private:
  const LowerCaseString header_name_;
};

class CookieHashMethod : public HashMethodImplBase {
public:
  CookieHashMethod(const std::string& key, const std::string& path,
                   const absl::optional<std::chrono::seconds>& ttl, bool terminal)
      : HashMethodImplBase(terminal), key_(key), path_(path), ttl_(ttl) {}

  absl::optional<uint64_t> evaluate(const Network::Address::Instance*,
                                    const RequestHeaderMap& headers,
                                    const HashPolicy::AddCookieCallback add_cookie,
                                    const StreamInfo::FilterStateSharedPtr, int) const override {
    absl::optional<uint64_t> hash;
    std::string value = Utility::parseCookieValue(headers, key_);
    if (value.empty() && ttl_.has_value()) {
      value = add_cookie(key_, path_, ttl_.value());
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
};

class IpHashMethod : public HashMethodImplBase {
public:
  IpHashMethod(bool terminal) : HashMethodImplBase(terminal) {}

  absl::optional<uint64_t> evaluate(const Network::Address::Instance* downstream_addr,
                                    const RequestHeaderMap&, const HashPolicy::AddCookieCallback,
                                    const StreamInfo::FilterStateSharedPtr, int) const override {
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
                                    const StreamInfo::FilterStateSharedPtr, int) const override {
    absl::optional<uint64_t> hash;

    const HeaderEntry* header = headers.Path();
    if (header) {
      Http::Utility::QueryParams query_parameters =
          Http::Utility::parseQueryString(header->value().getStringView());
      const auto& iter = query_parameters.find(parameter_name_);
      if (iter != query_parameters.end()) {
        hash = HashUtil::xxHash64(iter->second);
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

  absl::optional<uint64_t> evaluate(const Network::Address::Instance*, const RequestHeaderMap&,
                                    const HashPolicy::AddCookieCallback,
                                    const StreamInfo::FilterStateSharedPtr filter_state,
                                    int) const override {
    if (filter_state->hasData<Hashable>(key_)) {
      return filter_state->getDataReadOnly<Hashable>(key_).hash();
    }
    return absl::nullopt;
  }

private:
  const std::string key_;
};

class RetryCountHashMethod : public HashMethodImplBase {
public:
  RetryCountHashMethod(bool terminal) : HashMethodImplBase(terminal) {}

  absl::optional<uint64_t> evaluate(const Network::Address::Instance*, const RequestHeaderMap&,
                                    const HashPolicy::AddCookieCallback,
                                    const StreamInfo::FilterStateSharedPtr,
                                    int retry) const override {
    if (retry > 0) {
      // Return the negated retry count to trigger large variance in the computed hash. Returning
      // the positive retry count will only vary the hash by +/-1 on the first retry and if the
      // load balancer just performs modulo arithmetic on the hash, retries are likely to land on
      // only the adjacent hosts.
      return absl::make_optional<uint64_t>(static_cast<uint64_t>(-retry));
    }

    return absl::nullopt;
  }
};

HashPolicyImpl::HashPolicyImpl(
    absl::Span<const envoy::config::route::v3::RouteAction::HashPolicy* const> hash_policies) {

  // Iterate over hash policies and check that RetryCountHashMethod is always used in conjunction
  // with another method.
  bool sawRetry = false;
  bool sawNonRetry = false;
  for (auto* hash_policy : hash_policies) {
    if (hash_policy->policy_specifier_case() ==
        envoy::config::route::v3::RouteAction::HashPolicy::PolicySpecifierCase::kRetryCount) {
      sawRetry = true;
    } else {
      sawNonRetry = true;
    }

    if (hash_policy->terminal()) {
      if (sawRetry && !sawNonRetry) {
        throw EnvoyException(
            "RetryCount hash policy must be used in conjunction with another policy");
      }

      sawRetry = false;
      sawNonRetry = false;
    }
  }
  if (sawRetry && !sawNonRetry) {
    throw EnvoyException("RetryCount hash policy must be used in conjunction with another policy");
  }

  hash_impls_.reserve(hash_policies.size());
  for (auto* hash_policy : hash_policies) {
    switch (hash_policy->policy_specifier_case()) {
    case envoy::config::route::v3::RouteAction::HashPolicy::PolicySpecifierCase::kHeader:
      hash_impls_.emplace_back(
          new HeaderHashMethod(hash_policy->header().header_name(), hash_policy->terminal()));
      break;
    case envoy::config::route::v3::RouteAction::HashPolicy::PolicySpecifierCase::kCookie: {
      absl::optional<std::chrono::seconds> ttl;
      if (hash_policy->cookie().has_ttl()) {
        ttl = std::chrono::seconds(hash_policy->cookie().ttl().seconds());
      }
      hash_impls_.emplace_back(new CookieHashMethod(hash_policy->cookie().name(),
                                                    hash_policy->cookie().path(), ttl,
                                                    hash_policy->terminal()));
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
    case envoy::config::route::v3::RouteAction::HashPolicy::PolicySpecifierCase::kRetryCount:
      hash_impls_.emplace_back(new RetryCountHashMethod(hash_policy->terminal()));
      break;
    default:
      throw EnvoyException(
          absl::StrCat("Unsupported hash policy ", hash_policy->policy_specifier_case()));
    }
  }
}

absl::optional<uint64_t>
HashPolicyImpl::generateHash(const Network::Address::Instance* downstream_addr,
                             const RequestHeaderMap& headers, const AddCookieCallback add_cookie,
                             const StreamInfo::FilterStateSharedPtr filter_state, int retry) const {
  absl::optional<uint64_t> hash;
  for (const HashMethodPtr& hash_impl : hash_impls_) {
    const absl::optional<uint64_t> new_hash =
        hash_impl->evaluate(downstream_addr, headers, add_cookie, filter_state, retry);
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
