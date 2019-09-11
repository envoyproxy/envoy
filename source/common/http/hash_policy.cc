#include "common/http/hash_policy.h"

#include "common/http/utility.h"

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

  absl::optional<uint64_t> evaluate(const Network::Address::Instance*, const HeaderMap& headers,
                                    const HashPolicy::AddCookieCallback) const override {
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

  absl::optional<uint64_t> evaluate(const Network::Address::Instance*, const HeaderMap& headers,
                                    const HashPolicy::AddCookieCallback add_cookie) const override {
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
                                    const HeaderMap&,
                                    const HashPolicy::AddCookieCallback) const override {
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

HashPolicyImpl::HashPolicyImpl(
    absl::Span<const envoy::api::v2::route::RouteAction::HashPolicy* const> hash_policies) {
  // TODO(htuch): Add support for cookie hash policies, #1295
  hash_impls_.reserve(hash_policies.size());

  for (auto* hash_policy : hash_policies) {
    switch (hash_policy->policy_specifier_case()) {
    case envoy::api::v2::route::RouteAction::HashPolicy::kHeader:
      hash_impls_.emplace_back(
          new HeaderHashMethod(hash_policy->header().header_name(), hash_policy->terminal()));
      break;
    case envoy::api::v2::route::RouteAction::HashPolicy::kCookie: {
      absl::optional<std::chrono::seconds> ttl;
      if (hash_policy->cookie().has_ttl()) {
        ttl = std::chrono::seconds(hash_policy->cookie().ttl().seconds());
      }
      hash_impls_.emplace_back(new CookieHashMethod(hash_policy->cookie().name(),
                                                    hash_policy->cookie().path(), ttl,
                                                    hash_policy->terminal()));
      break;
    }
    case envoy::api::v2::route::RouteAction::HashPolicy::kConnectionProperties:
      if (hash_policy->connection_properties().source_ip()) {
        hash_impls_.emplace_back(new IpHashMethod(hash_policy->terminal()));
      }
      break;
    default:
      throw EnvoyException(
          fmt::format("Unsupported hash policy {}", hash_policy->policy_specifier_case()));
    }
  }
}

absl::optional<uint64_t>
HashPolicyImpl::generateHash(const Network::Address::Instance* downstream_addr,
                             const HeaderMap& headers, const AddCookieCallback add_cookie) const {
  absl::optional<uint64_t> hash;
  for (const HashMethodPtr& hash_impl : hash_impls_) {
    const absl::optional<uint64_t> new_hash =
        hash_impl->evaluate(downstream_addr, headers, add_cookie);
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
