#pragma once

#include <functional>
#include <memory>

#include "envoy/http/conn_pool.h"

#include "common/http/connection_mapper.h"

namespace Envoy {
namespace Http {

class WrappedConnectionPool : public ConnectionPool::Instance {
public:
  WrappedConnectionPool(std::unique_ptr<ConnectionMapper> mapper, Protocol protocol);
  Protocol protocol() const override;
  void addDrainedCallback(DrainedCb cb) override;
  void drainConnections() override;
  ConnectionPool::Cancellable* newStream(Http::StreamDecoder& response_decoder,
                                         ConnectionPool::Callbacks& callbacks) override;

  ConnectionPool::Cancellable* newStream(Http::StreamDecoder& response_decoder,
                                         ConnectionPool::Callbacks& callbacks,
                                         const Upstream::LoadBalancerContext& context) override;

private:
  std::unique_ptr<ConnectionMapper> mapper_;
  Protocol protocol_;
};

} // namespace Http
} // namespace Envoy
