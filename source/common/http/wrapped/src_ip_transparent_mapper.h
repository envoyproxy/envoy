#pragma once

#include <vector>

#include "envoy/http/conn_pool.h"
#include "envoy/http/connection_mapper.h"

namespace Envoy {
namespace Http {
class SrcIpTransparentMapper : public ConnectionMapper {
public:
  SrcIpTransparentMapper(ConnPoolBuilder builder, uint64_t max_num_pools);
  ~SrcIpTransparentMapper();
  ConnectionPool::Instance* assignPool(const Upstream::LoadBalancerContext& context) override;
  void addIdleCallback(IdleCb callback) override;
  void drainPools() override;
  bool allPoolsIdle() const override;

private:
  ConnPoolBuilder builder_;
  const size_t max_num_pools_;
  std::vector<ConnectionPool::InstancePtr> active_pools_;
};

} // namespace Http
} // namespace Envoy
