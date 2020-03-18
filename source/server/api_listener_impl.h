#pragma once

#include <memory>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/server/api_listener.h"
#include "envoy/server/drain_manager.h"
#include "envoy/server/filter_config.h"
#include "envoy/server/listener_manager.h"
#include "envoy/stats/scope.h"

#include "common/common/empty_string.h"
#include "common/common/logger.h"
#include "common/http/conn_manager_impl.h"
#include "common/init/manager_impl.h"
#include "common/stream_info/stream_info_impl.h"

#include "server/filter_chain_manager_impl.h"

namespace Envoy {
namespace Server {

class ListenerManagerImpl;

/**
 * Base class all ApiListeners.
 */
class ApiListenerImplBase : public ApiListener,
                            public Network::DrainDecision,
                            Logger::Loggable<Logger::Id::http> {
public:
  // TODO(junr03): consider moving Envoy Mobile's SyntheticAddressImpl to Envoy in order to return
  // that rather than this semi-real one.
  const Network::Address::InstanceConstSharedPtr& address() const { return address_; }

  // ApiListener
  absl::string_view name() const override { return name_; }

  // Network::DrainDecision
  // TODO(junr03): hook up draining to listener state management.
  bool drainClose() const override { return false; }

protected:
  ApiListenerImplBase(const envoy::config::listener::v3::Listener& config,
                      ListenerManagerImpl& parent, const std::string& name);

  // Synthetic class that acts as a stub Network::ReadFilterCallbacks.
  // TODO(junr03): if we are able to separate the Network Filter aspects of the
  // Http::ConnectionManagerImpl from the http management aspects of it, it is possible we would not
  // need this and the SyntheticConnection stub anymore.
  class SyntheticReadCallbacks : public Network::ReadFilterCallbacks {
  public:
    SyntheticReadCallbacks(ApiListenerImplBase& parent)
        : parent_(parent), connection_(SyntheticConnection(*this)) {}

    // Network::ReadFilterCallbacks
    void continueReading() override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
    void injectReadDataToFilterChain(Buffer::Instance&, bool) override {
      NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
    }
    Upstream::HostDescriptionConstSharedPtr upstreamHost() override { return nullptr; }
    void upstreamHost(Upstream::HostDescriptionConstSharedPtr) override {
      NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
    }
    Network::Connection& connection() override { return connection_; }

    // Synthetic class that acts as a stub for the connection backing the
    // Network::ReadFilterCallbacks.
    class SyntheticConnection : public Network::Connection {
    public:
      SyntheticConnection(SyntheticReadCallbacks& parent)
          : parent_(parent), stream_info_(parent_.parent_.factory_context_.timeSource()),
            options_(std::make_shared<std::vector<Network::Socket::OptionConstSharedPtr>>()) {}

      void raiseConnectionEvent(Network::ConnectionEvent event);

      // Network::FilterManager
      void addWriteFilter(Network::WriteFilterSharedPtr) override {
        NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
      }
      void addFilter(Network::FilterSharedPtr) override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
      void addReadFilter(Network::ReadFilterSharedPtr) override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
      bool initializeReadFilters() override { return true; }

      // Network::Connection
      void addConnectionCallbacks(Network::ConnectionCallbacks& cb) override {
        callbacks_.push_back(&cb);
      }
      void addBytesSentCallback(Network::Connection::BytesSentCb) override {
        NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
      }
      void enableHalfClose(bool) override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
      void close(Network::ConnectionCloseType) override {}
      Event::Dispatcher& dispatcher() override {
        return parent_.parent_.factory_context_.dispatcher();
      }
      uint64_t id() const override { return 12345; }
      std::string nextProtocol() const override { return EMPTY_STRING; }
      void noDelay(bool) override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
      void readDisable(bool) override {}
      void detectEarlyCloseWhenReadDisabled(bool) override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
      bool readEnabled() const override { return true; }
      const Network::Address::InstanceConstSharedPtr& remoteAddress() const override {
        return parent_.parent_.address();
      }
      const Network::Address::InstanceConstSharedPtr& directRemoteAddress() const override {
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
      void write(Buffer::Instance&, bool) override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
      void setBufferLimits(uint32_t) override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
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
      std::list<Network::ConnectionCallbacks*> callbacks_;
    };

    ApiListenerImplBase& parent_;
    SyntheticConnection connection_;
  };

  const envoy::config::listener::v3::Listener& config_;
  ListenerManagerImpl& parent_;
  const std::string name_;
  Network::Address::InstanceConstSharedPtr address_;
  Stats::ScopePtr global_scope_;
  Stats::ScopePtr listener_scope_;
  FactoryContextImpl factory_context_;
  SyntheticReadCallbacks read_callbacks_;
};

/**
 * ApiListener that provides a handle to inject HTTP calls into Envoy via an
 * Http::ConnectionManager. Thus, it provides full access to Envoy's L7 features, e.g HTTP filters.
 */
class HttpApiListener : public ApiListenerImplBase {
public:
  HttpApiListener(const envoy::config::listener::v3::Listener& config, ListenerManagerImpl& parent,
                  const std::string& name);

  // ApiListener
  ApiListener::Type type() const override { return ApiListener::Type::HttpApiListener; }
  Http::ApiListenerOptRef http() override;
  void shutdown() override;

  Network::ReadFilterCallbacks& readCallbacksForTest() { return read_callbacks_; }

private:
  // Need to store the factory due to the shared_ptrs that need to be kept alive: date provider,
  // route config manager, scoped route config manager.
  std::function<Http::ApiListenerPtr()> http_connection_manager_factory_;
  // Http::ServerConnectionCallbacks is the API surface that this class provides via its handle().
  Http::ApiListenerPtr http_connection_manager_;
};

} // namespace Server
} // namespace Envoy
