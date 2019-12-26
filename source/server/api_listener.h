#pragma once

#include <memory>

#include "envoy/api/v2/lds.pb.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/server/drain_manager.h"
#include "envoy/server/filter_config.h"
#include "envoy/server/listener_manager.h"
#include "envoy/stats/scope.h"

#include "common/http/conn_manager_impl.h"
#include "common/common/empty_string.h"
#include "common/common/logger.h"
#include "common/init/manager_impl.h"
#include "common/stream_info/stream_info_impl.h"

#include "extensions/filters/network/http_connection_manager/config.h"

namespace Envoy {
namespace Server {

class ListenerManagerImpl;

/**
 * Listener that provides a handle to inject HTTP calls into envoy via the traditional HCM path.
 * Thus it provides full access to Envoy's L7 features, e.g HTTP filters.
 */
class HttpApiListener : public Configuration::FactoryContext,
                        public Network::DrainDecision,
                        Logger::Loggable<Logger::Id::http> {
public:
  HttpApiListener(const envoy::api::v2::Listener& config, ListenerManagerImpl& parent,
                  const std::string& name, ProtobufMessage::ValidationVisitor& validation_visitor);

  // see thoughts on what this API handle could be at server/listener_manager.h
  Http::ServerConnectionCallbacks* apiHandle();
  const Network::Address::InstanceConstSharedPtr& address() const { return address_; }

  // TODO(junr03): the majority of this surface could be moved out of the listener via some sort of
  // base class context.
  // Server::Configuration::FactoryContext
  AccessLog::AccessLogManager& accessLogManager() override;
  Upstream::ClusterManager& clusterManager() override;
  Event::Dispatcher& dispatcher() override;
  Network::DrainDecision& drainDecision() override;
  Grpc::Context& grpcContext() override;
  bool healthCheckFailed() override;
  Tracing::HttpTracer& httpTracer() override;
  Http::Context& httpContext() override;
  Init::Manager& initManager() override;
  const LocalInfo::LocalInfo& localInfo() const override;
  Envoy::Runtime::RandomGenerator& random() override;
  Envoy::Runtime::Loader& runtime() override;
  Stats::Scope& scope() override;
  Singleton::Manager& singletonManager() override;
  OverloadManager& overloadManager() override;
  ThreadLocal::Instance& threadLocal() override;
  Admin& admin() override;
  const envoy::api::v2::core::Metadata& listenerMetadata() const override;
  envoy::api::v2::core::TrafficDirection direction() const override;
  TimeSource& timeSource() override;
  ProtobufMessage::ValidationVisitor& messageValidationVisitor() override;
  Api::Api& api() override;
  ServerLifecycleNotifier& lifecycleNotifier() override;
  OptProcessContextRef processContext() override;
  Configuration::ServerFactoryContext& getServerFactoryContext() const override;
  Stats::Scope& listenerScope() override;

  bool drainClose() const override;

private:
  class SyntheticReadCallbacks : public Network::ReadFilterCallbacks {
  public:
    SyntheticReadCallbacks(HttpApiListener& parent)
        : parent_(parent), connection_(SyntheticConnection(*this)) {}

    // Network::ReadFilterCallbacks
    void continueReading() override {}
    void injectReadDataToFilterChain(Buffer::Instance&, bool) override {}
    Upstream::HostDescriptionConstSharedPtr upstreamHost() override { return nullptr; }
    void upstreamHost(Upstream::HostDescriptionConstSharedPtr) override {}
    Network::Connection& connection() override { return connection_; }

    class SyntheticConnection : public Network::Connection {
    public:
      SyntheticConnection(SyntheticReadCallbacks& parent)
          : parent_(parent), stream_info_(parent_.parent_.timeSource()),
            options_(std::make_shared<std::vector<Network::Socket::OptionConstSharedPtr>>()) {}

      // Network::FilterManager
      void addWriteFilter(Network::WriteFilterSharedPtr) override {}
      void addFilter(Network::FilterSharedPtr) override {}
      void addReadFilter(Network::ReadFilterSharedPtr) override {}
      bool initializeReadFilters() override { return true; }

      // Network::Connection
      void addConnectionCallbacks(Network::ConnectionCallbacks&) override {}
      void addBytesSentCallback(Network::Connection::BytesSentCb) override {}
      void enableHalfClose(bool) override {}
      void close(Network::ConnectionCloseType) override {}
      Event::Dispatcher& dispatcher() override { return parent_.parent_.dispatcher(); }
      uint64_t id() const override { return 12345; }
      std::string nextProtocol() const override { return EMPTY_STRING; }
      void noDelay(bool) override {}
      void readDisable(bool) override {}
      void detectEarlyCloseWhenReadDisabled(bool) override {}
      bool readEnabled() const override { return true; }
      const Network::Address::InstanceConstSharedPtr& remoteAddress() const override {
        return parent_.parent_.address();
      }
      absl::optional<Network::Connection::UnixDomainSocketPeerCredentials>
      unixSocketPeerCredentials() const override {
        return absl::nullopt;
      }
      const Network::Address::InstanceConstSharedPtr& localAddress() const override {
        return parent_.parent_.address();
      }
      void setConnectionStats(const Network::Connection::ConnectionStats&) override {}
      Ssl::ConnectionInfoConstSharedPtr ssl() const override { return nullptr; }
      absl::string_view requestedServerName() const override { return EMPTY_STRING; }
      State state() const override { return Network::Connection::State::Open; }
      void write(Buffer::Instance&, bool) override {}
      void setBufferLimits(uint32_t) override {}
      uint32_t bufferLimit() const override { return 65000; }
      bool localAddressRestored() const override { return false; }
      bool aboveHighWatermark() const override { return false; }
      const Network::ConnectionSocket::OptionsSharedPtr& socketOptions() const override {
        return options_;
      }
      StreamInfo::StreamInfo& streamInfo() override { return stream_info_; }
      const StreamInfo::StreamInfo& streamInfo() const override { return stream_info_; }
      void setDelayedCloseTimeout(std::chrono::milliseconds) override {}
      absl::string_view transportFailureReason() const override { return EMPTY_STRING; }

      SyntheticReadCallbacks& parent_;
      StreamInfo::StreamInfoImpl stream_info_;
      Network::ConnectionSocket::OptionsSharedPtr options_;
    };

    HttpApiListener& parent_;
    SyntheticConnection connection_;
  };

  const envoy::api::v2::Listener& config_;
  ListenerManagerImpl& parent_;
  const std::string name_;
  Network::Address::InstanceConstSharedPtr address_;
  ProtobufMessage::ValidationVisitor& validation_visitor_;
  Stats::ScopePtr global_scope_;
  Stats::ScopePtr listener_scope_;
  SyntheticReadCallbacks read_callbacks_;
  // Need to store the factory due to the shared_ptrs we need to keep alive: date provider, route
  // config manager, scoped route config manager.
  std::function<Http::ServerConnectionCallbacksPtr(Network::ReadFilterCallbacks&)>
      http_connection_manager_factory_;
  Http::ServerConnectionCallbacksPtr http_connection_manager_;
};

} // namespace Server
} // namespace Envoy
