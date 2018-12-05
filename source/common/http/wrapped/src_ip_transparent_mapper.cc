#include "common/http/wrapped/src_ip_transparent_mapper.h"

#include <algorithm>

#include "envoy/network/address.h"

#include "common/network/utility.h"

using Envoy::Network::Address::Ip;

namespace Envoy {
namespace Http {

SrcIpTransparentMapper::SrcIpTransparentMapper(ConnPoolBuilder builder, size_t max_num_pools)
    : builder_(builder), max_num_pools_(max_num_pools) {}

SrcIpTransparentMapper::~SrcIpTransparentMapper() = default;

ConnectionPool::Instance*
SrcIpTransparentMapper::assignPool(const Upstream::LoadBalancerContext& context) {
  const Ip& ip = getDownstreamSrcIp(context);
  ConnectionPool::Instance* pool = findActivePool(ip);
  if (pool) {
    return pool;
  }

  // If we didn't find an assigned pool for the provided context, and we're at the limit, fail.
  if (active_pools_.size() >= max_num_pools_) {
    return nullptr;
  }

  return allocateAndAssignPool(ip);
}

void SrcIpTransparentMapper::addIdleCallback(IdleCb cb) { idle_callbacks_.push_back(cb); }

void SrcIpTransparentMapper::drainPools() {
  ENVOY_LOG(debug, "Draining pools");
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

  ENVOY_LOG(debug, "Assigning a pool for {}", address.addressAsString());
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

  ConnectionPool::UpstreamSourceInformation info;
  // build up an Address::Instance object with the port set to 0 so we don't actually set the port
  // when establishing the connetion.
  info.source_address_ = Network::Utility::getAddressWithPort(
      *Network::Utility::copyInternetAddressAndPort(address), 0);
  to_return->setUpstreamSourceInformation(info);
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
  // that point. Note that it may call back immediately here. We should ignore it in that case.
  to_return->addDrainedCallback([this, to_return] { poolDrained(*to_return); });
  PoolTracker tracker;
  tracker.instance_ = std::move(new_pool);
  idle_pools_.emplace(std::move(tracker));
}

void SrcIpTransparentMapper::poolDrained(ConnectionPool::Instance& instance) {
  const auto& active_iter = active_pools_.find(&instance);
  if (active_iter == active_pools_.end()) {
    // The pool may call back immediately with it being drained, since it was just created.
    // ignore that case.
    ENVOY_LOG(debug, "Got a callback from an idle connection pool.");
    return;
  }

  // first clean up any assignments.
  if (active_iter->second.v4_address_.has_value()) {
    v4_assigned_.erase(active_iter->second.v4_address_.value());
    active_iter->second.v4_address_.reset();
  }

  // while these should be mutually exclusive, can't hurt to be safe.
  if (active_iter->second.v6_address_.has_value()) {
    v6_assigned_.erase(active_iter->second.v6_address_.value());
    active_iter->second.v6_address_.reset();
  }

  idle_pools_.emplace(std::move(active_iter->second));
  active_pools_.erase(active_iter);

  std::for_each(idle_callbacks_.begin(), idle_callbacks_.end(), [](auto& callback) { callback(); });
}

const Ip&
SrcIpTransparentMapper::getDownstreamSrcIp(const Upstream::LoadBalancerContext& context) const {
  const Network::Connection* downstream = context.downstreamConnection();
  // TODO (klarose): Is this really a good idea? This will just blow up the process... How much
  //       control do we have over the downstream? What else is reasonable error handling here...
  //       maybe we should just return an error alognside the instance pointer. Ugly API, but better
  //       than exploding? We can kill the downstream connection and log+count if it happens.
  if (!downstream) {
    throw BadDownstreamConnectionException("No downstream connection on transparent IP pool"
                                           " assignment");
  }
  const auto& address = downstream->remoteAddress();

  const auto ip = address->ip();
  if (!ip) {
    throw BadDownstreamConnectionException("Downstream connection is not IP!");
  }

  return *ip;
}
} // namespace Http
} // namespace Envoy
