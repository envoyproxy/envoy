#pragma once

#include <memory>
#include <ostream>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/network/socket.h"
#include "envoy/server/api_listener.h"
#include "envoy/server/filter_config.h"
#include "envoy/server/instance.h"
#include "envoy/server/listener_manager.h"
#include "envoy/stats/scope.h"

#include "source/common/common/empty_string.h"
#include "source/common/common/logger.h"
#include "source/common/http/conn_manager_impl.h"
#include "source/common/init/manager_impl.h"
#include "source/common/network/socket_impl.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/server/factory_context_impl.h"

namespace Envoy {
namespace Server {

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
  Common::CallbackHandlePtr addOnDrainCloseCb(DrainCloseCb) const override {
    IS_ENVOY_BUG("Unexpected call to addOnDrainCloseCb");
    return nullptr;
  }

protected:
  ApiListenerImplBase(Network::Address::InstanceConstSharedPtr&& address,
                      const envoy::config::listener::v3::Listener& config, Server::Instance& server,
                      const std::string& name);

  // Synthetic class that acts as a stub Network::ReadFilterCallbacks.
  // TODO(junr03): if we are able to separate the Network Filter aspects of the
  // Http::ConnectionManagerImpl from the http management aspects of it, it is possible we would not
  // need this and the SyntheticConnection stub anymore.
  class SyntheticReadCallbacks : public Network::ReadFilterCallbacks {
  public:
    SyntheticReadCallbacks(ApiListenerImplBase& parent, Event::Dispatcher& dispatcher)
        : parent_(parent), connection_(SyntheticConnection(*this, dispatcher)) {}

    // Network::ReadFilterCallbacks
    void continueReading() override { IS_ENVOY_BUG("Unexpected call to continueReading"); }
    void injectReadDataToFilterChain(Buffer::Instance&, bool) override {
      IS_ENVOY_BUG("Unexpected call to injectReadDataToFilterChain");
    }
    bool startUpstreamSecureTransport() override {
      IS_ENVOY_BUG("Unexpected call to startUpstreamSecureTransport");
      return false;
    }
    Upstream::HostDescriptionConstSharedPtr upstreamHost() override { return nullptr; }
    void upstreamHost(Upstream::HostDescriptionConstSharedPtr) override {
      IS_ENVOY_BUG("Unexpected call to upstreamHost");
    }
    Network::Connection& connection() override { return connection_; }
    const Network::Socket& socket() override { PANIC("not implemented"); }

    // Synthetic class that acts as a stub for the connection backing the
    // Network::ReadFilterCallbacks.
    class SyntheticConnection : public Network::Connection {
    public:
      SyntheticConnection(SyntheticReadCallbacks& parent, Event::Dispatcher& dispatcher)
          : parent_(parent), dispatcher_(dispatcher),
            connection_info_provider_(std::make_shared<Network::ConnectionInfoSetterImpl>(
                parent.parent_.address_, parent.parent_.address_)),
            stream_info_(parent_.parent_.factory_context_.serverFactoryContext().timeSource(),
                         connection_info_provider_, StreamInfo::FilterState::LifeSpan::Connection),
            options_(std::make_shared<std::vector<Network::Socket::OptionConstSharedPtr>>()) {}

      void raiseConnectionEvent(Network::ConnectionEvent event);

      // Network::FilterManager
      void addWriteFilter(Network::WriteFilterSharedPtr) override {
        IS_ENVOY_BUG("Unexpected function call");
      }
      void addFilter(Network::FilterSharedPtr) override {
        IS_ENVOY_BUG("Unexpected function call");
      }
      void addReadFilter(Network::ReadFilterSharedPtr) override {
        IS_ENVOY_BUG("Unexpected function call");
      }
      void removeReadFilter(Network::ReadFilterSharedPtr) override {
        IS_ENVOY_BUG("Unexpected function call");
      }
      bool initializeReadFilters() override { return true; }

      // Network::Connection
      void addConnectionCallbacks(Network::ConnectionCallbacks& cb) override {
        callbacks_.push_back(&cb);
      }
      void removeConnectionCallbacks(Network::ConnectionCallbacks& cb) override {
        callbacks_.remove(&cb);
      }
      void addBytesSentCallback(Network::Connection::BytesSentCb) override {
        IS_ENVOY_BUG("Unexpected function call");
      }
      void enableHalfClose(bool) override { IS_ENVOY_BUG("Unexpected function call"); }
      bool isHalfCloseEnabled() const override {
        IS_ENVOY_BUG("Unexpected function call");
        return false;
      }
      void close(Network::ConnectionCloseType) override {}
      void close(Network::ConnectionCloseType, absl::string_view) override {}
      Network::DetectedCloseType detectedCloseType() const override {
        return Network::DetectedCloseType::Normal;
      };
      Event::Dispatcher& dispatcher() const override { return dispatcher_; }
      uint64_t id() const override { return 12345; }
      void hashKey(std::vector<uint8_t>&) const override {}
      std::string nextProtocol() const override { return EMPTY_STRING; }
      void noDelay(bool) override { IS_ENVOY_BUG("Unexpected function call"); }
      ReadDisableStatus readDisable(bool) override { return ReadDisableStatus::NoTransition; }
      void detectEarlyCloseWhenReadDisabled(bool) override {
        IS_ENVOY_BUG("Unexpected function call");
      }
      bool readEnabled() const override { return true; }
      Network::ConnectionInfoSetter& connectionInfoSetter() override {
        return *connection_info_provider_;
      }
      const Network::ConnectionInfoProvider& connectionInfoProvider() const override {
        return *connection_info_provider_;
      }
      Network::ConnectionInfoProviderSharedPtr connectionInfoProviderSharedPtr() const override {
        return connection_info_provider_;
      }
      absl::optional<Network::Connection::UnixDomainSocketPeerCredentials>
      unixSocketPeerCredentials() const override {
        return absl::nullopt;
      }
      void setConnectionStats(const Network::Connection::ConnectionStats&) override {}
      Ssl::ConnectionInfoConstSharedPtr ssl() const override { return nullptr; }
      absl::string_view requestedServerName() const override { return EMPTY_STRING; }
      State state() const override { return Network::Connection::State::Open; }
      bool connecting() const override { return false; }
      void write(Buffer::Instance&, bool) override { IS_ENVOY_BUG("Unexpected function call"); }
      void setBufferLimits(uint32_t) override { IS_ENVOY_BUG("Unexpected function call"); }
      uint32_t bufferLimit() const override { return 65000; }
      bool aboveHighWatermark() const override { return false; }
      const Network::ConnectionSocket::OptionsSharedPtr& socketOptions() const override {
        return options_;
      }
      StreamInfo::StreamInfo& streamInfo() override { return stream_info_; }
      const StreamInfo::StreamInfo& streamInfo() const override { return stream_info_; }
      void setDelayedCloseTimeout(std::chrono::milliseconds) override {}
      absl::string_view transportFailureReason() const override { return EMPTY_STRING; }
      absl::string_view localCloseReason() const override { return EMPTY_STRING; }
      bool startSecureTransport() override {
        IS_ENVOY_BUG("Unexpected function call");
        return false;
      }
      absl::optional<std::chrono::milliseconds> lastRoundTripTime() const override { return {}; }
      void configureInitialCongestionWindow(uint64_t, std::chrono::microseconds) override {}
      absl::optional<uint64_t> congestionWindowInBytes() const override { return {}; }
      // ScopeTrackedObject
      void dumpState(std::ostream& os, int) const override { os << "SyntheticConnection"; }

      SyntheticReadCallbacks& parent_;
      Event::Dispatcher& dispatcher_;
      Network::ConnectionInfoSetterSharedPtr connection_info_provider_;
      StreamInfo::StreamInfoImpl stream_info_;
      Network::ConnectionSocket::OptionsSharedPtr options_;
      std::list<Network::ConnectionCallbacks*> callbacks_;
    };

    ApiListenerImplBase& parent_;
    SyntheticConnection connection_;
  };

  const envoy::config::listener::v3::Listener& config_;
  const std::string name_;
  Network::Address::InstanceConstSharedPtr address_;
  FactoryContextImpl factory_context_;
};

/**
 * ApiListener that provides a handle to inject HTTP calls into Envoy via an
 * Http::ConnectionManager. Thus, it provides full access to Envoy's L7 features, e.g HTTP filters.
 */
class HttpApiListener : public ApiListenerImplBase {
public:
  // Class to wrap an Http::ApiListener and the associated SyntheticReadCallbacks to ensure that
  // both objects have the same lifetime.
  //
  // Public for testing.
  class ApiListenerWrapper : public Http::ApiListener {
  public:
    ApiListenerWrapper(HttpApiListener& parent, Event::Dispatcher& dispatcher)
        : read_callbacks_(parent, dispatcher),
          http_connection_manager_(parent.http_connection_manager_factory_(read_callbacks_)) {}
    ~ApiListenerWrapper() override;

    Http::RequestDecoderHandlePtr newStreamHandle(Http::ResponseEncoder& response_encoder,
                                                  bool is_internally_created = false) override;

    SyntheticReadCallbacks& readCallbacks() { return read_callbacks_; }

  private:
    SyntheticReadCallbacks read_callbacks_;
    Http::ApiListenerPtr http_connection_manager_;
  };

  // ApiListener
  ApiListener::Type type() const override { return ApiListener::Type::HttpApiListener; }
  Http::ApiListenerPtr createHttpApiListener(Event::Dispatcher& dispatcher) override;
  static absl::StatusOr<std::unique_ptr<HttpApiListener>>
  create(const envoy::config::listener::v3::Listener& config, Server::Instance& server,
         const std::string& name);

private:
  HttpApiListener(Network::Address::InstanceConstSharedPtr&& address,
                  const envoy::config::listener::v3::Listener& config, Server::Instance& server,
                  const std::string& name);

  // Need to store the factory due to the shared_ptrs that need to be kept alive: date provider,
  // route config manager, scoped route config manager.
  std::function<Http::ApiListenerPtr(Network::ReadFilterCallbacks&)>
      http_connection_manager_factory_;
};

} // namespace Server
} // namespace Envoy
