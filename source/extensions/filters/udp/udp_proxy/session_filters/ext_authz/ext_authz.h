#pragma once

#include <chrono>
#include <memory>
#include <queue>

#include "envoy/extensions/filters/udp/udp_proxy/session/ext_authz/v3/ext_authz.pb.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/network/filter.h"
#include "envoy/server/factory_context.h"
#include "envoy/service/auth/v3/external_auth.pb.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/common/ext_authz/ext_authz.h"
#include "source/extensions/filters/common/ext_authz/ext_authz_grpc_impl.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace SessionFilters {
namespace ExtAuthz {

using FilterConfig =
    envoy::extensions::filters::udp::udp_proxy::session::ext_authz::v3::FilterConfig;
using FilterFactoryCb = Network::UdpSessionFilterFactoryCb;
using ReadFilter = Network::UdpSessionReadFilter;
using ReadFilterStatus = Network::UdpSessionReadFilterStatus;
using ReadFilterCallbacks = Network::UdpSessionReadFilterCallbacks;
using BufferedDatagramPtr = std::unique_ptr<Network::UdpRecvData>;

inline constexpr absl::string_view FilterName = "envoy.filters.udp.session.ext_authz";

/**
 * All UDP session external authorization stats. @see stats_macros.h
 */
#define ALL_UDP_SESSION_EXT_AUTHZ_STATS(COUNTER, GAUGE)                                            \
  COUNTER(ok)                                                                                      \
  COUNTER(denied)                                                                                  \
  COUNTER(error)                                                                                   \
  COUNTER(failure_mode_allowed)                                                                    \
  COUNTER(total)                                                                                   \
  COUNTER(buffer_overflow)                                                                         \
  GAUGE(active, Accumulate)

/**
 * Struct definition for all UDP session external authorization stats. @see stats_macros.h
 */
struct ExtAuthzStats {
  ALL_UDP_SESSION_EXT_AUTHZ_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

/**
 * Filter configuration, shared across all sessions and workers. Holds the gRPC client factory used
 * to create a per-session ext_authz client.
 */
class Config {
public:
  Config(const FilterConfig& config, Stats::Scope& scope,
         Server::Configuration::ServerFactoryContext& context);

  const ExtAuthzStats& stats() const { return stats_; }
  bool failureModeAllow() const { return failure_mode_allow_; }
  bool bufferEnabled() const { return buffer_enabled_; }
  uint32_t maxBufferedDatagrams() const { return max_buffered_datagrams_; }
  uint64_t maxBufferedBytes() const { return max_buffered_bytes_; }

  // Creates a per-session gRPC ext_authz client (cheap for unary RPC, like network ext_authz).
  Filters::Common::ExtAuthz::ClientPtr createClient() const;

private:
  static ExtAuthzStats generateStats(Stats::Scope& scope) {
    return {ALL_UDP_SESSION_EXT_AUTHZ_STATS(POOL_COUNTER(scope), POOL_GAUGE(scope))};
  }
  static Grpc::AsyncClientFactoryPtr
  createAsyncClientFactory(const FilterConfig& config, Stats::Scope& scope,
                           Server::Configuration::ServerFactoryContext& context);

  const Stats::ScopeSharedPtr stats_scope_;
  const ExtAuthzStats stats_;
  const bool failure_mode_allow_;
  const std::chrono::milliseconds timeout_;
  const bool buffer_enabled_;
  const uint32_t max_buffered_datagrams_;
  const uint64_t max_buffered_bytes_;
  const Grpc::AsyncClientFactoryPtr async_client_factory_;
};

using ConfigSharedPtr = std::shared_ptr<Config>;

/**
 * Per-session read filter that authorizes a new UDP session against an external gRPC ext_authz
 * service before any datagrams reach the upstream; denied sessions are dropped.
 */
class Filter : public ReadFilter,
               public Filters::Common::ExtAuthz::RequestCallbacks,
               Logger::Loggable<Logger::Id::filter> {
public:
  Filter(ConfigSharedPtr config, Filters::Common::ExtAuthz::ClientPtr&& client)
      : config_(std::move(config)), client_(std::move(client)),
        session_buffer_enabled_(config_->bufferEnabled()) {}
  ~Filter() override;

  // Network::UdpSessionReadFilter
  ReadFilterStatus onNewSession() override;
  ReadFilterStatus onData(Network::UdpRecvData& data) override;
  void initializeReadFilterCallbacks(ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }

  // Filters::Common::ExtAuthz::RequestCallbacks
  void onComplete(Filters::Common::ExtAuthz::ResponsePtr&& response) override;

private:
  enum class Status { NotStarted, Calling, Complete };

  void maybeBufferDatagram(Network::UdpRecvData& data);
  void clearBuffer();
  bool sessionBufferEnabled() const { return session_buffer_enabled_; }
  void disableSessionBuffer() { session_buffer_enabled_ = false; }

  const ConfigSharedPtr config_;
  Filters::Common::ExtAuthz::ClientPtr client_;
  ReadFilterCallbacks* read_callbacks_{};
  Status status_{Status::NotStarted};
  // True while check() is on the stack, to detect synchronous completion.
  bool calling_check_{false};
  bool completed_{false};
  bool allowed_{false};
  bool session_buffer_enabled_;
  envoy::service::auth::v3::CheckRequest check_request_;
  uint64_t buffered_bytes_{0};
  std::queue<BufferedDatagramPtr> datagrams_buffer_;
};

} // namespace ExtAuthz
} // namespace SessionFilters
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
