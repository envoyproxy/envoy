#pragma once

#include <unordered_map>
#include <vector>

#include "envoy/http/conn_pool.h"
#include "envoy/http/connection_mapper.h"

namespace Envoy {

namespace Network {
namespace Addresss {
class Ip;
}
} // namespace Network

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
  //! Finds the connection pool associated with @c address if one exists
  //! @param address The IP Address to search on. May be IPv4 or IPv6.
  //! @return nullptr if no pool is found, or the pool associated with the provided address.
  ConnectionPool::Instance* findActivePool(const Network::Address::Ip& address) const;

  //! Assigns a pool to the provided address. While the pool is assigned, any new assignments from
  //! this address will be to the provided pool.
  //! @param address The ip address to which to assign the pool. May be IPv4 or IPv6.
  //! @param pool The connection pool to assign.
  void assignPool(const Network::Address::Ip& address, ConnectionPool::Instance& pool);

  ConnPoolBuilder builder_;
  const size_t max_num_pools_;
  std::vector<ConnectionPool::InstancePtr> active_pools_;
  std::unordered_map<uint32_t, ConnectionPool::Instance*> v4_assigned_;
};

} // namespace Http
} // namespace Envoy
