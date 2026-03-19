#include "source/extensions/filters/http/bandwidth_share/fair_token_bucket_impl.h"

#include <algorithm>

#include "source/common/common/assert.h"
#include "source/common/common/lock_guard.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BandwidthShareFilter {
namespace FairTokenBucket {

Factory::Factory(uint64_t max_tokens, TimeSource& time_source,
                 std::chrono::milliseconds spill_frequency, double fill_rate)
    : impl_(max_tokens, time_source, fill_rate), time_source_(time_source),
      spill_frequency_(spill_frequency) {
  Thread::LockGuard lock(mutex_);
  impl_.maybeReset(max_tokens);
}

std::shared_ptr<Factory> Factory::create(uint64_t max_tokens, TimeSource& time_source,
                                         std::chrono::milliseconds spill_frequency,
                                         double fill_rate) {
  if (fill_rate <= 0) {
    fill_rate = max_tokens;
  }
  return std::shared_ptr<Factory>{new Factory(max_tokens, time_source, spill_frequency, fill_rate)};
}

std::shared_ptr<Tenant> Factory::getTenant(absl::string_view tenant_name, uint64_t weight) {
  std::shared_ptr<Tenant> tenant;
  {
    Thread::LockGuard lock(tenants_mutex_);
    auto it = active_tenants_.find(tenant_name);
    if (it == active_tenants_.end() || (tenant = it->second.lock()) == nullptr) {
      tenant = std::make_shared<Tenant>(tenant_name, shared_from_this(), weight);
      if (it != active_tenants_.end()) {
        // Replace the entry rather than update in-place so the key always points
        // to the current live Tenant::name_.
        active_tenants_.erase(it);
      }
      active_tenants_.emplace(tenant->name_, tenant);
      return tenant;
    }
  }
  {
    Thread::LockGuard lock(mutex_);
    tenant->weight_ = weight;
  }
  return tenant;
}

Tenant::~Tenant() {
  Thread::LockGuard lock(parent_->tenants_mutex_);
  // It's possible that this has had its shared_ptr refcount decremented
  // and then a new tenant got inserted in its place, though extremely unlikely -
  // so only remove from the map if the weak_ptr in the map is expired.
  auto it = parent_->active_tenants_.find(name_);
  if (it != parent_->active_tenants_.end() && it->second.expired()) {
    parent_->active_tenants_.erase(it);
  }
}

std::shared_ptr<Request> Tenant::makeRequest() {
  return std::shared_ptr<Request>(new Request(shared_from_this()));
}

class SpillHandler {
public:
  void prepare(Factory& factory) ABSL_EXCLUSIVE_LOCKS_REQUIRED(&Factory::mutex_) {
    want_tokens_ = 0;
    total_weight_ = 0;
    for (std::shared_ptr<Tenant>& tenant : factory.waiting_tenants_) {
      prepareTenant(*tenant);
      total_weight_ += tenant->weight_;
      want_tokens_ += tenant->want_tokens_;
    }
  }
  void prepareTenant(Tenant& tenant) ABSL_EXCLUSIVE_LOCKS_REQUIRED(&Factory::mutex_) {
    tenant.want_tokens_ = 0;
    for (std::shared_ptr<Request>& request : tenant.waiting_requests_) {
      // should not be in queue if not waiting for tokens.
      ASSERT(request->queued_tokens_ > 0);
      tenant.want_tokens_ += request->queued_tokens_;
    }
  }
  // Returns true if finished.
  bool spill(Factory& factory, uint64_t tokens) ABSL_EXCLUSIVE_LOCKS_REQUIRED(&Factory::mutex_) {
    if (tokens == want_tokens_) {
      // If we have enough tokens to drain the queue, simply drain it.
      for (std::shared_ptr<Tenant>& tenant : factory.waiting_tenants_) {
        spillToTenant(*tenant, tenant->want_tokens_);
      }
      factory.waiting_tenants_.clear();
      return true;
    }
    while (tokens) {
      uint64_t tokens_per_weight = tokens / total_weight_;
      // Give the unweighted remainder to whoever is first in the queue and needs more.
      uint64_t spare_tokens = tokens - tokens_per_weight * total_weight_;
      std::erase_if(
          factory.waiting_tenants_,
          [&](std::shared_ptr<Tenant>& tenant) ABSL_EXCLUSIVE_LOCKS_REQUIRED(&Factory::mutex_) {
            uint64_t to_consume =
                std::min(tokens_per_weight * tenant->weight_, tenant->want_tokens_);
            if (spare_tokens > 0 && tenant->want_tokens_ > to_consume) {
              uint64_t also_consume = std::min(spare_tokens, tenant->want_tokens_ - to_consume);
              spare_tokens -= also_consume;
              to_consume += also_consume;
            }
            tokens -= to_consume;
            if (spillToTenant(*tenant, to_consume)) {
              total_weight_ -= tenant->weight_;
              return true;
            }
            return false;
          });
    }
    return false;
  }
  // Returns true if finished.
  bool spillToTenant(Tenant& tenant, uint64_t tokens)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&Factory::mutex_) {
    ASSERT(tokens <= tenant.want_tokens_);
    if (tokens == tenant.want_tokens_) {
      // We have enough to clear the queue, so just do it.
      for (std::shared_ptr<Request>& request : tenant.waiting_requests_) {
        spillToRequest(*request, request->queued_tokens_);
      }
      tenant.waiting_requests_.clear();
      return true;
    }
    while (tokens) {
      uint64_t tokens_per_request = tokens / tenant.waiting_requests_.size();
      // Give the unweighted remainder to whoever is first in the queue and needs more.
      uint64_t spare_tokens = tokens - tokens_per_request * tenant.waiting_requests_.size();
      std::erase_if(tenant.waiting_requests_,
                    [&](const std::shared_ptr<Request>& request)
                        ABSL_EXCLUSIVE_LOCKS_REQUIRED(&Factory::mutex_) {
                          uint64_t to_consume =
                              std::min(tokens_per_request, request->queued_tokens_);
                          if (spare_tokens > 0 && request->queued_tokens_ > to_consume) {
                            uint64_t also_consume =
                                std::min(spare_tokens, request->queued_tokens_ - to_consume);
                            spare_tokens -= also_consume;
                            to_consume += also_consume;
                          }
                          tokens -= to_consume;
                          tenant.want_tokens_ -= to_consume;
                          return spillToRequest(*request, to_consume);
                        });
    }
    return false;
  }
  // Returns true if finished.
  bool spillToRequest(Request& request, uint64_t tokens)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&Factory::mutex_) {
    request.held_tokens_ += tokens;
    request.queued_tokens_ -= tokens;
    return request.queued_tokens_ == 0;
  }

  uint64_t total_weight_ = 0;
  uint64_t want_tokens_ = 0;
};

void Factory::spill() {
  if (!next_spill_) {
    return;
  }
  if (time_source_.monotonicTime() < *next_spill_) {
    return;
  }
  SpillHandler spill_handler;
  spill_handler.prepare(*this);
  uint64_t tokens = impl_.consume(spill_handler.want_tokens_, /* allow_partial = */ true);
  if (spill_handler.spill(*this, tokens)) {
    next_spill_ = absl::nullopt;
  } else {
    next_spill_ = time_source_.monotonicTime() + spill_frequency_;
  }
}

void Factory::addToQueue(std::shared_ptr<Tenant> tenant) {
  waiting_tenants_.push_back(std::move(tenant));
}

void Tenant::addToQueue(std::shared_ptr<Request> request) {
  if (waiting_requests_.empty()) {
    // This is the first request on this tenant, so add the tenant to the
    // parent queue.
    parent_->addToQueue(shared_from_this());
  }
  waiting_requests_.push_back(std::move(request));
}

uint64_t Request::consume(uint64_t want_tokens) {
  Thread::LockGuard lock(tenant_->parent_->mutex_);
  tenant_->parent_->spill();
  uint64_t got = 0;
  if (held_tokens_) {
    got += std::min(held_tokens_, want_tokens);
    held_tokens_ -= got;
    if (got == want_tokens) {
      return got;
    }
  }
  if (!tenant_->parent_->next_spill_) {
    // Nonblocking, just get the tokens directly from the source bucket.
    got += tenant_->parent_->impl_.consume(want_tokens - got, /* allow_partial= */ true);
    if (got == want_tokens) {
      return got;
    }
    // If the full set of tokens was not received, the limit has been hit - start
    // the secondary process of splitting tokens between requests.
    queued_tokens_ = want_tokens - got;
    tenant_->addToQueue(shared_from_this());
    tenant_->parent_->next_spill_ =
        tenant_->parent_->time_source_.monotonicTime() + tenant_->parent_->spill_frequency_;
    return got;
  }
  if (queued_tokens_ > 0) {
    // Request was already in the queue, update how much this request wants, in case
    // it has increased.
    queued_tokens_ = std::max(want_tokens - got, queued_tokens_);
    return got;
  }
  queued_tokens_ = want_tokens - got;
  tenant_->addToQueue(shared_from_this());
  return got;
}

void Request::cancel() {
  Thread::LockGuard lock(tenant_->parent_->mutex_);
  if (queued_tokens_) {
    tenant_->removeFromQueue(shared_from_this());
  }
  tenant_.reset();
}

void Tenant::removeFromQueue(std::shared_ptr<Request> req) {
  // This is a linear search, but deletions of requests that are waiting on
  // data *and* are being bandwidth-limited should be very rare.
  auto it = std::find_if(waiting_requests_.begin(), waiting_requests_.end(),
                         [&](std::shared_ptr<Request>& p) { return req.get() == p.get(); });
  // removeRequest should not be called if the request is not waiting,
  // so it should always be found.
  ASSERT(it != waiting_requests_.end());
  waiting_requests_.erase(it);
  if (waiting_requests_.empty()) {
    parent_->removeFromQueue(shared_from_this());
  }
}

void Factory::removeFromQueue(std::shared_ptr<Tenant> tenant) {
  // This is a linear search, but deletions of requests that are waiting on
  // data *and* are being bandwidth-limited should be very rare.
  auto it = std::find(waiting_tenants_.begin(), waiting_tenants_.end(), tenant);
  // removeTenant should not be called if the tenant is not waiting,
  // so it should always be found.
  ASSERT(it != waiting_tenants_.end());
  waiting_tenants_.erase(it);
}

Client::Client(Factory& factory, absl::string_view tenant_name, uint64_t weight)
    : request_(factory.getTenant(tenant_name, weight)->makeRequest()) {
  ASSERT(weight > 0);
}

Client::~Client() { request_->cancel(); }

uint64_t Client::consume(uint64_t tokens, bool allow_partial) {
  if (allow_partial == false) {
    IS_ENVOY_BUG("consume with allow_partial=false is not expected to be called");
    return 0;
  }
  return request_->consume(tokens);
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
