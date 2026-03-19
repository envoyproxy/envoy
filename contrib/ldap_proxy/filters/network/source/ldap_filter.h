// LDAP proxy filter - handles StartTLS upgrades transparently.
//
// Two modes:
//   ON_DEMAND: client initiates StartTLS -> we negotiate with upstream, upgrade both
//   ALWAYS: we proactively send StartTLS to upstream on first data, downstream stays plain

#pragma once

#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/event/timer.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"

#include "contrib/ldap_proxy/filters/network/source/ldap_decoder.h"
#include "contrib/ldap_proxy/filters/network/source/protocol_helpers.h"
#include "contrib/ldap_proxy/filters/network/source/protocol_templates.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace LdapProxy {

#define ALL_LDAP_PROXY_STATS(COUNTER) \
  COUNTER(starttls_success)           \
  COUNTER(decoder_error)              \
  COUNTER(protocol_violation)

struct LdapProxyStats {
  ALL_LDAP_PROXY_STATS(GENERATE_COUNTER_STRUCT)
};

enum class FilterState {
  Inspecting,
  NegotiatingUpstream,
  NegotiatingDownstream,
  Passthrough,
  Closed,
};

class LdapFilter : public Network::Filter,
                   public Network::ConnectionCallbacks,
                   public DecoderCallbacks,
                   public Logger::Loggable<Logger::Id::filter> {
public:
  LdapFilter(Stats::Scope& scope, bool force_upstream_starttls = false);
  ~LdapFilter() override = default;

  // ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override;

  // WriteFilter
  Network::FilterStatus onWrite(Buffer::Instance& data, bool end_stream) override;
  void initializeWriteFilterCallbacks(Network::WriteFilterCallbacks& callbacks) override;

  // DecoderCallbacks
  void onStartTlsRequest(uint32_t msg_id, size_t msg_length) override;
  void onPlaintextOperation(uint32_t msg_id) override;
  void onDecoderError() override;
  void onPipelineViolation() override;

  // ConnectionCallbacks
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

  FilterState state() const { return state_; }

private:
  bool isDownstreamTls() const;
  void closeConnection();
  void sendStartTlsSuccessResponse();
  void sendUpstreamStartTlsRequest();
  bool handleUpstreamStartTlsResponse(Buffer::Instance& data);
  void completeTlsSwitch();
  void transitionToEncrypted();
  void initiateUpstreamStartTls();
  void completeUpstreamOnlyTlsSwitch();

  LdapProxyStats generateStats(Stats::Scope& scope) {
    return LdapProxyStats{ALL_LDAP_PROXY_STATS(POOL_COUNTER_PREFIX(scope, ""))};
  }

  Network::ReadFilterCallbacks* read_callbacks_{};
  Network::WriteFilterCallbacks* write_callbacks_{};
  LdapProxyStats stats_;
  DecoderPtr decoder_;
  FilterState state_{FilterState::Inspecting};

  bool upstream_starttls_{false};

  Buffer::OwnedImpl inspect_buffer_;
  Buffer::OwnedImpl upstream_buffer_;
  Buffer::OwnedImpl pending_downstream_data_;

  uint32_t pending_starttls_msg_id_{0};
  uint32_t upstream_starttls_msg_id_{0};

  bool pending_downstream_tls_switch_{false};
  uint64_t pending_response_bytes_{0};
  Event::TimerPtr downstream_flush_timer_;
  Event::TimerPtr upstream_starttls_timer_;
};

} // namespace LdapProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
