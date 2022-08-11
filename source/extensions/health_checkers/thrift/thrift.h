#pragma once

#include <chrono>

#include "envoy/api/api.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/data/core/v3/health_check_event.pb.h"
#include "envoy/extensions/filters/network/thrift_proxy/v3/thrift_proxy.pb.h"
#include "envoy/extensions/filters/network/thrift_proxy/v3/thrift_proxy.pb.validate.h"
#include "envoy/extensions/health_checkers/thrift/v3/thrift.pb.h"
#include "envoy/router/router.h"

#include "source/common/network/filter_impl.h"
#include "source/common/upstream/health_checker_base_impl.h"
#include "source/extensions/filters/network/thrift_proxy/config.h"
#include "source/extensions/filters/network/thrift_proxy/router/router.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace ThriftHealthChecker {

using namespace Envoy::Extensions::NetworkFilters;
using namespace Envoy::Extensions::NetworkFilters::ThriftProxy;

/**
 * Thrift health checker implementation.
 */
class ThriftHealthChecker : public Upstream::HealthCheckerImplBase {
public:
  ThriftHealthChecker(const Upstream::Cluster& cluster,
                      const envoy::config::core::v3::HealthCheck& config,
                      const envoy::extensions::health_checkers::thrift::v3::Thrift& thrift_config,
                      Event::Dispatcher& dispatcher, Runtime::Loader& runtime,
                      Upstream::HealthCheckEventLoggerPtr&& event_logger, Api::Api& api);

protected:
  envoy::data::core::v3::HealthCheckerType healthCheckerType() const override {
    return envoy::data::core::v3::THRIFT;
  }

private:
  // friend class ThriftHealthCheckerTest;
  class Client;

  // Network::ClientConnection takes a shared pointer callback but we need a
  // unique DeferredDeletable pointer for connection management. Therefore we
  // need aditional wrapper class.
  struct ThriftSessionCallbacks : public Network::ConnectionCallbacks,
                                  public Network::ReadFilterBaseImpl {
    ThriftSessionCallbacks(Client& parent) : parent_(parent) {}

    // Network::ConnectionCallbacks
    void onEvent(Network::ConnectionEvent event) override { parent_.onEvent(event); }
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    // Network::ReadFilter
    Network::FilterStatus onData(Buffer::Instance& data, bool) override {
      parent_.onData(data);
      return Network::FilterStatus::StopIteration;
    }

    Client& parent_;
  };

  struct ThriftActiveHealthCheckSession;
  class Client : public Event::DeferredDeletable {
  public:
    Client(ThriftActiveHealthCheckSession& parent, Upstream::Host::CreateConnectionData& data,
           int32_t seq_id)
        : parent_(parent), connection_(std::move(data.connection_)),
          host_description_(data.host_description_), seq_id_(seq_id) {}

    // TODO: comment
    void start();

    // TODO: comment
    bool makeRequest();

    // TODO: comment
    void close();

    void onData(Buffer::Instance& data);

    void onEvent(Network::ConnectionEvent event) { parent_.onEvent(event); }

  private:
    int32_t sequenceId() {
      seq_id_++;
      if (seq_id_ == std::numeric_limits<int32_t>::max()) {
        seq_id_ = 0;
      }
      return seq_id_;
    }
    ThriftActiveHealthCheckSession& parent_;
    Network::ClientConnectionPtr connection_;
    Upstream::HostDescriptionConstSharedPtr host_description_;
    int32_t seq_id_{0};
    std::shared_ptr<ThriftSessionCallbacks> session_callbacks_;
  };

  using ClientPtr = std::unique_ptr<Client>;

  struct ThriftActiveHealthCheckSession : public ActiveHealthCheckSession,
                                          public Network::ConnectionCallbacks {
    ThriftActiveHealthCheckSession(ThriftHealthChecker& parent,
                                   const Upstream::HostSharedPtr& host);
    ~ThriftActiveHealthCheckSession() override;

    // ActiveHealthCheckSession
    void onInterval() override;
    void onTimeout() override;
    void onDeferredDelete() final;

    // Network::ConnectionCallbacks
    void onEvent(Network::ConnectionEvent event) override;
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    ThriftHealthChecker& parent_;
    const std::string& hostname_;
    bool expect_close_{false};

    ClientPtr client_;
  };

  using ThriftActiveHealthCheckSessionPtr = std::unique_ptr<ThriftActiveHealthCheckSession>;

  // HealthCheckerImplBase
  ActiveHealthCheckSessionPtr makeSession(Upstream::HostSharedPtr host) override {
    return std::make_unique<ThriftActiveHealthCheckSession>(*this, host);
  }

  const std::string method_name_;
  const TransportType transport_;
  const ProtocolType protocol_;
};

} // namespace ThriftHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
