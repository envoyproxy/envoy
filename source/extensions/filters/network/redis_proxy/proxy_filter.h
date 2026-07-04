#pragma once

#include <cstdint>
#include <list>
#include <memory>
#include <optional>
#include <string>

#include "envoy/extensions/filters/network/redis_proxy/v3/redis_proxy.pb.h"
#include "envoy/network/drain_decision.h"
#include "envoy/network/filter.h"
#include "envoy/stats/scope.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/event/real_time_system.h"
#include "source/extensions/common/dynamic_forward_proxy/dns_cache.h"
#include "source/extensions/filters/network/common/redis/codec.h"
#include "source/extensions/filters/network/redis_proxy/command_splitter.h"
#include "source/extensions/filters/network/redis_proxy/external_auth.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

/**
 * All redis proxy stats. @see stats_macros.h
 */
#define ALL_REDIS_PROXY_STATS(COUNTER, GAUGE)                                                      \
  COUNTER(downstream_cx_drain_close)                                                               \
  COUNTER(downstream_cx_protocol_error)                                                            \
  COUNTER(downstream_cx_rx_bytes_total)                                                            \
  COUNTER(downstream_cx_total)                                                                     \
  COUNTER(downstream_cx_tx_bytes_total)                                                            \
  COUNTER(downstream_rq_noproto)                                                                   \
  COUNTER(downstream_rq_total)                                                                     \
  GAUGE(downstream_cx_active, Accumulate)                                                          \
  GAUGE(downstream_cx_rx_bytes_buffered, Accumulate)                                               \
  GAUGE(downstream_cx_tx_bytes_buffered, Accumulate)                                               \
  GAUGE(downstream_rq_active, Accumulate)

/**
 * Struct definition for all redis proxy stats. @see stats_macros.h
 */
struct ProxyStats {
  ALL_REDIS_PROXY_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

/**
 * Configuration for the redis proxy filter.
 */
class ProxyFilterConfig : public Logger::Loggable<Logger::Id::redis> {
public:
  ProxyFilterConfig(
      const envoy::extensions::filters::network::redis_proxy::v3::RedisProxy& config,
      Stats::Scope& scope, const Network::DrainDecision& drain_decision, Runtime::Loader& runtime,
      Api::Api& api, TimeSource& time_source,
      Extensions::Common::DynamicForwardProxy::DnsCacheManagerFactory& cache_manager_factory);

  const Network::DrainDecision& drain_decision_;
  Runtime::Loader& runtime_;
  const std::string stat_prefix_;
  const std::string redis_drain_close_runtime_key_{"redis.drain_close_enabled"};
  ProxyStats stats_;
  const std::string downstream_auth_username_;
  std::vector<std::string> downstream_auth_passwords_;
  TimeSource& timeSource() const { return time_source_; };
  // Listener-level RESP version (downstream + upstream). Fixed at config load.
  Common::Redis::RespProtocolVersion protocolVersion() const { return protocol_version_; }
  const bool external_auth_enabled_;
  const bool external_auth_expiration_enabled_;

  // DNS cache used for ASK/MOVED responses.
  const Extensions::Common::DynamicForwardProxy::DnsCacheManagerSharedPtr dns_cache_manager_;
  const Extensions::Common::DynamicForwardProxy::DnsCacheSharedPtr dns_cache_{nullptr};

private:
  static ProxyStats generateStats(const std::string& prefix, Stats::Scope& scope);
  Extensions::Common::DynamicForwardProxy::DnsCacheSharedPtr
  getCache(const envoy::extensions::filters::network::redis_proxy::v3::RedisProxy& config);
  TimeSource& time_source_;
  const Common::Redis::RespProtocolVersion protocol_version_;
};

using ProxyFilterConfigSharedPtr = std::shared_ptr<ProxyFilterConfig>;

/**
 * A redis multiplexing proxy filter. This filter will take incoming redis pipelined commands, and
 * multiplex them onto a consistently hashed connection pool of backend servers.
 */
class ProxyFilter : public Network::ReadFilter,
                    public Common::Redis::DecoderCallbacks,
                    public Network::ConnectionCallbacks,
                    public Logger::Loggable<Logger::Id::redis>,
                    public ExternalAuth::AuthenticateCallback {
public:
  ProxyFilter(Common::Redis::DecoderFactory& factory, Common::Redis::EncoderPtr&& encoder,
              CommandSplitter::Instance& splitter, ProxyFilterConfigSharedPtr config,
              ExternalAuth::ExternalAuthClientPtr&& auth_client);
  ~ProxyFilter() override;

  // Network::ReadFilter
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override;
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override { return Network::FilterStatus::Continue; }

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

  // Common::Redis::DecoderCallbacks
  void onRespValue(Common::Redis::RespValuePtr&& value) override;

  // AuthenticateCallback
  void onAuthenticateExternal(CommandSplitter::SplitCallbacks& request,
                              ExternalAuth::AuthenticateResponsePtr&& response) override;

  bool connectionAllowed();

  Common::Redis::Client::Transaction& transaction() { return transaction_; }

private:
  friend class RedisProxyFilterTest;

  enum class ExternalAuthCallStatus { Pending, Ready };

  struct PendingRequest : public CommandSplitter::SplitCallbacks {
    PendingRequest(ProxyFilter& parent);
    ~PendingRequest() override;

    // RedisProxy::CommandSplitter::SplitCallbacks
    bool connectionAllowed() override { return parent_.connectionAllowed(); }
    void onQuit() override { parent_.onQuit(*this); }
    void onAuth(const std::string& password) override { parent_.onAuth(*this, password); }
    void onAuth(const std::string& username, const std::string& password) override {
      parent_.onAuth(*this, username, password);
    }
    void onResponse(Common::Redis::RespValuePtr&& value) override {
      parent_.onResponse(*this, std::move(value));
    }

    Common::Redis::Client::Transaction& transaction() override { return parent_.transaction(); }

    AuthAttempt attemptDownstreamAuthInline(const std::string& username,
                                            const std::string& password,
                                            uint32_t requested_version) override {
      return parent_.attemptDownstreamAuthInline(*this, username, password, requested_version);
    }
    void setDownstreamRespVersion(uint32_t version) override;
    Common::Redis::RespProtocolVersion protocolVersion() const override {
      return parent_.config_->protocolVersion();
    }

    uint32_t currentDownstreamRespVersion() const override {
      return parent_.downstream_resp_version_;
    }

    std::optional<uint32_t> takePendingHelloAuthVersion() override {
      auto version = pending_hello_auth_version_;
      pending_hello_auth_version_.reset();
      return version;
    }

    // Downstream RESP version captured at request creation, set in the constructor from the
    // parent filter's current downstream_resp_version_.
    uint32_t resp_version_at_creation_;
    ProxyFilter& parent_;
    // This value is set when the request is on hold, waiting for an external auth response.
    Common::Redis::RespValuePtr pending_request_value_;
    Common::Redis::RespValuePtr pending_response_;
    CommandSplitter::SplitRequestPtr request_handle_;
    // When this PendingRequest is a HELLO N AUTH ... whose inline-auth check was deferred to
    // the external auth provider, holds N (the requested protocol version). On
    // onAuthenticateExternal, ProxyFilter consults this to emit the deferred HELLO Map (and
    // flip the downstream RESP version) on success or an error reply on failure, instead of
    // the +OK that the AUTH-command path emits.
    std::optional<uint32_t> pending_hello_auth_version_;
  };

  void onQuit(PendingRequest& request);
  void onAuth(PendingRequest& request, const std::string& password);
  void onAuth(PendingRequest& request, const std::string& username, const std::string& password);
  void onResponse(PendingRequest& request, Common::Redis::RespValuePtr&& value);
  bool checkPassword(const std::string& password);
  // Shared local-credential policy for ``AUTH <user> <pass>`` and ``HELLO N AUTH <user> <pass>``
  // (one copy so the two auth entry points cannot silently diverge).
  bool checkCredentials(const std::string& username, const std::string& password);
  // Inline-auth path used by HELLO N AUTH ... handling. Local-credentials case returns
  // Allowed (flipping connection_allowed_) or Denied. External-auth case stashes
  // ``requested_version`` on ``request`` and kicks off ``authenticateExternal``, returning
  // ``ImplOwnsResponse``; ``onAuthenticateExternal`` then emits the deferred HELLO Map / error.
  CommandSplitter::SplitCallbacks::AuthAttempt
  attemptDownstreamAuthInline(PendingRequest& request, const std::string& username,
                              const std::string& password, uint32_t requested_version);
  void processRespValue(Common::Redis::RespValuePtr&& value, PendingRequest& request);
  // Drain any pending_request_value_ entries left in pending_requests_ after an external-auth
  // round trip resolved (called from onAuthenticateExternal). Walks the entire list in FIFO
  // order, not just the front; bails if a resumed entry starts a new round trip, and is
  // guarded against reentrant calls because a resumed AUTH can resolve synchronously (gRPC send()
  // may fail inline) and re-enter this method from within processRespValue.
  void resumeAuthHeldRequests();

  Common::Redis::DecoderPtr decoder_;
  Common::Redis::EncoderPtr encoder_;
  CommandSplitter::Instance& splitter_;
  ProxyFilterConfigSharedPtr config_;
  Buffer::OwnedImpl encoder_buffer_;
  Network::ReadFilterCallbacks* callbacks_{};
  std::list<PendingRequest> pending_requests_;
  bool connection_allowed_;
  // Per-connection negotiated downstream RESP version, held as the wire integer (2 or 3) so
  // it compares directly against the ``HELLO N`` argument. This is distinct from the
  // listener-policy type ``Common::Redis::RespProtocolVersion`` returned by
  // ``ProxyFilterConfig::protocolVersion()``; the two are bridged by ``toWireRespVersion`` /
  // ``toRespProtocolVersion`` at the boundaries. Starts at 2 (a legacy client never sends
  // HELLO) and is flipped by ``setDownstreamRespVersion`` when a ``HELLO N`` whose ``N``
  // matches the listener policy succeeds.
  uint32_t downstream_resp_version_{2};
  Common::Redis::Client::Transaction transaction_;
  bool connection_quit_;
  // True while resumeAuthHeldRequests is draining. A resumed AUTH that resolves synchronously
  // re-enters resumeAuthHeldRequests via onAuthenticateExternal; the nested call must not start
  // a second drain loop over the same list.
  bool resuming_held_requests_{false};
  ExternalAuth::ExternalAuthClientPtr auth_client_;
  ExternalAuthCallStatus external_auth_call_status_;
  long external_auth_expiration_epoch_;
};

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
