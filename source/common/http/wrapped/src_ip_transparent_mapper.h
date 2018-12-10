#pragma once

#include <stack>
#include <unordered_map>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/http/conn_pool.h"
#include "envoy/http/connection_mapper.h"

#include "absl/hash/hash.h"
#include "absl/numeric/int128.h"
#include "absl/types/optional.h"

namespace Envoy {

namespace Network {
namespace Addresss {
class Ip;
}
} // namespace Network

namespace Http {
class SrcIpTransparentMapper : Logger::Loggable<Logger::Id::http>, public ConnectionMapper {
public:
  SrcIpTransparentMapper(ConnPoolBuilder builder, size_t max_num_pools);
  ~SrcIpTransparentMapper();

  ConnectionPool::Instance* assignPool(const Upstream::LoadBalancerContext& context) override;
  void addIdleCallback(IdleCb callback) override;
  void drainPools() override;
  bool allPoolsIdle() const override;

private:
  /**
   * Finds the connection pool associated with @c address if one exists
   * @param address The IP Address to search on. May be IPv4 or IPv6.
   * @return nullptr if no pool is found, or the pool associated with the provided address.
   */
  ConnectionPool::Instance* findActivePool(const Network::Address::Ip& address) const;

  /**
   * Finds the connection pool corresponding to the provided IPv4 address
   */
  ConnectionPool::Instance* findActiveV4(const Network::Address::Ipv4& v4_addr) const;

  /**
   * Finds the connection pool corresponding to the provided IPv6 address
   */
  ConnectionPool::Instance* findActiveV6(const Network::Address::Ipv6& v6_addr) const;

  /**
   * Assigns a pool to the provided address. While the pool is assigned, any new assignments from
   * this address will be to that pool.
   * @pre: !idle_pools_.empty()
   * @param address The ip address to which to assign the pool. May be IPv4 or IPv6.
   * @return The assigned pool
   */
  ConnectionPool::Instance* assignPool(const Network::Address::Ip& address);

  /**
   * Allocates a pool, creating one if necessary, then assigns it to the provided IP address.
   * @param address The IP address to assign. May be v4 or v6.
   * @return the connection pool to use.
   */
  ConnectionPool::Instance* allocateAndAssignPool(const Network::Address::Ip& address);

  /**
   *  Creates a new pool and registers it to be tracked internally.
   */
  void registerNewPool();

  void poolDrained(ConnectionPool::Instance& drained_pool);

  /**
   * Get the downstream source IP from the provided context, or throws an exception if it can't.
   * @throws BadDownstreamConnectionException if the context didn't have a valid downstream IP.
   * @return a reference to the context's source IP.
   */
  const Network::Address::Ip&
  getDownstreamSrcIp(const Upstream::LoadBalancerContext& context) const;

  /**
   * Allows us to track a given connection pool -- i.e. is it pending or active. Is it assigned
   * a v4 or v6 address?
   */
  struct PoolTracker {
    ConnectionPool::InstancePtr instance_;
    absl::optional<absl::uint128> v6_address_;
    absl::optional<uint32_t> v4_address_;
  };

  ConnPoolBuilder builder_;
  const size_t max_num_pools_;
  std::vector<IdleCb> idle_callbacks_;
  std::unordered_map<ConnectionPool::Instance*, PoolTracker> active_pools_;
  std::stack<PoolTracker, std::vector<PoolTracker>> idle_pools_;
  std::unordered_map<uint32_t, ConnectionPool::Instance*> v4_assigned_;
  // TODO(klarose): research a better hash. V6 addresses aren't always that random in all bits.
  std::unordered_map<absl::uint128, ConnectionPool::Instance*, absl::Hash<absl::uint128>>
      v6_assigned_;
};

class BadDownstreamConnectionException : public EnvoyException {
public:
  BadDownstreamConnectionException(const std::string& message) : EnvoyException(message) {}
};

} // namespace Http
} // namespace Envoy
