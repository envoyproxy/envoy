#include "source/extensions/filters/http/bandwidth_share/fair_token_bucket_impl.h"

#include <algorithm>

#include "source/common/common/assert.h"
#include "source/common/common/lock_guard.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BandwidthShareFilter {
namespace FairTokenBucket {

Client::~Client() { bucket_->clientDestroyed(*this); }

Bucket::Bucket(uint64_t max_tokens, TimeSource& time_source,
               std::chrono::milliseconds fill_interval, double fill_rate)
    : impl_(max_tokens, time_source, fill_rate), time_source_(time_source),
      fill_interval_(fill_interval), max_tokens_(max_tokens) {
  Thread::LockGuard lock(mutex_);
  impl_.maybeReset(max_tokens);
}

std::shared_ptr<Bucket> Bucket::create(uint64_t max_tokens, TimeSource& time_source,
                                       std::chrono::milliseconds fill_interval, double fill_rate) {
  if (fill_rate <= 0) {
    fill_rate = max_tokens;
  }
  return std::shared_ptr<Bucket>{new Bucket(max_tokens, time_source, fill_interval, fill_rate)};
}

void Bucket::clientDestroyed(Client& client) {
  Thread::LockGuard lock(mutex_);
  clientDrained(client);
}

void Bucket::clientLimited(Client& client) {
  ASSERT(!client.known_limited_);
  client.known_limited_ = true;
  auto [it, inserted] = active_tenants_.emplace(client.tenant_name_, client.tenant_weight_);
  if (inserted) {
    active_tenants_total_weight_ += it->weight_;
  } else {
    it->active_clients_++;
  }
}

void Bucket::purgeDrainedTenants() {
  if (draining_tenants_.empty()) {
    return;
  }
  MonotonicTime now = time_source_.monotonicTime();
  auto end_expired = std::find_if(draining_tenants_.begin(), draining_tenants_.end(),
                                  [&now](auto& pair) { return pair.first > now; });
  for (auto it = draining_tenants_.begin(); it != end_expired; it++) {
    auto tenant_iterator = active_tenants_.find(absl::string_view{it->second});
    ASSERT(tenant_iterator != active_tenants_.end());
    tenant_iterator->active_clients_--;
    if (!tenant_iterator->active_clients_) {
      active_tenants_total_weight_ -= tenant_iterator->weight_;
      active_tenants_.erase(tenant_iterator);
    }
  }
  draining_tenants_.erase(draining_tenants_.begin(), end_expired);
}

void Bucket::clientDrained(Client& client) {
  if (!client.known_limited_) {
    return;
  }
  draining_tenants_.emplace_back(time_source_.monotonicTime() + fill_interval_,
                                 client.tenant_name_);
}

uint64_t Bucket::tokensPerInterval() const { return max_tokens_ * fill_interval_.count() / 1000; }

uint64_t Bucket::requestTokens(Client& client, uint64_t want_tokens) {
  Thread::LockGuard lock(mutex_);
  purgeDrainedTenants();
  held_tokens_ += impl_.consume(max_tokens_ - held_tokens_, /*allow_partial=*/true);
  uint64_t avail_tokens =
      (tokensPerInterval() < held_tokens_) ? held_tokens_ - tokensPerInterval() : 0;
  if (avail_tokens < want_tokens) {
    if (!client.known_limited_) {
      clientLimited(client);
      held_tokens_ -= avail_tokens;
      return avail_tokens;
    }
    auto it = active_tenants_.find(client.tenant_name_);
    ASSERT(it != active_tenants_.end());
    uint64_t tokens_to_tenant_per_interval =
        tokensPerInterval() * it->weight_ / active_tenants_total_weight_;
    uint64_t tokens_to_client_per_interval = tokens_to_tenant_per_interval / it->active_clients_;
    avail_tokens = std::min(held_tokens_, avail_tokens + tokens_to_client_per_interval);
  }
  if (avail_tokens >= want_tokens) {
    avail_tokens = want_tokens;
    clientDrained(client);
  }
  held_tokens_ -= avail_tokens;
  return avail_tokens;
}

uint64_t Client::consume(uint64_t tokens, bool allow_partial) {
  if (allow_partial == false) {
    IS_ENVOY_BUG("consume with allow_partial=false is not expected to be called");
    return 0;
  }
  return bucket_->requestTokens(*this, tokens);
}

uint64_t Client::consume(uint64_t, bool, std::chrono::milliseconds&) {
  IS_ENVOY_BUG("consume with time_to_next_token is not expected to be called");
  return 0;
}

std::chrono::milliseconds Client::nextTokenAvailable() {
  IS_ENVOY_BUG("nextTokenAvailable is not expected to be called");
  return {};
}

} // namespace FairTokenBucket
} // namespace BandwidthShareFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
