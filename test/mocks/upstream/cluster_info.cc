#include "test/mocks/upstream/cluster_info.h"

#include <limits>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/upstream/host_description.h"
#include "envoy/upstream/upstream.h"

#include "source/common/config/metadata.h"
#include "source/common/http/utility.h"
#include "source/common/network/raw_buffer_socket.h"
#include "source/common/upstream/upstream_impl.h"

using testing::_;
using testing::Invoke;
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;

namespace Envoy {
namespace Upstream {

MockLoadBalancerSubsetInfo::MockLoadBalancerSubsetInfo() {
  ON_CALL(*this, isEnabled()).WillByDefault(Return(false));
  ON_CALL(*this, fallbackPolicy())
      .WillByDefault(Return(envoy::config::cluster::v3::Cluster::LbSubsetConfig::ANY_ENDPOINT));
  ON_CALL(*this, defaultSubset()).WillByDefault(ReturnRef(ProtobufWkt::Struct::default_instance()));
  ON_CALL(*this, subsetSelectors()).WillByDefault(ReturnRef(subset_selectors_));
}

MockLoadBalancerSubsetInfo::~MockLoadBalancerSubsetInfo() = default;

MockIdleTimeEnabledClusterInfo::MockIdleTimeEnabledClusterInfo() {
  ON_CALL(*this, idleTimeout()).WillByDefault(Return(std::chrono::milliseconds(1000)));
}

MockIdleTimeEnabledClusterInfo::~MockIdleTimeEnabledClusterInfo() = default;

MockClusterInfo::MockClusterInfo()
    : http2_options_(::Envoy::Http2::Utility::initializeAndValidateOptions(
          envoy::config::core::v3::Http2ProtocolOptions())),
      stat_names_(stats_store_.symbolTable()),
      cluster_load_report_stat_names_(stats_store_.symbolTable()),
      cluster_circuit_breakers_stat_names_(stats_store_.symbolTable()),
      cluster_request_response_size_stat_names_(stats_store_.symbolTable()),
      cluster_timeout_budget_stat_names_(stats_store_.symbolTable()),
      stats_(ClusterInfoImpl::generateStats(stats_store_, stat_names_)),
      transport_socket_matcher_(new NiceMock<Upstream::MockTransportSocketMatcher>()),
      load_report_stats_(ClusterInfoImpl::generateLoadReportStats(load_report_stats_store_,
                                                                  cluster_load_report_stat_names_)),
      request_response_size_stats_(std::make_unique<ClusterRequestResponseSizeStats>(
          ClusterInfoImpl::generateRequestResponseSizeStats(
              request_response_size_stats_store_, cluster_request_response_size_stat_names_))),
      timeout_budget_stats_(
          std::make_unique<ClusterTimeoutBudgetStats>(ClusterInfoImpl::generateTimeoutBudgetStats(
              timeout_budget_stats_store_, cluster_timeout_budget_stat_names_))),
      circuit_breakers_stats_(ClusterInfoImpl::generateCircuitBreakersStats(
          stats_store_, cluster_circuit_breakers_stat_names_.default_, true,
          cluster_circuit_breakers_stat_names_)),
      resource_manager_(new Upstream::ResourceManagerImpl(
          runtime_, "fake_key", 1, 1024, 1024, 1, std::numeric_limits<uint64_t>::max(),
          circuit_breakers_stats_, absl::nullopt, absl::nullopt)),
      stats_scope_(stats_store_.createScope("test_scope")) {
  ON_CALL(*this, connectTimeout()).WillByDefault(Return(std::chrono::milliseconds(1)));
  ON_CALL(*this, idleTimeout()).WillByDefault(Return(absl::optional<std::chrono::milliseconds>()));
  ON_CALL(*this, perUpstreamPreconnectRatio()).WillByDefault(Return(1.0));
  ON_CALL(*this, name()).WillByDefault(ReturnRef(name_));
  ON_CALL(*this, observabilityName()).WillByDefault(ReturnRef(observability_name_));
  ON_CALL(*this, edsServiceName()).WillByDefault(ReturnPointee(&eds_service_name_));
  ON_CALL(*this, http1Settings()).WillByDefault(ReturnRef(http1_settings_));
  ON_CALL(*this, http2Options()).WillByDefault(ReturnRef(http2_options_));
  ON_CALL(*this, http3Options()).WillByDefault(ReturnRef(http3_options_));
  ON_CALL(*this, commonHttpProtocolOptions())
      .WillByDefault(ReturnRef(common_http_protocol_options_));
  ON_CALL(*this, extensionProtocolOptions(_)).WillByDefault(Return(extension_protocol_options_));
  ON_CALL(*this, maxResponseHeadersCount())
      .WillByDefault(ReturnPointee(&max_response_headers_count_));
  ON_CALL(*this, maxRequestsPerConnection())
      .WillByDefault(ReturnPointee(&max_requests_per_connection_));
  ON_CALL(*this, stats()).WillByDefault(ReturnRef(stats_));
  ON_CALL(*this, statsScope()).WillByDefault(ReturnRef(stats_store_));
  // TODO(incfly): The following is a hack because it's not possible to directly embed
  // a mock transport socket factory matcher due to circular dependencies. Fix this up in a follow
  // up.
  ON_CALL(*this, transportSocketMatcher())
      .WillByDefault(
          Invoke([this]() -> TransportSocketMatcher& { return *transport_socket_matcher_; }));
  ON_CALL(*this, loadReportStats()).WillByDefault(ReturnRef(load_report_stats_));
  ON_CALL(*this, requestResponseSizeStats())
      .WillByDefault(Return(
          std::reference_wrapper<ClusterRequestResponseSizeStats>(*request_response_size_stats_)));
  ON_CALL(*this, timeoutBudgetStats())
      .WillByDefault(
          Return(std::reference_wrapper<ClusterTimeoutBudgetStats>(*timeout_budget_stats_)));
  ON_CALL(*this, sourceAddress()).WillByDefault(ReturnRef(source_address_));
  ON_CALL(*this, resourceManager(_))
      .WillByDefault(Invoke(
          [this](ResourcePriority) -> Upstream::ResourceManager& { return *resource_manager_; }));
  ON_CALL(*this, lbType()).WillByDefault(ReturnPointee(&lb_type_));
  ON_CALL(*this, sourceAddress()).WillByDefault(ReturnRef(source_address_));
  ON_CALL(*this, lbSubsetInfo()).WillByDefault(ReturnRef(lb_subset_));
  ON_CALL(*this, lbRingHashConfig()).WillByDefault(ReturnRef(lb_ring_hash_config_));
  ON_CALL(*this, lbMaglevConfig()).WillByDefault(ReturnRef(lb_maglev_config_));
  ON_CALL(*this, lbOriginalDstConfig()).WillByDefault(ReturnRef(lb_original_dst_config_));
  ON_CALL(*this, upstreamConfig()).WillByDefault(ReturnRef(upstream_config_));
  ON_CALL(*this, lbConfig()).WillByDefault(ReturnRef(lb_config_));
  ON_CALL(*this, clusterSocketOptions()).WillByDefault(ReturnRef(cluster_socket_options_));
  ON_CALL(*this, metadata()).WillByDefault(ReturnRef(metadata_));
  ON_CALL(*this, upstreamHttpProtocolOptions())
      .WillByDefault(ReturnRef(upstream_http_protocol_options_));
  ON_CALL(*this, alternateProtocolsCacheOptions())
      .WillByDefault(ReturnRef(alternate_protocols_cache_options_));
  // Delayed construction of typed_metadata_, to allow for injection of metadata
  ON_CALL(*this, typedMetadata())
      .WillByDefault(Invoke([this]() -> const Envoy::Config::TypedMetadata& {
        if (typed_metadata_ == nullptr) {
          typed_metadata_ =
              std::make_unique<Config::TypedMetadataImpl<ClusterTypedMetadataFactory>>(metadata_);
        }
        return *typed_metadata_;
      }));
  ON_CALL(*this, clusterType()).WillByDefault(ReturnRef(cluster_type_));
  ON_CALL(*this, upstreamHttpProtocol(_))
      .WillByDefault(Return(std::vector<Http::Protocol>{Http::Protocol::Http11}));
}

MockClusterInfo::~MockClusterInfo() = default;

Http::Http1::CodecStats& MockClusterInfo::http1CodecStats() const {
  return Http::Http1::CodecStats::atomicGet(http1_codec_stats_, *stats_scope_);
}

Http::Http2::CodecStats& MockClusterInfo::http2CodecStats() const {
  return Http::Http2::CodecStats::atomicGet(http2_codec_stats_, *stats_scope_);
}

Http::Http3::CodecStats& MockClusterInfo::http3CodecStats() const {
  return Http::Http3::CodecStats::atomicGet(http3_codec_stats_, *stats_scope_);
}

} // namespace Upstream
} // namespace Envoy
