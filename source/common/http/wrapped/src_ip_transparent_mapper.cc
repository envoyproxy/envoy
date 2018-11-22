#include "common/http/wrapped/src_ip_transparent_mapper.h"

namespace Envoy {
namespace Http {

SrcIpTransparentMapper::SrcIpTransparentMapper(ConnPoolBuilder builder, size_t max_num_pools)
    : builder_(builder), max_num_pools_(max_num_pools) {}

SrcIpTransparentMapper::~SrcIpTransparentMapper() = default;

ConnectionPool::Instance* SrcIpTransparentMapper::assignPool(const Upstream::LoadBalancerContext&) {
  if (active_pools_.size() >= max_num_pools_) {
    return nullptr;
  }

  auto pool = builder_();
  ConnectionPool::Instance* to_return = pool.get();
  active_pools_.emplace_back(std::move(pool));

  return to_return;
}

void SrcIpTransparentMapper::addIdleCallback(IdleCb) {}

void SrcIpTransparentMapper::drainPools() {
  for (auto& pool : active_pools_) {
    pool->drainConnections();
  }
}

bool SrcIpTransparentMapper::allPoolsIdle() const { return active_pools_.empty(); }

} // namespace Http
} // namespace Envoy
