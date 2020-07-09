#include <algorithm>
#include <fstream>
#include <memory>
#include <regex>
#include <unordered_map>
#include <vector>

#include "envoy/admin/v3/clusters.pb.h"
#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/admin/v3/server_info.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/json/json_object.h"
#include "envoy/upstream/outlier_detection.h"
#include "envoy/upstream/upstream.h"

#include "common/http/message_impl.h"
#include "common/json/json_loader.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"
#include "common/upstream/upstream_impl.h"

#include "test/server/admin/admin_instance.h"
#include "test/test_common/logging.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "absl/strings/match.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::HasSubstr;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;

namespace Envoy {
namespace Server {

INSTANTIATE_TEST_SUITE_P(IpVersions, AdminInstanceTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(AdminInstanceTest, MutatesErrorWithGet) {
  Buffer::OwnedImpl data;
  Http::TestResponseHeaderMapImpl header_map;
  const std::string path("/healthcheck/fail");
  // TODO(jmarantz): the call to getCallback should be made to fail, but as an interim we will
  // just issue a warning, so that scripts using curl GET commands to mutate state can be fixed.
  EXPECT_LOG_CONTAINS("error",
                      "admin path \"" + path + "\" mutates state, method=GET rather than POST",
                      EXPECT_EQ(Http::Code::MethodNotAllowed, getCallback(path, header_map, data)));
}

TEST_P(AdminInstanceTest, WriteAddressToFile) {
  std::ifstream address_file(address_out_path_);
  std::string address_from_file;
  std::getline(address_file, address_from_file);
  EXPECT_EQ(admin_.socket().localAddress()->asString(), address_from_file);
}

TEST_P(AdminInstanceTest, AdminBadAddressOutPath) {
  std::string bad_path = TestEnvironment::temporaryPath("some/unlikely/bad/path/admin.address");
  AdminImpl admin_bad_address_out_path(cpu_profile_path_, server_);
  EXPECT_LOG_CONTAINS(
      "critical", "cannot open admin address output file " + bad_path + " for writing.",
      admin_bad_address_out_path.startHttpListener(
          "/dev/null", bad_path, Network::Test::getCanonicalLoopbackAddress(GetParam()), nullptr,
          listener_scope_.createScope("listener.admin.")));
  EXPECT_FALSE(std::ifstream(bad_path));
}

TEST_P(AdminInstanceTest, CustomHandler) {
  auto callback = [](absl::string_view, Http::HeaderMap&, Buffer::Instance&,
                     AdminStream&) -> Http::Code { return Http::Code::Accepted; };

  // Test removable handler.
  EXPECT_NO_LOGS(EXPECT_TRUE(admin_.addHandler("/foo/bar", "hello", callback, true, false)));
  Http::TestResponseHeaderMapImpl header_map;
  Buffer::OwnedImpl response;
  EXPECT_EQ(Http::Code::Accepted, getCallback("/foo/bar", header_map, response));

  // Test that removable handler gets removed.
  EXPECT_TRUE(admin_.removeHandler("/foo/bar"));
  EXPECT_EQ(Http::Code::NotFound, getCallback("/foo/bar", header_map, response));
  EXPECT_FALSE(admin_.removeHandler("/foo/bar"));

  // Add non removable handler.
  EXPECT_TRUE(admin_.addHandler("/foo/bar", "hello", callback, false, false));
  EXPECT_EQ(Http::Code::Accepted, getCallback("/foo/bar", header_map, response));

  // Add again and make sure it is not there twice.
  EXPECT_FALSE(admin_.addHandler("/foo/bar", "hello", callback, false, false));

  // Try to remove non removable handler, and make sure it is not removed.
  EXPECT_FALSE(admin_.removeHandler("/foo/bar"));
  EXPECT_EQ(Http::Code::Accepted, getCallback("/foo/bar", header_map, response));
}

TEST_P(AdminInstanceTest, RejectHandlerWithXss) {
  auto callback = [](absl::string_view, Http::HeaderMap&, Buffer::Instance&,
                     AdminStream&) -> Http::Code { return Http::Code::Accepted; };
  EXPECT_LOG_CONTAINS("error",
                      "filter \"/foo<script>alert('hi')</script>\" contains invalid character '<'",
                      EXPECT_FALSE(admin_.addHandler("/foo<script>alert('hi')</script>", "hello",
                                                     callback, true, false)));
}

TEST_P(AdminInstanceTest, RejectHandlerWithEmbeddedQuery) {
  auto callback = [](absl::string_view, Http::HeaderMap&, Buffer::Instance&,
                     AdminStream&) -> Http::Code { return Http::Code::Accepted; };
  EXPECT_LOG_CONTAINS("error",
                      "filter \"/bar?queryShouldNotBeInPrefix\" contains invalid character '?'",
                      EXPECT_FALSE(admin_.addHandler("/bar?queryShouldNotBeInPrefix", "hello",
                                                     callback, true, false)));
}

TEST_P(AdminInstanceTest, EscapeHelpTextWithPunctuation) {
  auto callback = [](absl::string_view, Http::HeaderMap&, Buffer::Instance&,
                     AdminStream&) -> Http::Code { return Http::Code::Accepted; };

  // It's OK to have help text with HTML characters in it, but when we render the home
  // page they need to be escaped.
  const std::string planets = "jupiter>saturn>mars";
  EXPECT_TRUE(admin_.addHandler("/planets", planets, callback, true, false));

  Http::TestResponseHeaderMapImpl header_map;
  Buffer::OwnedImpl response;
  EXPECT_EQ(Http::Code::OK, getCallback("/", header_map, response));
  const Http::HeaderString& content_type = header_map.ContentType()->value();
  EXPECT_THAT(std::string(content_type.getStringView()), testing::HasSubstr("text/html"));
  EXPECT_EQ(-1, response.search(planets.data(), planets.size(), 0, 0));
  const std::string escaped_planets = "jupiter&gt;saturn&gt;mars";
  EXPECT_NE(-1, response.search(escaped_planets.data(), escaped_planets.size(), 0, 0));
}

TEST_P(AdminInstanceTest, HelpUsesFormForMutations) {
  Http::TestResponseHeaderMapImpl header_map;
  Buffer::OwnedImpl response;
  EXPECT_EQ(Http::Code::OK, getCallback("/", header_map, response));
  const std::string logging_action = "<form action='logging' method='post'";
  const std::string stats_href = "<a href='stats'";
  EXPECT_NE(-1, response.search(logging_action.data(), logging_action.size(), 0, 0));
  EXPECT_NE(-1, response.search(stats_href.data(), stats_href.size(), 0, 0));
}

TEST_P(AdminInstanceTest, ConfigDump) {
  Buffer::OwnedImpl response;
  Http::TestResponseHeaderMapImpl header_map;
  auto entry = admin_.getConfigTracker().add("foo", [] {
    auto msg = std::make_unique<ProtobufWkt::StringValue>();
    msg->set_value("bar");
    return msg;
  });
  const std::string expected_json = R"EOF({
 "configs": [
  {
   "@type": "type.googleapis.com/google.protobuf.StringValue",
   "value": "bar"
  }
 ]
}
)EOF";
  EXPECT_EQ(Http::Code::OK, getCallback("/config_dump", header_map, response));
  std::string output = response.toString();
  EXPECT_EQ(expected_json, output);
}

TEST_P(AdminInstanceTest, ConfigDumpMaintainsOrder) {
  // Add configs in random order and validate config_dump dumps in the order.
  auto bootstrap_entry = admin_.getConfigTracker().add("bootstrap", [] {
    auto msg = std::make_unique<ProtobufWkt::StringValue>();
    msg->set_value("bootstrap_config");
    return msg;
  });
  auto route_entry = admin_.getConfigTracker().add("routes", [] {
    auto msg = std::make_unique<ProtobufWkt::StringValue>();
    msg->set_value("routes_config");
    return msg;
  });
  auto listener_entry = admin_.getConfigTracker().add("listeners", [] {
    auto msg = std::make_unique<ProtobufWkt::StringValue>();
    msg->set_value("listeners_config");
    return msg;
  });
  auto cluster_entry = admin_.getConfigTracker().add("clusters", [] {
    auto msg = std::make_unique<ProtobufWkt::StringValue>();
    msg->set_value("clusters_config");
    return msg;
  });
  const std::string expected_json = R"EOF({
 "configs": [
  {
   "@type": "type.googleapis.com/google.protobuf.StringValue",
   "value": "bootstrap_config"
  },
  {
   "@type": "type.googleapis.com/google.protobuf.StringValue",
   "value": "clusters_config"
  },
  {
   "@type": "type.googleapis.com/google.protobuf.StringValue",
   "value": "listeners_config"
  },
  {
   "@type": "type.googleapis.com/google.protobuf.StringValue",
   "value": "routes_config"
  }
 ]
}
)EOF";
  // Run it multiple times and validate that order is preserved.
  for (size_t i = 0; i < 5; i++) {
    Buffer::OwnedImpl response;
    Http::TestResponseHeaderMapImpl header_map;
    EXPECT_EQ(Http::Code::OK, getCallback("/config_dump", header_map, response));
    const std::string output = response.toString();
    EXPECT_EQ(expected_json, output);
  }
}

// helper method for adding host's info
void addHostInfo(NiceMock<Upstream::MockHost>& host, const std::string& hostname,
                 const std::string& address_url, envoy::config::core::v3::Locality& locality,
                 const std::string& hostname_for_healthcheck,
                 const std::string& healthcheck_address_url, int weight, int priority) {
  ON_CALL(host, locality()).WillByDefault(ReturnRef(locality));

  Network::Address::InstanceConstSharedPtr address = Network::Utility::resolveUrl(address_url);
  ON_CALL(host, address()).WillByDefault(Return(address));
  ON_CALL(host, hostname()).WillByDefault(ReturnRef(hostname));

  ON_CALL(host, hostnameForHealthChecks()).WillByDefault(ReturnRef(hostname_for_healthcheck));
  Network::Address::InstanceConstSharedPtr healthcheck_address =
      Network::Utility::resolveUrl(healthcheck_address_url);
  ON_CALL(host, healthCheckAddress()).WillByDefault(Return(healthcheck_address));

  auto metadata = std::make_shared<envoy::config::core::v3::Metadata>();
  ON_CALL(host, metadata()).WillByDefault(Return(metadata));

  ON_CALL(host, health()).WillByDefault(Return(Upstream::Host::Health::Healthy));

  ON_CALL(host, weight()).WillByDefault(Return(weight));
  ON_CALL(host, priority()).WillByDefault(Return(priority));
}

// Test that using ?include_eds parameter adds EDS to the config dump.
TEST_P(AdminInstanceTest, ConfigDumpWithEndpoint) {
  Upstream::ClusterManager::ClusterInfoMap cluster_map;
  ON_CALL(server_.cluster_manager_, clusters()).WillByDefault(ReturnPointee(&cluster_map));

  NiceMock<Upstream::MockClusterMockPrioritySet> cluster;
  cluster_map.emplace(cluster.info_->name_, cluster);

  ON_CALL(*cluster.info_, addedViaApi()).WillByDefault(Return(false));

  Upstream::MockHostSet* host_set = cluster.priority_set_.getMockHostSet(0);
  auto host = std::make_shared<NiceMock<Upstream::MockHost>>();
  host_set->hosts_.emplace_back(host);

  envoy::config::core::v3::Locality locality;
  const std::string hostname_for_healthcheck = "test_hostname_healthcheck";
  const std::string hostname = "foo.com";

  addHostInfo(*host, hostname, "tcp://1.2.3.4:80", locality, hostname_for_healthcheck,
              "tcp://1.2.3.5:90", 5, 6);

  Buffer::OwnedImpl response;
  Http::TestResponseHeaderMapImpl header_map;
  EXPECT_EQ(Http::Code::OK, getCallback("/config_dump?include_eds", header_map, response));
  std::string output = response.toString();
  const std::string expected_json = R"EOF({
 "configs": [
  {
   "@type": "type.googleapis.com/envoy.admin.v3.EndpointsConfigDump",
   "static_endpoint_configs": [
    {
     "endpoint_config": {
      "@type": "type.googleapis.com/envoy.config.endpoint.v3.ClusterLoadAssignment",
      "cluster_name": "fake_cluster",
      "endpoints": [
       {
        "locality": {},
        "lb_endpoints": [
         {
          "endpoint": {
           "address": {
            "socket_address": {
             "address": "1.2.3.4",
             "port_value": 80
            }
           },
           "health_check_config": {
            "port_value": 90,
            "hostname": "test_hostname_healthcheck"
           },
           "hostname": "foo.com"
          },
          "health_status": "HEALTHY",
          "metadata": {},
          "load_balancing_weight": 5
         }
        ],
        "priority": 6
       }
      ],
      "policy": {
       "overprovisioning_factor": 140
      }
     }
    }
   ]
  }
 ]
}
)EOF";
  EXPECT_EQ(expected_json, output);
}

// Test EDS config dump while multiple localities and priorities exist
TEST_P(AdminInstanceTest, ConfigDumpWithLocalityEndpoint) {
  Upstream::ClusterManager::ClusterInfoMap cluster_map;
  ON_CALL(server_.cluster_manager_, clusters()).WillByDefault(ReturnPointee(&cluster_map));

  NiceMock<Upstream::MockClusterMockPrioritySet> cluster;
  cluster_map.emplace(cluster.info_->name_, cluster);

  ON_CALL(*cluster.info_, addedViaApi()).WillByDefault(Return(false));

  Upstream::MockHostSet* host_set_1 = cluster.priority_set_.getMockHostSet(0);
  auto host_1 = std::make_shared<NiceMock<Upstream::MockHost>>();
  host_set_1->hosts_.emplace_back(host_1);

  envoy::config::core::v3::Locality locality_1;
  locality_1.set_region("oceania");
  locality_1.set_zone("hello");
  locality_1.set_sub_zone("world");

  const std::string hostname_for_healthcheck = "test_hostname_healthcheck";
  const std::string hostname_1 = "foo.com";

  addHostInfo(*host_1, hostname_1, "tcp://1.2.3.4:80", locality_1, hostname_for_healthcheck,
              "tcp://1.2.3.5:90", 5, 6);

  auto host_2 = std::make_shared<NiceMock<Upstream::MockHost>>();
  host_set_1->hosts_.emplace_back(host_2);
  const std::string empty_hostname_for_healthcheck = "";
  const std::string hostname_2 = "boo.com";

  addHostInfo(*host_2, hostname_2, "tcp://1.2.3.7:8", locality_1, empty_hostname_for_healthcheck,
              "tcp://1.2.3.7:8", 3, 6);

  envoy::config::core::v3::Locality locality_2;

  auto host_3 = std::make_shared<NiceMock<Upstream::MockHost>>();
  host_set_1->hosts_.emplace_back(host_3);
  const std::string hostname_3 = "coo.com";

  addHostInfo(*host_3, hostname_3, "tcp://1.2.3.8:8", locality_2, empty_hostname_for_healthcheck,
              "tcp://1.2.3.8:8", 3, 4);

  std::vector<Upstream::HostVector> locality_hosts = {
      {Upstream::HostSharedPtr(host_1), Upstream::HostSharedPtr(host_2)},
      {Upstream::HostSharedPtr(host_3)}};
  auto hosts_per_locality = new Upstream::HostsPerLocalityImpl(std::move(locality_hosts), false);

  Upstream::LocalityWeightsConstSharedPtr locality_weights{new Upstream::LocalityWeights{1, 3}};
  ON_CALL(*host_set_1, hostsPerLocality()).WillByDefault(ReturnRef(*hosts_per_locality));
  ON_CALL(*host_set_1, localityWeights()).WillByDefault(Return(locality_weights));

  Upstream::MockHostSet* host_set_2 = cluster.priority_set_.getMockHostSet(1);
  auto host_4 = std::make_shared<NiceMock<Upstream::MockHost>>();
  host_set_2->hosts_.emplace_back(host_4);
  const std::string hostname_4 = "doo.com";

  addHostInfo(*host_4, hostname_4, "tcp://1.2.3.9:8", locality_1, empty_hostname_for_healthcheck,
              "tcp://1.2.3.9:8", 3, 2);

  Buffer::OwnedImpl response;
  Http::TestResponseHeaderMapImpl header_map;
  EXPECT_EQ(Http::Code::OK, getCallback("/config_dump?include_eds", header_map, response));
  std::string output = response.toString();
  const std::string expected_json = R"EOF({
 "configs": [
  {
   "@type": "type.googleapis.com/envoy.admin.v3.EndpointsConfigDump",
   "static_endpoint_configs": [
    {
     "endpoint_config": {
      "@type": "type.googleapis.com/envoy.config.endpoint.v3.ClusterLoadAssignment",
      "cluster_name": "fake_cluster",
      "endpoints": [
       {
        "locality": {
         "region": "oceania",
         "zone": "hello",
         "sub_zone": "world"
        },
        "lb_endpoints": [
         {
          "endpoint": {
           "address": {
            "socket_address": {
             "address": "1.2.3.4",
             "port_value": 80
            }
           },
           "health_check_config": {
            "port_value": 90,
            "hostname": "test_hostname_healthcheck"
           },
           "hostname": "foo.com"
          },
          "health_status": "HEALTHY",
          "metadata": {},
          "load_balancing_weight": 5
         },
         {
          "endpoint": {
           "address": {
            "socket_address": {
             "address": "1.2.3.7",
             "port_value": 8
            }
           },
           "health_check_config": {},
           "hostname": "boo.com"
          },
          "health_status": "HEALTHY",
          "metadata": {},
          "load_balancing_weight": 3
         }
        ],
        "load_balancing_weight": 1,
        "priority": 6
       },
       {
        "locality": {},
        "lb_endpoints": [
         {
          "endpoint": {
           "address": {
            "socket_address": {
             "address": "1.2.3.8",
             "port_value": 8
            }
           },
           "health_check_config": {},
           "hostname": "coo.com"
          },
          "health_status": "HEALTHY",
          "metadata": {},
          "load_balancing_weight": 3
         }
        ],
        "load_balancing_weight": 3,
        "priority": 4
       },
       {
        "locality": {
         "region": "oceania",
         "zone": "hello",
         "sub_zone": "world"
        },
        "lb_endpoints": [
         {
          "endpoint": {
           "address": {
            "socket_address": {
             "address": "1.2.3.9",
             "port_value": 8
            }
           },
           "health_check_config": {},
           "hostname": "doo.com"
          },
          "health_status": "HEALTHY",
          "metadata": {},
          "load_balancing_weight": 3
         }
        ],
        "priority": 2
       }
      ],
      "policy": {
       "overprovisioning_factor": 140
      }
     }
    }
   ]
  }
 ]
}
)EOF";
  EXPECT_EQ(expected_json, output);
  delete (hosts_per_locality);
}

// Test that using the resource query parameter filters the config dump.
// We add both static and dynamic listener config to the dump, but expect only
// dynamic in the JSON with ?resource=dynamic_listeners.
TEST_P(AdminInstanceTest, ConfigDumpFiltersByResource) {
  Buffer::OwnedImpl response;
  Http::TestResponseHeaderMapImpl header_map;
  auto listeners = admin_.getConfigTracker().add("listeners", [] {
    auto msg = std::make_unique<envoy::admin::v3::ListenersConfigDump>();
    auto dyn_listener = msg->add_dynamic_listeners();
    dyn_listener->set_name("foo");
    auto stat_listener = msg->add_static_listeners();
    envoy::config::listener::v3::Listener listener;
    listener.set_name("bar");
    stat_listener->mutable_listener()->PackFrom(listener);
    return msg;
  });
  const std::string expected_json = R"EOF({
 "configs": [
  {
   "@type": "type.googleapis.com/envoy.admin.v3.ListenersConfigDump.DynamicListener",
   "name": "foo"
  }
 ]
}
)EOF";
  EXPECT_EQ(Http::Code::OK,
            getCallback("/config_dump?resource=dynamic_listeners", header_map, response));
  std::string output = response.toString();
  EXPECT_EQ(expected_json, output);
}

// Test that using the resource query parameter filters the config dump including EDS.
// We add both static and dynamic endpoint config to the dump, but expect only
// dynamic in the JSON with ?resource=dynamic_endpoint_configs.
TEST_P(AdminInstanceTest, ConfigDumpWithEndpointFiltersByResource) {
  Upstream::ClusterManager::ClusterInfoMap cluster_map;
  ON_CALL(server_.cluster_manager_, clusters()).WillByDefault(ReturnPointee(&cluster_map));

  NiceMock<Upstream::MockClusterMockPrioritySet> cluster_1;
  cluster_map.emplace(cluster_1.info_->name_, cluster_1);

  ON_CALL(*cluster_1.info_, addedViaApi()).WillByDefault(Return(true));

  Upstream::MockHostSet* host_set = cluster_1.priority_set_.getMockHostSet(0);
  auto host_1 = std::make_shared<NiceMock<Upstream::MockHost>>();
  host_set->hosts_.emplace_back(host_1);

  envoy::config::core::v3::Locality locality;
  const std::string hostname_for_healthcheck = "test_hostname_healthcheck";
  const std::string hostname_1 = "foo.com";

  addHostInfo(*host_1, hostname_1, "tcp://1.2.3.4:80", locality, hostname_for_healthcheck,
              "tcp://1.2.3.5:90", 5, 6);

  NiceMock<Upstream::MockClusterMockPrioritySet> cluster_2;
  cluster_2.info_->name_ = "fake_cluster_2";
  cluster_map.emplace(cluster_2.info_->name_, cluster_2);

  ON_CALL(*cluster_2.info_, addedViaApi()).WillByDefault(Return(false));

  Upstream::MockHostSet* host_set_2 = cluster_2.priority_set_.getMockHostSet(0);
  auto host_2 = std::make_shared<NiceMock<Upstream::MockHost>>();
  host_set_2->hosts_.emplace_back(host_2);
  const std::string hostname_2 = "boo.com";

  addHostInfo(*host_2, hostname_2, "tcp://1.2.3.5:8", locality, hostname_for_healthcheck,
              "tcp://1.2.3.4:1", 3, 4);

  Buffer::OwnedImpl response;
  Http::TestResponseHeaderMapImpl header_map;
  EXPECT_EQ(Http::Code::OK,
            getCallback("/config_dump?include_eds&resource=dynamic_endpoint_configs", header_map,
                        response));
  std::string output = response.toString();
  const std::string expected_json = R"EOF({
 "configs": [
  {
   "@type": "type.googleapis.com/envoy.admin.v3.EndpointsConfigDump.DynamicEndpointConfig",
   "endpoint_config": {
    "@type": "type.googleapis.com/envoy.config.endpoint.v3.ClusterLoadAssignment",
    "cluster_name": "fake_cluster",
    "endpoints": [
     {
      "locality": {},
      "lb_endpoints": [
       {
        "endpoint": {
         "address": {
          "socket_address": {
           "address": "1.2.3.4",
           "port_value": 80
          }
         },
         "health_check_config": {
          "port_value": 90,
          "hostname": "test_hostname_healthcheck"
         },
         "hostname": "foo.com"
        },
        "health_status": "HEALTHY",
        "metadata": {},
        "load_balancing_weight": 5
       }
      ],
      "priority": 6
     }
    ],
    "policy": {
     "overprovisioning_factor": 140
    }
   }
  }
 ]
}
)EOF";
  EXPECT_EQ(expected_json, output);
}

// Test that using the mask query parameter filters the config dump.
// We add both static and dynamic listener config to the dump, but expect only
// dynamic in the JSON with ?mask=dynamic_listeners.
TEST_P(AdminInstanceTest, ConfigDumpFiltersByMask) {
  Buffer::OwnedImpl response;
  Http::TestResponseHeaderMapImpl header_map;
  auto listeners = admin_.getConfigTracker().add("listeners", [] {
    auto msg = std::make_unique<envoy::admin::v3::ListenersConfigDump>();
    auto dyn_listener = msg->add_dynamic_listeners();
    dyn_listener->set_name("foo");
    auto stat_listener = msg->add_static_listeners();
    envoy::config::listener::v3::Listener listener;
    listener.set_name("bar");
    stat_listener->mutable_listener()->PackFrom(listener);
    return msg;
  });
  const std::string expected_json = R"EOF({
 "configs": [
  {
   "@type": "type.googleapis.com/envoy.admin.v3.ListenersConfigDump",
   "dynamic_listeners": [
    {
     "name": "foo"
    }
   ]
  }
 ]
}
)EOF";
  EXPECT_EQ(Http::Code::OK,
            getCallback("/config_dump?mask=dynamic_listeners", header_map, response));
  std::string output = response.toString();
  EXPECT_EQ(expected_json, output);
}

ProtobufTypes::MessagePtr testDumpClustersConfig() {
  auto msg = std::make_unique<envoy::admin::v3::ClustersConfigDump>();
  auto* static_cluster = msg->add_static_clusters();
  envoy::config::cluster::v3::Cluster inner_cluster;
  inner_cluster.set_name("foo");
  inner_cluster.set_ignore_health_on_host_removal(true);
  static_cluster->mutable_cluster()->PackFrom(inner_cluster);

  auto* dyn_cluster = msg->add_dynamic_active_clusters();
  dyn_cluster->set_version_info("baz");
  dyn_cluster->mutable_last_updated()->set_seconds(5);
  envoy::config::cluster::v3::Cluster inner_dyn_cluster;
  inner_dyn_cluster.set_name("bar");
  inner_dyn_cluster.set_ignore_health_on_host_removal(true);
  inner_dyn_cluster.mutable_http2_protocol_options()->set_allow_connect(true);
  dyn_cluster->mutable_cluster()->PackFrom(inner_dyn_cluster);
  return msg;
}

// Test that when using both resource and mask query parameters the JSON output contains
// only the desired resource and the fields specified in the mask.
TEST_P(AdminInstanceTest, ConfigDumpFiltersByResourceAndMask) {
  Buffer::OwnedImpl response;
  Http::TestResponseHeaderMapImpl header_map;
  auto clusters = admin_.getConfigTracker().add("clusters", testDumpClustersConfig);
  const std::string expected_json = R"EOF({
 "configs": [
  {
   "@type": "type.googleapis.com/envoy.admin.v3.ClustersConfigDump.DynamicCluster",
   "version_info": "baz",
   "cluster": {
    "@type": "type.googleapis.com/envoy.config.cluster.v3.Cluster",
    "name": "bar",
    "http2_protocol_options": {
     "allow_connect": true
    }
   }
  }
 ]
}
)EOF";
  EXPECT_EQ(Http::Code::OK, getCallback("/config_dump?resource=dynamic_active_clusters&mask="
                                        "cluster.name,version_info,cluster.http2_protocol_options",
                                        header_map, response));
  std::string output = response.toString();
  EXPECT_EQ(expected_json, output);
}

// Test that no fields are present in the JSON output if there is no intersection between the fields
// of the config dump and the fields present in the mask query parameter.
TEST_P(AdminInstanceTest, ConfigDumpNonExistentMask) {
  Buffer::OwnedImpl response;
  Http::TestResponseHeaderMapImpl header_map;
  auto clusters = admin_.getConfigTracker().add("clusters", testDumpClustersConfig);
  const std::string expected_json = R"EOF({
 "configs": [
  {
   "@type": "type.googleapis.com/envoy.admin.v3.ClustersConfigDump.StaticCluster"
  }
 ]
}
)EOF";
  EXPECT_EQ(Http::Code::OK,
            getCallback("/config_dump?resource=static_clusters&mask=bad", header_map, response));
  std::string output = response.toString();
  EXPECT_EQ(expected_json, output);
}

// Test that a 404 Not found is returned if a non-existent resource is passed in as the
// resource query parameter.
TEST_P(AdminInstanceTest, ConfigDumpNonExistentResource) {
  Buffer::OwnedImpl response;
  Http::TestResponseHeaderMapImpl header_map;
  auto listeners = admin_.getConfigTracker().add("listeners", [] {
    auto msg = std::make_unique<ProtobufWkt::StringValue>();
    msg->set_value("listeners_config");
    return msg;
  });
  EXPECT_EQ(Http::Code::NotFound, getCallback("/config_dump?resource=foo", header_map, response));
}

// Test that a 400 Bad Request is returned if the passed resource query parameter is not a
// repeated field.
TEST_P(AdminInstanceTest, ConfigDumpResourceNotRepeated) {
  Buffer::OwnedImpl response;
  Http::TestResponseHeaderMapImpl header_map;
  auto clusters = admin_.getConfigTracker().add("clusters", [] {
    auto msg = std::make_unique<envoy::admin::v3::ClustersConfigDump>();
    msg->set_version_info("foo");
    return msg;
  });
  EXPECT_EQ(Http::Code::BadRequest,
            getCallback("/config_dump?resource=version_info", header_map, response));
}

TEST_P(AdminInstanceTest, ClustersJson) {
  Upstream::ClusterManager::ClusterInfoMap cluster_map;
  ON_CALL(server_.cluster_manager_, clusters()).WillByDefault(ReturnPointee(&cluster_map));

  NiceMock<Upstream::MockClusterMockPrioritySet> cluster;
  cluster_map.emplace(cluster.info_->name_, cluster);

  NiceMock<Upstream::Outlier::MockDetector> outlier_detector;
  ON_CALL(Const(cluster), outlierDetector()).WillByDefault(Return(&outlier_detector));
  ON_CALL(outlier_detector,
          successRateEjectionThreshold(
              Upstream::Outlier::DetectorHostMonitor::SuccessRateMonitorType::ExternalOrigin))
      .WillByDefault(Return(6.0));
  ON_CALL(outlier_detector,
          successRateEjectionThreshold(
              Upstream::Outlier::DetectorHostMonitor::SuccessRateMonitorType::LocalOrigin))
      .WillByDefault(Return(9.0));

  ON_CALL(*cluster.info_, addedViaApi()).WillByDefault(Return(true));

  Upstream::MockHostSet* host_set = cluster.priority_set_.getMockHostSet(0);
  auto host = std::make_shared<NiceMock<Upstream::MockHost>>();

  envoy::config::core::v3::Locality locality;
  locality.set_region("test_region");
  locality.set_zone("test_zone");
  locality.set_sub_zone("test_sub_zone");
  ON_CALL(*host, locality()).WillByDefault(ReturnRef(locality));

  host_set->hosts_.emplace_back(host);
  Network::Address::InstanceConstSharedPtr address =
      Network::Utility::resolveUrl("tcp://1.2.3.4:80");
  ON_CALL(*host, address()).WillByDefault(Return(address));
  const std::string hostname = "foo.com";
  ON_CALL(*host, hostname()).WillByDefault(ReturnRef(hostname));

  // Add stats in random order and validate that they come in order.
  Stats::PrimitiveCounter test_counter;
  test_counter.add(10);
  Stats::PrimitiveCounter rest_counter;
  rest_counter.add(10);
  Stats::PrimitiveCounter arest_counter;
  arest_counter.add(5);
  std::vector<std::pair<absl::string_view, Stats::PrimitiveCounterReference>> counters = {
      {"arest_counter", arest_counter},
      {"rest_counter", rest_counter},
      {"test_counter", test_counter},
  };
  Stats::PrimitiveGauge test_gauge;
  test_gauge.set(11);
  Stats::PrimitiveGauge atest_gauge;
  atest_gauge.set(10);
  std::vector<std::pair<absl::string_view, Stats::PrimitiveGaugeReference>> gauges = {
      {"atest_gauge", atest_gauge},
      {"test_gauge", test_gauge},
  };
  ON_CALL(*host, counters()).WillByDefault(Invoke([&counters]() { return counters; }));
  ON_CALL(*host, gauges()).WillByDefault(Invoke([&gauges]() { return gauges; }));

  ON_CALL(*host, healthFlagGet(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC))
      .WillByDefault(Return(true));
  ON_CALL(*host, healthFlagGet(Upstream::Host::HealthFlag::FAILED_OUTLIER_CHECK))
      .WillByDefault(Return(true));
  ON_CALL(*host, healthFlagGet(Upstream::Host::HealthFlag::FAILED_EDS_HEALTH))
      .WillByDefault(Return(false));
  ON_CALL(*host, healthFlagGet(Upstream::Host::HealthFlag::DEGRADED_ACTIVE_HC))
      .WillByDefault(Return(true));
  ON_CALL(*host, healthFlagGet(Upstream::Host::HealthFlag::DEGRADED_EDS_HEALTH))
      .WillByDefault(Return(true));
  ON_CALL(*host, healthFlagGet(Upstream::Host::HealthFlag::PENDING_DYNAMIC_REMOVAL))
      .WillByDefault(Return(true));

  ON_CALL(
      host->outlier_detector_,
      successRate(Upstream::Outlier::DetectorHostMonitor::SuccessRateMonitorType::ExternalOrigin))
      .WillByDefault(Return(43.2));
  ON_CALL(*host, weight()).WillByDefault(Return(5));
  ON_CALL(host->outlier_detector_,
          successRate(Upstream::Outlier::DetectorHostMonitor::SuccessRateMonitorType::LocalOrigin))
      .WillByDefault(Return(93.2));
  ON_CALL(*host, priority()).WillByDefault(Return(6));

  Buffer::OwnedImpl response;
  Http::TestResponseHeaderMapImpl header_map;
  EXPECT_EQ(Http::Code::OK, getCallback("/clusters?format=json", header_map, response));
  std::string output_json = response.toString();
  envoy::admin::v3::Clusters output_proto;
  TestUtility::loadFromJson(output_json, output_proto);

  const std::string expected_json = R"EOF({
 "cluster_statuses": [
  {
   "name": "fake_cluster",
   "success_rate_ejection_threshold": {
    "value": 6
   },
   "local_origin_success_rate_ejection_threshold": {
    "value": 9
   },
   "added_via_api": true,
   "host_statuses": [
    {
     "address": {
      "socket_address": {
       "protocol": "TCP",
       "address": "1.2.3.4",
       "port_value": 80
      }
     },
     "stats": [
       {
       "name": "arest_counter",
       "value": "5",
       "type": "COUNTER"
       },
       {
       "name": "rest_counter",
       "value": "10",
       "type": "COUNTER"
      },
      {
       "name": "test_counter",
       "value": "10",
       "type": "COUNTER"
      },
      {
       "name": "atest_gauge",
       "value": "10",
       "type": "GAUGE"
      },
      {
       "name": "test_gauge",
       "value": "11",
       "type": "GAUGE"
      },
     ],
     "health_status": {
      "eds_health_status": "DEGRADED",
      "failed_active_health_check": true,
      "failed_outlier_check": true,
      "failed_active_degraded_check": true,
      "pending_dynamic_removal": true
     },
     "success_rate": {
      "value": 43.2
     },
     "weight": 5,
     "hostname": "foo.com",
     "priority": 6,
     "local_origin_success_rate": {
      "value": 93.2
     },
     "locality": {
       "region": "test_region",
       "zone": "test_zone",
       "sub_zone": "test_sub_zone"
     }
    }
   ]
  }
 ]
}
)EOF";

  envoy::admin::v3::Clusters expected_proto;
  TestUtility::loadFromJson(expected_json, expected_proto);

  // Ensure the protos created from each JSON are equivalent.
  EXPECT_THAT(output_proto, ProtoEq(expected_proto));

  // Ensure that the normal text format is used by default.
  Buffer::OwnedImpl response2;
  EXPECT_EQ(Http::Code::OK, getCallback("/clusters", header_map, response2));
  const std::string expected_text = R"EOF(fake_cluster::outlier::success_rate_average::0
fake_cluster::outlier::success_rate_ejection_threshold::6
fake_cluster::outlier::local_origin_success_rate_average::0
fake_cluster::outlier::local_origin_success_rate_ejection_threshold::9
fake_cluster::default_priority::max_connections::1
fake_cluster::default_priority::max_pending_requests::1024
fake_cluster::default_priority::max_requests::1024
fake_cluster::default_priority::max_retries::1
fake_cluster::high_priority::max_connections::1
fake_cluster::high_priority::max_pending_requests::1024
fake_cluster::high_priority::max_requests::1024
fake_cluster::high_priority::max_retries::1
fake_cluster::added_via_api::true
fake_cluster::1.2.3.4:80::arest_counter::5
fake_cluster::1.2.3.4:80::atest_gauge::10
fake_cluster::1.2.3.4:80::rest_counter::10
fake_cluster::1.2.3.4:80::test_counter::10
fake_cluster::1.2.3.4:80::test_gauge::11
fake_cluster::1.2.3.4:80::hostname::foo.com
fake_cluster::1.2.3.4:80::health_flags::/failed_active_hc/failed_outlier_check/degraded_active_hc/degraded_eds_health/pending_dynamic_removal
fake_cluster::1.2.3.4:80::weight::5
fake_cluster::1.2.3.4:80::region::test_region
fake_cluster::1.2.3.4:80::zone::test_zone
fake_cluster::1.2.3.4:80::sub_zone::test_sub_zone
fake_cluster::1.2.3.4:80::canary::false
fake_cluster::1.2.3.4:80::priority::6
fake_cluster::1.2.3.4:80::success_rate::43.2
fake_cluster::1.2.3.4:80::local_origin_success_rate::93.2
)EOF";
  EXPECT_EQ(expected_text, response2.toString());
}

} // namespace Server
} // namespace Envoy
