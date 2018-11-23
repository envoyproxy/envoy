#include "common/http/wrapped/src_ip_transparent_mapper.h"

#include <algorithm>

#include "envoy/network/address.h"

using Envoy::Network::Address::Ip;

namespace Envoy {
namespace Http {

SrcIpTransparentMapper::SrcIpTransparentMapper(ConnPoolBuilder builder, size_t max_num_pools)
    : builder_(builder), max_num_pools_(max_num_pools) {}

SrcIpTransparentMapper::~SrcIpTransparentMapper() = default;

ConnectionPool::Instance*
SrcIpTransparentMapper::assignPool(const Upstream::LoadBalancerContext& context) {
  // TODO(klarose: handle downstreaConnection being empty. Maybe reserve a connpool for this case
  // and
  //                return it?)
  const auto& address = context.downstreamConnection()->remoteAddress();
  ASSERT(address.get());

  // TODO(klarose: handle address not being IP. Same as above.
  const auto ip = address->ip();
  ASSERT(ip);
  ConnectionPool::Instance* pool = findActivePool(*ip);
  if (pool) {
    return pool;
  }

  // If we didn't find an assigned pool for the provided context, and we're at the limit, fail.
  if (active_pools_.size() >= max_num_pools_) {
    return nullptr;
  }

  return allocateAndAssignPool(*ip);
}

void SrcIpTransparentMapper::addIdleCallback(IdleCb cb) { idle_callbacks_.push_back(cb); }

void SrcIpTransparentMapper::drainPools() {
  std::for_each(active_pools_.begin(), active_pools_.end(),
                [](auto& pool_iter) { pool_iter.second.instance_->drainConnections(); });
}

bool SrcIpTransparentMapper::allPoolsIdle() const { return active_pools_.empty(); }

ConnectionPool::Instance* SrcIpTransparentMapper::findActivePool(const Ip& ip) const {
  const Network::Address::Ipv4* v4_addr = ip.ipv4();
  if (v4_addr) {
    return findActiveV4(*v4_addr);
  }

  const Network::Address::Ipv6* v6_addr = ip.ipv6();
  ASSERT(v6_addr); // we expect an address to be either v4 or v6.
  return findActiveV6(*v6_addr);
}

ConnectionPool::Instance*
SrcIpTransparentMapper::findActiveV4(const Network::Address::Ipv4& v4_addr) const {

  const uint32_t addr_as_int = v4_addr.address();
  const auto& v4_iter = v4_assigned_.find(addr_as_int);

  if (v4_iter == v4_assigned_.end()) {
    return nullptr;
  }

  return v4_iter->second;
}

ConnectionPool::Instance*
SrcIpTransparentMapper::findActiveV6(const Network::Address::Ipv6& v6_addr) const {

  const absl::uint128 addr_as_int = v6_addr.address();
  const auto& v6_iter = v6_assigned_.find(addr_as_int);

  if (v6_iter == v6_assigned_.end()) {
    return nullptr;
  }

  return v6_iter->second;
}

ConnectionPool::Instance* SrcIpTransparentMapper::assignPool(const Ip& address) {
  // This should only be called if we've guranteed there's an idle pool.
  ASSERT(!idle_pools_.empty());

  PoolTracker next_up = std::move(idle_pools_.top());
  idle_pools_.pop();

  ConnectionPool::Instance* to_return = next_up.instance_.get();
  const Network::Address::Ipv4* v4_addr = address.ipv4();
  if (v4_addr) {
    const uint32_t raw = v4_addr->address();
    v4_assigned_[raw] = to_return;
    next_up.v4_address_ = raw;
  } else {
    const Network::Address::Ipv6* v6_addr = address.ipv6();
    const absl::uint128 raw = v6_addr->address();
    v6_assigned_[raw] = to_return;
    next_up.v6_address_ = raw;
  }

  active_pools_.emplace(to_return, std::move(next_up));
  return to_return;
}

ConnectionPool::Instance* SrcIpTransparentMapper::allocateAndAssignPool(const Ip& address) {
  if (idle_pools_.empty()) {
    // make sure we have a free pool!
    registerNewPool();
  }

  return assignPool(address);
}

void SrcIpTransparentMapper::registerNewPool() {
  auto new_pool = builder_();
  ConnectionPool::Instance* to_return = new_pool.get();
  // Register a callback so we may be notified when the pool is drained. We will consider it idle at
  // that point.
  to_return->addDrainedCallback([this, to_return] { poolDrained(*to_return); });
  PoolTracker tracker;
  tracker.instance_ = std::move(new_pool);
  idle_pools_.emplace(std::move(tracker));
}

void SrcIpTransparentMapper::poolDrained(ConnectionPool::Instance& instance) {
  const auto& active_iter = active_pools_.find(&instance);
  ASSERT(active_iter != active_pools_.end());

  // first clean up any assignments.
  if (active_iter->second.v4_address_.has_value()) {
    v4_assigned_.erase(active_iter->second.v4_address_.value());
  }

  // while these should be mutually exclusive, can't hurt to be safe.
  if (active_iter->second.v6_address_.has_value()) {
    v6_assigned_.erase(active_iter->second.v6_address_.value());
  }

  idle_pools_.emplace(std::move(active_iter->second));
  active_pools_.erase(active_iter);

  std::for_each(idle_callbacks_.begin(), idle_callbacks_.end(), [](auto& callback) { callback(); });
}

} // namespace Http
} // namespace Envoy
