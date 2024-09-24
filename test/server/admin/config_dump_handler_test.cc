#include "test/integration/filters/test_listener_filter.pb.h"
#include "test/integration/filters/test_network_filter.pb.h"
#include "test/server/admin/admin_instance.h"

using testing::HasSubstr;
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;

namespace Envoy {
namespace Server {

INSTANTIATE_TEST_SUITE_P(IpVersions, AdminInstanceTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// helper method for adding host's info
void addHostInfo(NiceMock<Upstream::MockHost>& host, const std::string& hostname,
                 const std::string& address_url,
                 const std::vector<std::string> additional_addresses_url,
                 envoy::config::core::v3::Locality& locality,
                 const std::string& hostname_for_healthcheck,
                 const std::string& healthcheck_address_url, int weight, int priority) {
  ON_CALL(host, locality()).WillByDefault(ReturnRef(locality));

  Network::Address::InstanceConstSharedPtr address = *Network::Utility::resolveUrl(address_url);
  ON_CALL(host, address()).WillByDefault(Return(address));
  std::shared_ptr<Upstream::HostImplBase::AddressVector> address_list =
      std::make_shared<Upstream::HostImplBase::AddressVector>();
  address_list->push_back(*Network::Utility::resolveUrl(address_url));
  for (auto& new_addr : additional_addresses_url) {
    address_list->push_back(*Network::Utility::resolveUrl(new_addr));
  }
  ON_CALL(host, addressListOrNull()).WillByDefault(Return(address_list));
  ON_CALL(host, hostname()).WillByDefault(ReturnRef(hostname));

  ON_CALL(host, hostnameForHealthChecks()).WillByDefault(ReturnRef(hostname_for_healthcheck));
  Network::Address::InstanceConstSharedPtr healthcheck_address =
      *Network::Utility::resolveUrl(healthcheck_address_url);
  ON_CALL(host, healthCheckAddress()).WillByDefault(Return(healthcheck_address));

  auto metadata = std::make_shared<envoy::config::core::v3::Metadata>();
  ON_CALL(host, metadata()).WillByDefault(Return(metadata));

  ON_CALL(host, coarseHealth()).WillByDefault(Return(Upstream::Host::Health::Healthy));

  ON_CALL(host, weight()).WillByDefault(Return(weight));
  ON_CALL(host, priority()).WillByDefault(Return(priority));
}

TEST_P(AdminInstanceTest, ConfigDump) {
  Buffer::OwnedImpl response;
  Buffer::OwnedImpl response2;
  Http::TestResponseHeaderMapImpl header_map;
  auto entry = admin_.getConfigTracker().add("foo", [](const Matchers::StringMatcher&) {
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
  EXPECT_EQ(Http::Code::OK,
            getCallback("/config_dump?resource=&mask=&name_regex=", header_map, response2));
  EXPECT_EQ(expected_json, response.toString());
  EXPECT_EQ(expected_json, response2.toString());
}

TEST_P(AdminInstanceTest, ConfigDumpMaintainsOrder) {
  // Add configs in random order and validate config_dump dumps in the order.
  auto bootstrap_entry =
      admin_.getConfigTracker().add("bootstrap", [](const Matchers::StringMatcher&) {
        auto msg = std::make_unique<ProtobufWkt::StringValue>();
        msg->set_value("bootstrap_config");
        return msg;
      });
  auto route_entry = admin_.getConfigTracker().add("routes", [](const Matchers::StringMatcher&) {
    auto msg = std::make_unique<ProtobufWkt::StringValue>();
    msg->set_value("routes_config");
    return msg;
  });
  auto listener_entry =
      admin_.getConfigTracker().add("listeners", [](const Matchers::StringMatcher&) {
        auto msg = std::make_unique<ProtobufWkt::StringValue>();
        msg->set_value("listeners_config");
        return msg;
      });
  auto cluster_entry =
      admin_.getConfigTracker().add("clusters", [](const Matchers::StringMatcher&) {
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

// Test that using ?include_eds parameter adds EDS to the config dump.
TEST_P(AdminInstanceTest, ConfigDumpWithEndpoint) {
  Upstream::ClusterManager::ClusterInfoMaps cluster_maps;
  ON_CALL(server_.cluster_manager_, clusters()).WillByDefault(ReturnPointee(&cluster_maps));

  NiceMock<Upstream::MockClusterMockPrioritySet> cluster;
  cluster_maps.active_clusters_.emplace(cluster.info_->name_, cluster);

  ON_CALL(*cluster.info_, addedViaApi()).WillByDefault(Return(false));

  Upstream::MockHostSet* host_set = cluster.priority_set_.getMockHostSet(0);
  auto host = std::make_shared<NiceMock<Upstream::MockHost>>();
  host_set->hosts_.emplace_back(host);

  envoy::config::core::v3::Locality locality;
  const std::string hostname_for_healthcheck = "test_hostname_healthcheck";
  const std::string hostname = "foo.com";

  addHostInfo(*host, hostname, "tcp://1.2.3.4:80", {"tcp://5.6.7.8:80"}, locality,
              hostname_for_healthcheck, "tcp://1.2.3.5:90", 5, 6);
  // Adding drop_overload config.
  ON_CALL(cluster, dropOverload()).WillByDefault(Return(UnitFloat(0.00035)));
  const std::string drop_overload = "drop_overload";
  ON_CALL(cluster, dropCategory()).WillByDefault(ReturnRef(drop_overload));
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
           "hostname": "foo.com",
           "additional_addresses": [
            {
             "address": {
              "socket_address": {
               "address": "5.6.7.8",
               "port_value": 80
              }
             }
            }
           ]
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
       "drop_overloads": [
        {
         "category": "drop_overload",
         "drop_percentage": {
          "numerator": 350,
          "denominator": "MILLION"
         }
        }
       ],
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
  Upstream::ClusterManager::ClusterInfoMaps cluster_maps;
  ON_CALL(server_.cluster_manager_, clusters()).WillByDefault(ReturnPointee(&cluster_maps));

  NiceMock<Upstream::MockClusterMockPrioritySet> cluster;
  cluster_maps.active_clusters_.emplace(cluster.info_->name_, cluster);

  ON_CALL(*cluster.info_, addedViaApi()).WillByDefault(Return(false));

  const std::string hostname_for_healthcheck = "test_hostname_healthcheck";
  const std::string empty_hostname_for_healthcheck = "";

  envoy::config::core::v3::Locality locality_1;

  Upstream::MockHostSet* host_set_1 = cluster.priority_set_.getMockHostSet(0);
  auto host_1 = std::make_shared<NiceMock<Upstream::MockHost>>();
  host_set_1->hosts_.emplace_back(host_1);
  const std::string hostname_1 = "coo.com";

  addHostInfo(*host_1, hostname_1, "tcp://1.2.3.8:8", {}, locality_1,
              empty_hostname_for_healthcheck, "tcp://1.2.3.8:8", 3, 4);

  const std::string hostname_2 = "foo.com";
  auto host_2 = std::make_shared<NiceMock<Upstream::MockHost>>();
  host_set_1->hosts_.emplace_back(host_2);

  envoy::config::core::v3::Locality locality_2;
  locality_2.set_region("oceania");
  locality_2.set_zone("hello");
  locality_2.set_sub_zone("world");

  addHostInfo(*host_2, hostname_2, "tcp://1.2.3.4:80", {}, locality_2, hostname_for_healthcheck,
              "tcp://1.2.3.5:90", 5, 6);

  auto host_3 = std::make_shared<NiceMock<Upstream::MockHost>>();
  host_set_1->hosts_.emplace_back(host_3);
  const std::string hostname_3 = "boo.com";

  addHostInfo(*host_3, hostname_3, "tcp://1.2.3.7:8", {}, locality_2,
              empty_hostname_for_healthcheck, "tcp://1.2.3.7:8", 3, 6);

  std::vector<Upstream::HostVector> locality_hosts = {
      {Upstream::HostSharedPtr(host_1)},
      {Upstream::HostSharedPtr(host_2), Upstream::HostSharedPtr(host_3)}};
  auto hosts_per_locality = new Upstream::HostsPerLocalityImpl(std::move(locality_hosts), false);

  Upstream::LocalityWeightsConstSharedPtr locality_weights{new Upstream::LocalityWeights{3, 1}};
  ON_CALL(*host_set_1, hostsPerLocality()).WillByDefault(ReturnRef(*hosts_per_locality));
  ON_CALL(*host_set_1, localityWeights()).WillByDefault(Return(locality_weights));

  Upstream::MockHostSet* host_set_2 = cluster.priority_set_.getMockHostSet(1);
  auto host_4 = std::make_shared<NiceMock<Upstream::MockHost>>();
  host_set_2->hosts_.emplace_back(host_4);
  const std::string hostname_4 = "doo.com";

  addHostInfo(*host_4, hostname_4, "tcp://1.2.3.9:8", {}, locality_2,
              empty_hostname_for_healthcheck, "tcp://1.2.3.9:8", 3, 2);

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
  auto listeners = admin_.getConfigTracker().add("listeners", [](const Matchers::StringMatcher&) {
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

// Test that using the resource and name_regex query parameters filter the config dump including
// EDS. We add both static and dynamic endpoint config to the dump, but expect only dynamic in the
// JSON with ?resource=dynamic_endpoint_configs, and only the one named `fake_cluster_2` with
// ?name_regex=fake_cluster_2.
TEST_P(AdminInstanceTest, ConfigDumpWithEndpointFiltersByResourceAndName) {
  Upstream::ClusterManager::ClusterInfoMaps cluster_maps;
  ON_CALL(server_.cluster_manager_, clusters()).WillByDefault(ReturnPointee(&cluster_maps));

  NiceMock<Upstream::MockClusterMockPrioritySet> cluster_1;
  cluster_maps.active_clusters_.emplace(cluster_1.info_->name_, cluster_1);

  ON_CALL(*cluster_1.info_, addedViaApi()).WillByDefault(Return(true));

  Upstream::MockHostSet* host_set = cluster_1.priority_set_.getMockHostSet(0);
  auto host_1 = std::make_shared<NiceMock<Upstream::MockHost>>();
  host_set->hosts_.emplace_back(host_1);

  envoy::config::core::v3::Locality locality;
  const std::string hostname_for_healthcheck = "test_hostname_healthcheck";
  const std::string hostname_1 = "foo.com";

  addHostInfo(*host_1, hostname_1, "tcp://1.2.3.4:80", {}, locality, hostname_for_healthcheck,
              "tcp://1.2.3.5:90", 5, 6);

  NiceMock<Upstream::MockClusterMockPrioritySet> cluster_2;
  cluster_2.info_->name_ = "fake_cluster_2";
  cluster_maps.active_clusters_.emplace(cluster_2.info_->name_, cluster_2);

  ON_CALL(*cluster_2.info_, addedViaApi()).WillByDefault(Return(false));

  Upstream::MockHostSet* host_set_2 = cluster_2.priority_set_.getMockHostSet(0);
  auto host_2 = std::make_shared<NiceMock<Upstream::MockHost>>();
  host_set_2->hosts_.emplace_back(host_2);
  const std::string hostname_2 = "boo.com";

  addHostInfo(*host_2, hostname_2, "tcp://1.2.3.5:8", {}, locality, hostname_for_healthcheck,
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

  // Check that endpoints dump uses the name_matcher.
  Buffer::OwnedImpl response2;
  EXPECT_EQ(Http::Code::OK, getCallback("/config_dump?include_eds=true&name_regex=fake_cluster_2",
                                        header_map, response2));
  const std::string expected_json2 = R"EOF({
 "configs": [
  {
   "@type": "type.googleapis.com/envoy.admin.v3.EndpointsConfigDump",
   "static_endpoint_configs": [
    {
     "endpoint_config": {
      "@type": "type.googleapis.com/envoy.config.endpoint.v3.ClusterLoadAssignment",
      "cluster_name": "fake_cluster_2",
      "endpoints": [
       {
        "locality": {},
        "lb_endpoints": [
         {
          "endpoint": {
           "address": {
            "socket_address": {
             "address": "1.2.3.5",
             "port_value": 8
            }
           },
           "health_check_config": {
            "port_value": 1,
            "hostname": "test_hostname_healthcheck"
           },
           "hostname": "boo.com"
          },
          "health_status": "HEALTHY",
          "metadata": {},
          "load_balancing_weight": 3
         }
        ],
        "priority": 4
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
  EXPECT_EQ(expected_json2, response2.toString());
}

// Test that using the mask query parameter filters the config dump.
// We add both static and dynamic listener config to the dump, but expect only
// dynamic in the JSON with ?mask=dynamic_listeners.
TEST_P(AdminInstanceTest, ConfigDumpFiltersByMask) {
  Buffer::OwnedImpl response;
  Http::TestResponseHeaderMapImpl header_map;
  auto listeners = admin_.getConfigTracker().add("listeners", [](const Matchers::StringMatcher&) {
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

TEST_P(AdminInstanceTest, ConfigDumpFiltersByNameRegex) {
  Buffer::OwnedImpl response;
  Http::TestResponseHeaderMapImpl header_map;
  auto listeners =
      admin_.getConfigTracker().add("listeners", [](const Matchers::StringMatcher& name_matcher) {
        auto msg = std::make_unique<envoy::admin::v3::ListenersConfigDump>();
        if (name_matcher.match("bar")) {
          auto dyn_listener = msg->add_dynamic_listeners();
          dyn_listener->set_name("bar");
        }
        if (name_matcher.match("foo")) {
          auto dyn_listener = msg->add_dynamic_listeners();
          dyn_listener->set_name("foo");
        }
        return msg;
      });
  const std::string expected_json = R"EOF({
 "configs": [
  {
   "@type": "type.googleapis.com/envoy.admin.v3.ListenersConfigDump",
   "dynamic_listeners": [
    {
     "name": "bar"
    }
   ]
  }
 ]
}
)EOF";
  EXPECT_EQ(Http::Code::OK, getCallback("/config_dump?name_regex=.*a.*", header_map, response));
  std::string output = response.toString();
  EXPECT_EQ(expected_json, output);
}

TEST_P(AdminInstanceTest, InvalidRegexIsBadRequest) {
  Buffer::OwnedImpl response;
  Http::TestResponseHeaderMapImpl header_map;
  EXPECT_EQ(Http::Code::BadRequest, getCallback("/config_dump?name_regex=[", header_map, response));
  std::string output = response.toString();
  EXPECT_THAT(output, testing::HasSubstr("Error while parsing name_regex"));
}

ProtobufTypes::MessagePtr testDumpClustersConfig(const Matchers::StringMatcher&) {
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

// Test that BadRequest is returned if there is no intersection between the fields
// of the config dump and the fields present in the mask query parameter.
TEST_P(AdminInstanceTest, ConfigDumpNonExistentMask) {
  Buffer::OwnedImpl response;
  Http::TestResponseHeaderMapImpl header_map;
  auto clusters = admin_.getConfigTracker().add("clusters", testDumpClustersConfig);
  EXPECT_EQ(Http::Code::BadRequest,
            getCallback("/config_dump?resource=static_clusters&mask=bad", header_map, response));
  std::string output = response.toString();
  EXPECT_THAT(output, HasSubstr("could not be successfully used"));
}

// Test that a 404 Not found is returned if a non-existent resource is passed in as the
// resource query parameter.
TEST_P(AdminInstanceTest, ConfigDumpNonExistentResource) {
  Buffer::OwnedImpl response;
  Http::TestResponseHeaderMapImpl header_map;
  auto listeners = admin_.getConfigTracker().add("listeners", [](const Matchers::StringMatcher&) {
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
  auto clusters = admin_.getConfigTracker().add("clusters", [](const Matchers::StringMatcher&) {
    auto msg = std::make_unique<envoy::admin::v3::ClustersConfigDump>();
    msg->set_version_info("foo");
    return msg;
  });
  EXPECT_EQ(Http::Code::BadRequest,
            getCallback("/config_dump?resource=version_info", header_map, response));
}

TEST_P(AdminInstanceTest, InvalidFieldMaskWithResourceDoesNotCrash) {
  Buffer::OwnedImpl response;
  Http::TestResponseHeaderMapImpl header_map;
  auto clusters = admin_.getConfigTracker().add("clusters", [](const Matchers::StringMatcher&) {
    auto msg = std::make_unique<envoy::admin::v3::ClustersConfigDump>();
    auto* static_cluster = msg->add_static_clusters();
    envoy::config::cluster::v3::Cluster inner_cluster;
    inner_cluster.add_transport_socket_matches()->set_name("match1");
    inner_cluster.add_transport_socket_matches()->set_name("match2");
    static_cluster->mutable_cluster()->PackFrom(inner_cluster);
    return msg;
  });

  // `transport_socket_matches` is a repeated field, and cannot be indexed through in a FieldMask.
  EXPECT_EQ(Http::Code::BadRequest,
            getCallback(
                "/config_dump?resource=static_clusters&mask=cluster.transport_socket_matches.name",
                header_map, response));
  std::string expected_mask_text = R"pb(paths: "cluster.transport_socket_matches.name")pb";
  Protobuf::FieldMask expected_mask_proto;
  Protobuf::TextFormat::ParseFromString(expected_mask_text, &expected_mask_proto);
  EXPECT_EQ(fmt::format("FieldMask {} could not be successfully used.",
                        expected_mask_proto.DebugString()),
            response.toString());
  EXPECT_EQ(header_map.ContentType()->value().getStringView(),
            Http::Headers::get().ContentTypeValues.Text);
  EXPECT_EQ(header_map.get(Http::Headers::get().XContentTypeOptions)[0]->value(),
            Http::Headers::get().XContentTypeOptionValues.Nosniff);
}

TEST_P(AdminInstanceTest, InvalidFieldMaskWithoutResourceDoesNotCrash) {
  Buffer::OwnedImpl response;
  Http::TestResponseHeaderMapImpl header_map;
  auto bootstrap = admin_.getConfigTracker().add("bootstrap", [](const Matchers::StringMatcher&) {
    auto msg = std::make_unique<envoy::admin::v3::BootstrapConfigDump>();
    auto* bootstrap = msg->mutable_bootstrap();
    bootstrap->mutable_node()->add_extensions()->set_name("ext1");
    bootstrap->mutable_node()->add_extensions()->set_name("ext2");
    return msg;
  });

  // `extensions` is a repeated field, and cannot be indexed through in a FieldMask.
  EXPECT_EQ(Http::Code::BadRequest,
            getCallback("/config_dump?mask=bootstrap.node.extensions.name", header_map, response));
  EXPECT_THAT(response.toString(), HasSubstr("could not be successfully applied to any configs"));
}

TEST_P(AdminInstanceTest, FieldMasksWorkWhenFetchingAllResources) {
  Buffer::OwnedImpl response;
  Http::TestResponseHeaderMapImpl header_map;
  auto bootstrap = admin_.getConfigTracker().add("bootstrap", [](const Matchers::StringMatcher&) {
    auto msg = std::make_unique<envoy::admin::v3::BootstrapConfigDump>();
    auto* bootstrap = msg->mutable_bootstrap();
    bootstrap->mutable_node()->add_extensions()->set_name("ext1");
    bootstrap->mutable_node()->add_extensions()->set_name("ext2");
    return msg;
  });
  auto clusters = admin_.getConfigTracker().add("clusters", [](const Matchers::StringMatcher&) {
    auto msg = std::make_unique<envoy::admin::v3::ClustersConfigDump>();
    auto* static_cluster = msg->add_static_clusters();
    envoy::config::cluster::v3::Cluster inner_cluster;
    inner_cluster.set_name("cluster1");
    static_cluster->mutable_cluster()->PackFrom(inner_cluster);
    return msg;
  });
  EXPECT_EQ(Http::Code::OK, getCallback("/config_dump?mask=bootstrap", header_map, response));
  EXPECT_EQ(R"EOF({
 "configs": [
  {
   "@type": "type.googleapis.com/envoy.admin.v3.BootstrapConfigDump",
   "bootstrap": {
    "node": {
     "extensions": [
      {
       "name": "ext1"
      },
      {
       "name": "ext2"
      }
     ]
    }
   }
  }
 ]
}
)EOF",
            response.toString());
}

ProtobufTypes::MessagePtr testDumpEcdsConfig(const Matchers::StringMatcher&) {
  auto msg = std::make_unique<envoy::admin::v3::EcdsConfigDump>();

  auto* ecds_listener = msg->mutable_ecds_filters()->Add();
  ecds_listener->set_version_info("1");
  ecds_listener->mutable_last_updated()->set_seconds(5);
  envoy::config::core::v3::TypedExtensionConfig listener_filter_config;
  listener_filter_config.set_name("foo");
  auto listener_config = test::integration::filters::TestTcpListenerFilterConfig();
  listener_config.set_drain_bytes(5);
  listener_filter_config.mutable_typed_config()->PackFrom(listener_config);
  ecds_listener->mutable_ecds_filter()->PackFrom(listener_filter_config);

  auto* ecds_network = msg->mutable_ecds_filters()->Add();
  ecds_network->set_version_info("1");
  ecds_network->mutable_last_updated()->set_seconds(5);
  envoy::config::core::v3::TypedExtensionConfig network_filter_config;
  network_filter_config.set_name("bar");
  auto network_config = test::integration::filters::TestDrainerNetworkFilterConfig();
  network_config.set_bytes_to_drain(5);
  network_filter_config.mutable_typed_config()->PackFrom(network_config);
  ecds_network->mutable_ecds_filter()->PackFrom(network_filter_config);

  return msg;
}

TEST_P(AdminInstanceTest, ConfigDumpEcds) {
  Buffer::OwnedImpl response;
  Http::TestResponseHeaderMapImpl header_map;
  auto ecds_config = admin_.getConfigTracker().add("ecds", testDumpEcdsConfig);
  const std::string expected_json = R"EOF({
 "configs": [
  {
   "@type": "type.googleapis.com/envoy.admin.v3.EcdsConfigDump.EcdsFilterConfig",
   "version_info": "1",
   "ecds_filter": {
    "@type": "type.googleapis.com/envoy.config.core.v3.TypedExtensionConfig",
    "name": "foo",
    "typed_config": {
     "@type": "type.googleapis.com/test.integration.filters.TestTcpListenerFilterConfig",
     "drain_bytes": 5
    }
   },
   "last_updated": "1970-01-01T00:00:05Z"
  },
  {
   "@type": "type.googleapis.com/envoy.admin.v3.EcdsConfigDump.EcdsFilterConfig",
   "version_info": "1",
   "ecds_filter": {
    "@type": "type.googleapis.com/envoy.config.core.v3.TypedExtensionConfig",
    "name": "bar",
    "typed_config": {
     "@type": "type.googleapis.com/test.integration.filters.TestDrainerNetworkFilterConfig",
     "bytes_to_drain": 5
    }
   },
   "last_updated": "1970-01-01T00:00:05Z"
  }
 ]
}
)EOF";
  EXPECT_EQ(Http::Code::OK,
            getCallback("/config_dump?resource=ecds_filters", header_map, response));
  std::string output = response.toString();
  EXPECT_EQ(expected_json, output);
}

TEST_P(AdminInstanceTest, ConfigDumpEcdsByResourceAndMask) {
  Buffer::OwnedImpl response;
  Http::TestResponseHeaderMapImpl header_map;
  auto ecds_config = admin_.getConfigTracker().add("ecds", testDumpEcdsConfig);
  const std::string expected_json = R"EOF({
 "configs": [
  {
   "@type": "type.googleapis.com/envoy.admin.v3.EcdsConfigDump.EcdsFilterConfig",
   "version_info": "1",
   "ecds_filter": {
    "@type": "type.googleapis.com/envoy.config.core.v3.TypedExtensionConfig",
    "name": "foo"
   }
  },
  {
   "@type": "type.googleapis.com/envoy.admin.v3.EcdsConfigDump.EcdsFilterConfig",
   "version_info": "1",
   "ecds_filter": {
    "@type": "type.googleapis.com/envoy.config.core.v3.TypedExtensionConfig",
    "name": "bar"
   }
  }
 ]
}
)EOF";
  EXPECT_EQ(Http::Code::OK, getCallback("/config_dump?resource=ecds_filters&mask="
                                        "ecds_filter.name,version_info",
                                        header_map, response));
  std::string output = response.toString();
  EXPECT_EQ(expected_json, output);
}

} // namespace Server
} // namespace Envoy
