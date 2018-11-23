#include "common/http/wrapped/src_ip_transparent_mapper.h"

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
  // TODO(klarose -- handle address not being IP. Same as above.
  const auto& ip = *address->ip();
  ConnectionPool::Instance* pool = findActivePool(ip);
  if (pool) {
    return pool;
  }

  if (active_pools_.size() >= max_num_pools_) {
    return nullptr;
  }

  auto new_pool = builder_();
  ConnectionPool::Instance* to_return = new_pool.get();
  active_pools_.emplace_back(std::move(new_pool));
  assignPool(ip, *to_return);

  return to_return;
}

void SrcIpTransparentMapper::addIdleCallback(IdleCb) {
  /* Callback when we get a callback from the pools we own
   * Register *that* callback on pool creation
   */
}

void SrcIpTransparentMapper::drainPools() {
  for (auto& pool : active_pools_) {
    pool->drainConnections();
  }
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

void SrcIpTransparentMapper::assignPool(const Ip& address, ConnectionPool::Instance& instance) {
  const Network::Address::Ipv4* v4_addr = address.ipv4();
  if (v4_addr) {
    v4_assigned_[v4_addr->address()] = &instance;
  } else {
    const Network::Address::Ipv6* v6_addr = address.ipv6();
    v6_assigned_[v6_addr->address()] = &instance;
  }
}

} // namespace Http
} // namespace Envoy
