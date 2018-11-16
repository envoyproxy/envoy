#pragma once

#include <utility>

#include "envoy/http/conn_pool.h"

namespace Envoy {
namespace Http {

class WrappedConnectionPool: public ConnectionPool::Instance {
public:
  WrappedConnectionPool(std::function<Http::ConnectionPool::InstancePtr()> builder);
  Http::Protocol protocol() const override;
  void addDrainedCallback(DrainedCb cb) override;
  void drainConnections() override;
  ConnectionPool::Cancellable* newStream(Http::StreamDecoder& response_decoder,
                                         ConnectionPool::Callbacks& callbacks) override;
private:
  std::function<Http::ConnectionPool::InstancePtr()> builder_;
};

} // namespace Http
} // namespace Envoy
