#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/integration/filters/common.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

std::string toUsec(MonotonicTime time) { return absl::StrCat(time.time_since_epoch().count()); }

} // namespace

// A filter that sticks stream info into headers for integration testing.
class StreamInfoToHeadersFilter : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "stream-info-to-headers-filter";

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override {
    return Http::FilterHeadersStatus::Continue;
  }

  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers, bool) override {
    const std::string dns_start = "envoy.dynamic_forward_proxy.dns_start_ms";
    const std::string dns_end = "envoy.dynamic_forward_proxy.dns_end_ms";
    StreamInfo::StreamInfo& stream_info = decoder_callbacks_->streamInfo();
    const StreamInfo::StreamInfo& conn_stream_info = decoder_callbacks_->connection()->streamInfo();

    if (stream_info.downstreamTiming().getValue(dns_start).has_value()) {
      headers.addCopy(Http::LowerCaseString("dns_start"),
                      toUsec(stream_info.downstreamTiming().getValue(dns_start).value()));
    }
    if (stream_info.downstreamTiming().getValue(dns_end).has_value()) {
      headers.addCopy(Http::LowerCaseString("dns_end"),
                      toUsec(stream_info.downstreamTiming().getValue(dns_end).value()));
    }
    if (conn_stream_info.downstreamTiming().has_value() &&
        conn_stream_info.downstreamTiming()->downstreamHandshakeComplete().has_value()) {
      headers.addCopy(
          Http::LowerCaseString("downstream_handshake_complete"),
          toUsec(conn_stream_info.downstreamTiming()->downstreamHandshakeComplete().value()));
    }
    if (decoder_callbacks_->streamInfo().upstreamInfo()) {
      if (decoder_callbacks_->streamInfo().upstreamInfo()->upstreamSslConnection()) {
        headers.addCopy(
            Http::LowerCaseString("alpn"),
            decoder_callbacks_->streamInfo().upstreamInfo()->upstreamSslConnection()->alpn());
      }
      if (decoder_callbacks_->streamInfo().upstreamInfo()->upstreamRemoteAddress()) {
        headers.addCopy(
            Http::LowerCaseString("remote_address"),
            decoder_callbacks_->streamInfo().upstreamInfo()->upstreamRemoteAddress()->asString());
      }

      headers.addCopy(Http::LowerCaseString("num_streams"),
                      decoder_callbacks_->streamInfo().upstreamInfo()->upstreamNumStreams());

      const auto maybe_local_interface_name =
          decoder_callbacks_->streamInfo().upstreamInfo()->upstreamInterfaceName();
      if (maybe_local_interface_name.has_value()) {
        headers.addCopy(Http::LowerCaseString("local_interface_name"),
                        maybe_local_interface_name.value());
      }

      StreamInfo::UpstreamTiming& upstream_timing =
          decoder_callbacks_->streamInfo().upstreamInfo()->upstreamTiming();
      if (upstream_timing.upstream_connect_start_.has_value()) {
        headers.addCopy(Http::LowerCaseString("upstream_connect_start"),
                        toUsec(upstream_timing.upstream_connect_start_.value()));
      }
      if (upstream_timing.upstream_connect_complete_.has_value()) {
        headers.addCopy(Http::LowerCaseString("upstream_connect_complete"),
                        toUsec(upstream_timing.upstream_connect_complete_.value()));
      }
      if (upstream_timing.upstream_handshake_complete_.has_value()) {
        headers.addCopy(Http::LowerCaseString("upstream_handshake_complete"),
                        toUsec(upstream_timing.upstream_handshake_complete_.value()));
      }
      if (upstream_timing.last_upstream_tx_byte_sent_.has_value()) {
        headers.addCopy(Http::LowerCaseString("request_send_end"),
                        toUsec(upstream_timing.last_upstream_tx_byte_sent_.value()));
      }
      if (upstream_timing.first_upstream_rx_byte_received_.has_value()) {
        headers.addCopy(Http::LowerCaseString("response_begin"),
                        toUsec(upstream_timing.first_upstream_rx_byte_received_.value()));
      }
    }
    return Http::FilterHeadersStatus::Continue;
  }
};

constexpr char StreamInfoToHeadersFilter::name[];
static Registry::RegisterFactory<SimpleFilterConfig<StreamInfoToHeadersFilter>,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Envoy
