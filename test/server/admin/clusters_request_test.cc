#include <cstdint>
#include <functional>
#include <memory>
#include <regex>

#include "envoy/buffer/buffer.h"
#include "envoy/http/codes.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/upstream/resource_manager_impl.h"
#include "source/server/admin/clusters_params.h"
#include "source/server/admin/clusters_request.h"

#include "test/mocks/server/instance.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/cluster_priority_set.h"
#include "test/mocks/upstream/host.h"
#include "test/mocks/upstream/priority_set.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Server {
namespace {

using testing::Const;
using testing::NiceMock;
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;

class BaseClustersRequestFixture : public testing::Test {
protected:
  BaseClustersRequestFixture() {
    ON_CALL(mock_server_, clusterManager()).WillByDefault(ReturnRef(mock_cluster_manager_));
    ON_CALL(mock_cluster_manager_, clusters()).WillByDefault(ReturnPointee(&cluster_info_maps_));
    resource_manager_default_ = std::make_unique<Upstream::ResourceManagerImpl>(
        runtime_, resource_manager_key_, 1024, 1024, 1024, 16, 4, 512,
        mock_cluster_info_.circuit_breakers_stats_, std::nullopt, std::nullopt);
    resource_manager_high_ = std::make_unique<Upstream::ResourceManagerImpl>(
        runtime_, resource_manager_key_, 4096, 4096, 4096, 16, 4, 1024,
        mock_cluster_info_.circuit_breakers_stats_, std::nullopt, std::nullopt);

    locality_.set_region("test_region");
    locality_.set_zone("test_zone");
    locality_.set_sub_zone("test_sub_zone");

    counter_.add(10);
    counters_.emplace_back("test_counter", counter_);
    gauge_.add(11);
    gauges_.emplace_back("test_gauge", gauge_);
  }

  using ClustersRequestPtr = std::unique_ptr<ClustersRequest>;

  ClustersRequestPtr makeRequest(uint64_t chunk_limit, ClustersParams& params) {
    switch (params.format_) {
    case ClustersParams::Format::Text:
      return std::make_unique<TextClustersRequest>(chunk_limit, mock_server_, params);
    case ClustersParams::Format::Json:
      return std::make_unique<JsonClustersRequest>(chunk_limit, mock_server_, params);
    }
    return nullptr;
  }

  struct ResponseResult {
    Http::Code code_;
    Buffer::OwnedImpl data_;
  };

  ResponseResult response(ClustersRequest& request, bool drain_after_next_chunk) {
    Http::TestResponseHeaderMapImpl response_headers;
    Http::Code code = request.start(response_headers);
    Buffer::OwnedImpl buffer;
    Buffer::OwnedImpl result_data;
    while (request.nextChunk(buffer)) {
      if (drain_after_next_chunk) {
        result_data.move(buffer);
      }
    }
    if (drain_after_next_chunk) {
      result_data.move(buffer);
    }
    return {
        /* code=*/code,
        /* data=*/drain_after_next_chunk ? std::move(result_data) : std::move(buffer),
    };
  }

  void loadNewMockClusterByName(NiceMock<Upstream::MockClusterMockPrioritySet>& mock_cluster,
                                absl::string_view name) {
    mock_cluster.info_->name_ = name;
    ON_CALL(*mock_cluster.info_, edsServiceName()).WillByDefault(ReturnRef(eds_service_name_));
    ON_CALL(*mock_cluster.info_, resourceManager(Upstream::ResourcePriority::Default))
        .WillByDefault(ReturnRef(std::ref(*resource_manager_default_).get()));
    ON_CALL(*mock_cluster.info_, resourceManager(Upstream::ResourcePriority::High))
        .WillByDefault(ReturnRef(std::ref(*resource_manager_high_).get()));
    ON_CALL(Const(mock_cluster), outlierDetector()).WillByDefault(Return(&detector_));
    ON_CALL(Const(detector_),
            successRateEjectionThreshold(
                Upstream::Outlier::DetectorHostMonitor::SuccessRateMonitorType::ExternalOrigin))
        .WillByDefault(Return(double(1.1)));
    ON_CALL(Const(detector_),
            successRateEjectionThreshold(
                Upstream::Outlier::DetectorHostMonitor::SuccessRateMonitorType::LocalOrigin))
        .WillByDefault(Return(double(1.1)));
    ON_CALL(*mock_cluster.info_, addedViaApi()).WillByDefault(Return(true));
    cluster_info_maps_.active_clusters_.emplace(name, std::ref(mock_cluster));

    Network::Address::InstanceConstSharedPtr address =
        Network::Utility::resolveUrl("tcp://1.2.3.4:80");
    ON_CALL(*mock_host_, address()).WillByDefault(Return(address));
    Upstream::MockHostSet* host_set = mock_cluster.priority_set_.getMockHostSet(0);

    ON_CALL(*mock_host_, counters()).WillByDefault(Return(counters_));
    ON_CALL(*mock_host_, gauges()).WillByDefault(Return(gauges_));

    ON_CALL(*mock_host_, hostname()).WillByDefault(ReturnRef(hostname_));
    ON_CALL(*mock_host_, locality()).WillByDefault(ReturnRef(locality_));
    ON_CALL(*mock_host_, healthFlagGet(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC))
        .WillByDefault(Return(true));
    ON_CALL(*mock_host_, healthFlagGet(Upstream::Host::HealthFlag::FAILED_OUTLIER_CHECK))
        .WillByDefault(Return(true));
    ON_CALL(*mock_host_, healthFlagGet(Upstream::Host::HealthFlag::EDS_STATUS_DRAINING))
        .WillByDefault(Return(true));
    ON_CALL(*mock_host_, healthFlagGet(Upstream::Host::HealthFlag::DEGRADED_ACTIVE_HC))
        .WillByDefault(Return(true));
    ON_CALL(*mock_host_, healthFlagGet(Upstream::Host::HealthFlag::PENDING_DYNAMIC_REMOVAL))
        .WillByDefault(Return(true));
    ON_CALL(*mock_host_, healthFlagGet(Upstream::Host::HealthFlag::PENDING_ACTIVE_HC))
        .WillByDefault(Return(true));
    ON_CALL(*mock_host_, healthFlagGet(Upstream::Host::HealthFlag::EXCLUDED_VIA_IMMEDIATE_HC_FAIL))
        .WillByDefault(Return(true));
    ON_CALL(*mock_host_, healthFlagGet(Upstream::Host::HealthFlag::ACTIVE_HC_TIMEOUT))
        .WillByDefault(Return(true));
    ON_CALL(*mock_host_, outlierDetector()).WillByDefault(ReturnRef(detector_host_monitor_));
    ON_CALL(
        Const(detector_host_monitor_),
        successRate(Upstream::Outlier::DetectorHostMonitor::SuccessRateMonitorType::ExternalOrigin))
        .WillByDefault(Return(double(1.0)));
    ON_CALL(*mock_host_, weight()).WillByDefault(Return(uint64_t(1)));
    ON_CALL(*mock_host_, priority()).WillByDefault(Return(uint64_t(1)));
    ON_CALL(
        Const(detector_host_monitor_),
        successRate(Upstream::Outlier::DetectorHostMonitor::SuccessRateMonitorType::LocalOrigin))
        .WillByDefault(Return(double(1.0)));

    host_set->hosts_.emplace_back(mock_host_);
  }

  const std::string hostname_{"test_hostname"};
  const std::string eds_service_name_{"potato_launcher"};
  NiceMock<Upstream::MockClusterInfo> mock_cluster_info_;
  NiceMock<MockInstance> mock_server_;
  NiceMock<Upstream::MockClusterManager> mock_cluster_manager_;
  Upstream::ClusterManager::ClusterInfoMaps cluster_info_maps_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Upstream::Outlier::MockDetector> detector_;
  const std::string resource_manager_key_{"test_resource_manager_key"};
  std::unique_ptr<Upstream::ResourceManager> resource_manager_default_;
  std::unique_ptr<Upstream::ResourceManager> resource_manager_high_;
  NiceMock<Upstream::MockPrioritySet> mock_priority_set_;
  std::unique_ptr<Upstream::MockHostSet> mock_host_set_{new NiceMock<Upstream::MockHostSet>()};
  std::vector<std::unique_ptr<Upstream::MockHostSet>> mock_host_sets_;
  std::shared_ptr<Upstream::MockHost> mock_host_{new NiceMock<Upstream::MockHost>()};
  std::vector<std::shared_ptr<Upstream::Host>> mock_hosts_;
  envoy::config::core::v3::Locality locality_;
  NiceMock<Upstream::Outlier::MockDetectorHostMonitor> detector_host_monitor_;
  Stats::PrimitiveCounter counter_;
  Stats::PrimitiveGauge gauge_;
  std::vector<std::pair<absl::string_view, Stats::PrimitiveCounterReference>> counters_;
  std::vector<std::pair<absl::string_view, Stats::PrimitiveGaugeReference>> gauges_;
};

struct VerifyJsonOutputParameters {
  bool drain_;
};

class VerifyJsonOutputFixture : public BaseClustersRequestFixture,
                                public testing::WithParamInterface<VerifyJsonOutputParameters> {};

TEST_P(VerifyJsonOutputFixture, VerifyJsonOutput) {
  // Small chunk limit will force Request::nextChunk to be called for each Cluster.
  constexpr int chunk_limit = 1;
  VerifyJsonOutputParameters params = GetParam();
  Buffer::OwnedImpl buffer;
  ClustersParams clusters_params;
  clusters_params.format_ = ClustersParams::Format::Json;

  NiceMock<Upstream::MockClusterMockPrioritySet> test_cluster;
  loadNewMockClusterByName(test_cluster, "test_cluster");

  NiceMock<Upstream::MockClusterMockPrioritySet> test_cluster2;
  loadNewMockClusterByName(test_cluster2, "test_cluster2");

  ResponseResult result = response(*makeRequest(chunk_limit, clusters_params), params.drain_);

  EXPECT_EQ(result.code_, Http::Code::OK);
  // The order of clusters is non-deterministic so strip the 2 from test_cluster2 and expect both
  // clusters to be identical. We also strip all whitespace when making the expectation since the
  // output will not actually have any.
  std::string expected_readable_output = R"EOF(
{
  "cluster_statuses": [
    {
      "circuit_breakers": {
        "thresholds": [
          {
            "priority": "DEFAULT",
            "max_connections": 1024,
            "max_pending_requests": 1024,
            "max_requests": 1024,
            "max_retries": 16
          },
          {
            "priority": "HIGH",
            "max_connections": 4096,
            "max_pending_requests": 4096,
            "max_requests": 4096,
            "max_retries": 16
          }
        ]
      },
      "success_rate_ejection_threshold": {
        "value": 1.1
      },
      "local_origin_success_rate_ejection_threshold": {
        "value": 1.1
      },
      "name": "test_cluster",
      "observability_name": "observability_name",
      "eds_service_name": "potato_launcher",
      "added_via_api": true,
      "host_statuses": [
        {
          "address": {
            "socket_address": {
              "address": "1.2.3.4",
              "port_value": 80
            }
          },
          "locality": {
            "region": "test_region",
            "zone": "test_zone",
            "sub_zone": "test_sub_zone"
          },
          "stats": [
            {
              "type": "COUNTER",
              "name": "test_counter",
              "value": 10
            },
            {
              "type": "GAUGE",
              "name": "test_gauge",
              "value": 11
            }
          ],
          "health_status": {
            "active_hc_timeout": true,
            "eds_health_status": "DRAINING",
            "excluded_via_immediate_hc_fail": true,
            "failed_active_degraded_check": true,
            "failed_active_health_check": true,
            "failed_outlier_check": true,
            "pending_active_hc": true,
            "pending_dynamic_removal": true
          },
          "success_rate": {
            "value": 1
          },
          "local_origin_success_rate": {
            "value": 1
          },
          "hostname": "test_hostname",
          "weight": 1,
          "priority": 1
        }
      ]
    },
    {
      "circuit_breakers": {
        "thresholds": [
          {
            "priority": "DEFAULT",
            "max_connections": 1024,
            "max_pending_requests": 1024,
            "max_requests": 1024,
            "max_retries": 16
          },
          {
            "priority": "HIGH",
            "max_connections": 4096,
            "max_pending_requests": 4096,
            "max_requests": 4096,
            "max_retries": 16
          }
        ]
      },
      "success_rate_ejection_threshold": {
        "value": 1.1
      },
      "local_origin_success_rate_ejection_threshold": {
        "value": 1.1
      },
      "name": "test_cluster",
      "observability_name": "observability_name",
      "eds_service_name": "potato_launcher",
      "added_via_api": true,
      "host_statuses": [
        {
          "address": {
            "socket_address": {
              "address": "1.2.3.4",
              "port_value": 80
            }
          },
          "locality": {
            "region": "test_region",
            "zone": "test_zone",
            "sub_zone": "test_sub_zone"
          },
          "stats": [
            {
              "type": "COUNTER",
              "name": "test_counter",
              "value": 10
            },
            {
              "type": "GAUGE",
              "name": "test_gauge",
              "value": 11
            }
          ],
          "health_status": {
            "active_hc_timeout": true,
            "eds_health_status": "DRAINING",
            "excluded_via_immediate_hc_fail": true,
            "failed_active_degraded_check": true,
            "failed_active_health_check": true,
            "failed_outlier_check": true,
            "pending_active_hc": true,
            "pending_dynamic_removal": true
          },
          "success_rate": {
            "value": 1
          },
          "local_origin_success_rate": {
            "value": 1
          },
          "hostname": "test_hostname",
          "weight": 1,
          "priority": 1
        }
      ]
    }
  ]
}
  )EOF";
  EXPECT_EQ(
      std::regex_replace(result.data_.toString(), std::regex("test_cluster2"), "test_cluster"),
      std::regex_replace(expected_readable_output, std::regex(R"(\s)"), ""));
}

constexpr VerifyJsonOutputParameters VERIFY_JSON_CASES[] = {
    {/* drain_=*/false},
    {/* drain_=*/true},
};

INSTANTIATE_TEST_SUITE_P(VerifyJsonOutput, VerifyJsonOutputFixture,
                         testing::ValuesIn<VerifyJsonOutputParameters>(VERIFY_JSON_CASES));

struct VerifyTextOutputParameters {
  bool drain_;
};

class VerifyTextOutputFixture : public BaseClustersRequestFixture,
                                public testing::WithParamInterface<VerifyTextOutputParameters> {};

// TODO(demitriswan) Implement test for text output verification.
TEST_P(VerifyTextOutputFixture, VerifyTextOutput) {
  // Small chunk limit will force Request::nextChunk to be called for each Cluster.
  constexpr int chunk_limit = 1;
  VerifyTextOutputParameters params = GetParam();
  Buffer::OwnedImpl buffer;
  ClustersParams clusters_params;
  clusters_params.format_ = ClustersParams::Format::Text;

  NiceMock<Upstream::MockClusterMockPrioritySet> test_cluster;
  loadNewMockClusterByName(test_cluster, "test_cluster");

  NiceMock<Upstream::MockClusterMockPrioritySet> test_cluster2;
  loadNewMockClusterByName(test_cluster2, "test_cluster2");

  ResponseResult result = response(*makeRequest(chunk_limit, clusters_params), params.drain_);

  EXPECT_EQ(result.code_, Http::Code::OK);
  // The order of clusters is non-deterministic so strip the 2 from test_cluster2 and expect both
  // clusters to be identical. We also strip all whitespace when making the expectation since the
  // output will not actually have any.
  std::string expected_readable_output =
      R"EOF(test_cluster::observability_name::observability_name
test_cluster::outlier::success_rate_average::0
test_cluster::outlier::success_rate_ejection_threshold::1.1
test_cluster::outlier::local_origin_success_rate_average::0
test_cluster::outlier::local_origin_success_rate_ejection_threshold::1.1
test_cluster::default_priority::max_connections::1024
test_cluster::default_priority::max_pending_requests::1024
test_cluster::default_priority::max_requests::1024
test_cluster::default_priority::max_retries::16
test_cluster::high_priority::max_connections::4096
test_cluster::high_priority::max_pending_requests::4096
test_cluster::high_priority::max_requests::4096
test_cluster::high_priority::max_retries::16
test_cluster::added_via_api::true
test_cluster::eds_service_name::potato_launcher
test_cluster::1.2.3.4:80::test_counter::10
test_cluster::1.2.3.4:80::test_gauge::11
test_cluster::1.2.3.4:80::hostname::test_hostname
test_cluster::1.2.3.4:80::health_flags::/failed_active_hc/failed_outlier_check/degraded_active_hc/pending_dynamic_removal/pending_active_hc/excluded_via_immediate_hc_fail/active_hc_timeout/eds_status_draining
test_cluster::1.2.3.4:80::weight::1
test_cluster::1.2.3.4:80::region::test_region
test_cluster::1.2.3.4:80::zone::test_zone
test_cluster::1.2.3.4:80::sub_zone::test_sub_zone
test_cluster::1.2.3.4:80::canary::false
test_cluster::1.2.3.4:80::priority::1
test_cluster::1.2.3.4:80::success_rate::1
test_cluster::1.2.3.4:80::local_origin_success_rate::1
test_cluster::observability_name::observability_name
test_cluster::outlier::success_rate_average::0
test_cluster::outlier::success_rate_ejection_threshold::1.1
test_cluster::outlier::local_origin_success_rate_average::0
test_cluster::outlier::local_origin_success_rate_ejection_threshold::1.1
test_cluster::default_priority::max_connections::1024
test_cluster::default_priority::max_pending_requests::1024
test_cluster::default_priority::max_requests::1024
test_cluster::default_priority::max_retries::16
test_cluster::high_priority::max_connections::4096
test_cluster::high_priority::max_pending_requests::4096
test_cluster::high_priority::max_requests::4096
test_cluster::high_priority::max_retries::16
test_cluster::added_via_api::true
test_cluster::eds_service_name::potato_launcher
test_cluster::1.2.3.4:80::test_counter::10
test_cluster::1.2.3.4:80::test_gauge::11
test_cluster::1.2.3.4:80::hostname::test_hostname
test_cluster::1.2.3.4:80::health_flags::/failed_active_hc/failed_outlier_check/degraded_active_hc/pending_dynamic_removal/pending_active_hc/excluded_via_immediate_hc_fail/active_hc_timeout/eds_status_draining
test_cluster::1.2.3.4:80::weight::1
test_cluster::1.2.3.4:80::region::test_region
test_cluster::1.2.3.4:80::zone::test_zone
test_cluster::1.2.3.4:80::sub_zone::test_sub_zone
test_cluster::1.2.3.4:80::canary::false
test_cluster::1.2.3.4:80::priority::1
test_cluster::1.2.3.4:80::success_rate::1
test_cluster::1.2.3.4:80::local_origin_success_rate::1
)EOF";
  EXPECT_EQ(
      std::regex_replace(result.data_.toString(), std::regex("test_cluster2"), "test_cluster"),
      expected_readable_output);
}

constexpr VerifyTextOutputParameters VERIFY_TEXT_CASES[] = {
    {/* drain_=*/true},
    {/* drain_=*/false},
};

INSTANTIATE_TEST_SUITE_P(VerifyTextOutput, VerifyTextOutputFixture,
                         testing::ValuesIn<VerifyTextOutputParameters>(VERIFY_TEXT_CASES));

TEST(Json, VerifyArrayPtrDestructionTerminatesJsonArray) {
  class Foo {
  public:
    Foo(std::unique_ptr<Json::Streamer> streamer, Buffer::Instance& buffer)
        : streamer_(std::move(streamer)), buffer_(buffer) {
      array_ = streamer_->makeRootArray();
    }
    void foo(Buffer::Instance& buffer, int64_t number) {
      array_->addNumber(number);
      buffer.move(buffer_);
    }
    std::unique_ptr<Json::Streamer> streamer_;
    Buffer::Instance& buffer_;
    Json::Streamer::ArrayPtr array_;
  };

  Buffer::OwnedImpl request_buffer;
  Buffer::OwnedImpl buffer;
  {
    Foo foo(std::make_unique<Json::Streamer>(buffer), buffer);
    foo.foo(request_buffer, 1);
    foo.foo(request_buffer, 2);
  }
  request_buffer.move(buffer);
  EXPECT_EQ(request_buffer.toString(), "[1,2]");
}

} // namespace
} // namespace Server
} // namespace Envoy
