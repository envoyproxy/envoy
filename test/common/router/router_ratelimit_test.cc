#include <memory>
#include <string>
#include <vector>

#include "common/config/rds_json.h"
#include "common/http/header_map_impl.h"
#include "common/json/json_loader.h"
#include "common/network/address_impl.h"
#include "common/router/config_impl.h"
#include "common/router/router_ratelimit.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/ratelimit/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::ReturnRef;
using testing::_;

namespace Envoy {
namespace Router {
namespace {

envoy::api::v2::route::RateLimit parseRateLimitFromJson(const std::string& json_string) {
  envoy::api::v2::route::RateLimit rate_limit;
  auto json_object_ptr = Json::Factory::loadFromString(json_string);
  Envoy::Config::RdsJson::translateRateLimit(*json_object_ptr, rate_limit);
  return rate_limit;
}

TEST(BadRateLimitConfiguration, MissingActions) {
  EXPECT_THROW(parseRateLimitFromJson("{}"), EnvoyException);
}

TEST(BadRateLimitConfiguration, BadType) {
  std::string json = R"EOF(
  {
    "actions":[
      {
        "type": "bad_type"
      }
    ]
  }
  )EOF";

  EXPECT_THROW(RateLimitPolicyEntryImpl(parseRateLimitFromJson(json)), EnvoyException);
}

TEST(BadRateLimitConfiguration, ActionsMissingRequiredFields) {
  std::string json_one = R"EOF(
  {
    "actions":[
      {
        "type": "request_headers"
      }
    ]
  }
  )EOF";

  EXPECT_THROW(RateLimitPolicyEntryImpl(parseRateLimitFromJson(json_one)), EnvoyException);

  std::string json_two = R"EOF(
  {
    "actions":[
      {
        "type": "request_headers",
        "header_name" : "test"
      }
    ]
  }
  )EOF";

  EXPECT_THROW(RateLimitPolicyEntryImpl(parseRateLimitFromJson(json_two)), EnvoyException);

  std::string json_three = R"EOF(
  {
    "actions":[
      {
        "type": "request_headers",
        "descriptor_key" : "test"
      }
    ]
  }
  )EOF";

  EXPECT_THROW(RateLimitPolicyEntryImpl(parseRateLimitFromJson(json_three)), EnvoyException);
}

static Http::TestHeaderMapImpl genHeaders(const std::string& host, const std::string& path,
                                          const std::string& method) {
  return Http::TestHeaderMapImpl{{":authority", host}, {":path", path}, {":method", method}};
}

class RateLimitConfiguration : public testing::Test {
public:
  void SetUpTest(const std::string json) {
    envoy::api::v2::RouteConfiguration route_config;
    auto json_object_ptr = Json::Factory::loadFromString(json);
    Envoy::Config::RdsJson::translateRouteConfiguration(*json_object_ptr, route_config);
    config_.reset(new ConfigImpl(route_config, factory_context_, true));
  }

  std::unique_ptr<ConfigImpl> config_;
  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  Http::TestHeaderMapImpl header_;
  const RouteEntry* route_;
  Network::Address::Ipv4Instance default_remote_address_{"10.0.0.1"};
};

TEST_F(RateLimitConfiguration, NoApplicableRateLimit) {
  std::string json = R"EOF(
  {
    "virtual_hosts": [
      {
        "name": "www2",
        "domains": ["www.lyft.com"],
        "routes": [
          {
            "prefix": "/foo",
            "cluster": "www2",
            "rate_limits": [
              {
                "actions":[
                  {
                    "type": "remote_address"
                  }
                ]
              }
            ]
          },
          {
            "prefix": "/bar",
            "cluster": "www2"
          }
        ]
      }
    ]
  }
  )EOF";

  SetUpTest(json);

  EXPECT_EQ(0U, config_->route(genHeaders("www.lyft.com", "/bar", "GET"), 0)
                    ->routeEntry()
                    ->rateLimitPolicy()
                    .getApplicableRateLimit(0)
                    .size());
}

TEST_F(RateLimitConfiguration, NoRateLimitPolicy) {
  std::string json = R"EOF(
  {
    "virtual_hosts": [
      {
        "name": "www2",
        "domains": ["www.lyft.com"],
        "routes": [
          {
            "prefix": "/",
            "cluster": "www2"
          }
        ]
      }
    ]
  }
  )EOF";

  SetUpTest(json);

  route_ = config_->route(genHeaders("www.lyft.com", "/bar", "GET"), 0)->routeEntry();
  EXPECT_EQ(0U, route_->rateLimitPolicy().getApplicableRateLimit(0).size());
  EXPECT_TRUE(route_->rateLimitPolicy().empty());
}

TEST_F(RateLimitConfiguration, TestGetApplicationRateLimit) {
  std::string json = R"EOF(
  {
    "virtual_hosts": [
      {
        "name": "www2",
        "domains": ["www.lyft.com"],
        "routes": [
          {
            "prefix": "/foo",
            "cluster": "www2",
            "rate_limits": [
              {
                "actions":[
                  {
                    "type": "remote_address"
                  }
                ]
              }
            ]
          }
        ]
      }
    ]
  }
  )EOF";

  SetUpTest(json);

  route_ = config_->route(genHeaders("www.lyft.com", "/foo", "GET"), 0)->routeEntry();
  EXPECT_FALSE(route_->rateLimitPolicy().empty());
  std::vector<std::reference_wrapper<const RateLimitPolicyEntry>> rate_limits =
      route_->rateLimitPolicy().getApplicableRateLimit(0);
  EXPECT_EQ(1U, rate_limits.size());

  std::vector<Envoy::RateLimit::Descriptor> descriptors;
  for (const RateLimitPolicyEntry& rate_limit : rate_limits) {
    rate_limit.populateDescriptors(*route_, descriptors, "", header_, default_remote_address_);
  }
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"remote_address", "10.0.0.1"}}}}),
              testing::ContainerEq(descriptors));
}

TEST_F(RateLimitConfiguration, TestVirtualHost) {
  std::string json = R"EOF(
  {
    "virtual_hosts": [
      {
        "name": "www2",
        "domains": ["www.lyft.com"],
        "routes": [
          {
            "prefix": "/",
            "cluster": "www2test"
          }
        ],
        "rate_limits": [
          {
            "actions": [
              {
                "type": "destination_cluster"
              }
            ]
          }
        ]
      }
    ]
  }
  )EOF";

  SetUpTest(json);

  route_ = config_->route(genHeaders("www.lyft.com", "/bar", "GET"), 0)->routeEntry();
  std::vector<std::reference_wrapper<const RateLimitPolicyEntry>> rate_limits =
      route_->virtualHost().rateLimitPolicy().getApplicableRateLimit(0);
  EXPECT_EQ(1U, rate_limits.size());

  std::vector<Envoy::RateLimit::Descriptor> descriptors;
  for (const RateLimitPolicyEntry& rate_limit : rate_limits) {
    rate_limit.populateDescriptors(*route_, descriptors, "service_cluster", header_,
                                   default_remote_address_);
  }
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"destination_cluster", "www2test"}}}}),
              testing::ContainerEq(descriptors));
}

TEST_F(RateLimitConfiguration, Stages) {
  std::string json = R"EOF(
  {
    "virtual_hosts": [
      {
        "name": "www2",
        "domains": ["www.lyft.com"],
        "routes": [
          {
            "prefix": "/foo",
            "cluster": "www2test",
            "rate_limits": [
              {
                "stage": 1,
                "actions": [
                  {
                    "type": "remote_address"
                  }
                ]
              },
              {
                "actions" : [
                  {
                    "type" : "destination_cluster"
                  }
                ]
              },
              {
                "actions": [
                  {
                    "type" : "destination_cluster"
                  },
                  {
                    "type": "source_cluster"
                  }
                ]
              }
            ]
          }
        ]
      }
    ]
  }
  )EOF";

  SetUpTest(json);

  route_ = config_->route(genHeaders("www.lyft.com", "/foo", "GET"), 0)->routeEntry();
  std::vector<std::reference_wrapper<const RateLimitPolicyEntry>> rate_limits =
      route_->rateLimitPolicy().getApplicableRateLimit(0);
  EXPECT_EQ(2U, rate_limits.size());

  std::vector<Envoy::RateLimit::Descriptor> descriptors;
  for (const RateLimitPolicyEntry& rate_limit : rate_limits) {
    rate_limit.populateDescriptors(*route_, descriptors, "service_cluster", header_,
                                   default_remote_address_);
  }
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>(
                  {{{{"destination_cluster", "www2test"}}},
                   {{{"destination_cluster", "www2test"}, {"source_cluster", "service_cluster"}}}}),
              testing::ContainerEq(descriptors));

  descriptors.clear();
  rate_limits = route_->rateLimitPolicy().getApplicableRateLimit(1UL);
  EXPECT_EQ(1U, rate_limits.size());

  for (const RateLimitPolicyEntry& rate_limit : rate_limits) {
    rate_limit.populateDescriptors(*route_, descriptors, "service_cluster", header_,
                                   default_remote_address_);
  }
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"remote_address", "10.0.0.1"}}}}),
              testing::ContainerEq(descriptors));

  rate_limits = route_->rateLimitPolicy().getApplicableRateLimit(10UL);
  EXPECT_TRUE(rate_limits.empty());
}

class RateLimitPolicyEntryTest : public testing::Test {
public:
  void SetUpTest(const std::string json) {
    rate_limit_entry_.reset(new RateLimitPolicyEntryImpl(parseRateLimitFromJson(json)));
    descriptors_.clear();
  }

  std::unique_ptr<RateLimitPolicyEntryImpl> rate_limit_entry_;
  Http::TestHeaderMapImpl header_;
  NiceMock<MockRouteEntry> route_;
  std::vector<Envoy::RateLimit::Descriptor> descriptors_;
  Network::Address::Ipv4Instance default_remote_address_{"10.0.0.1"};
};

TEST_F(RateLimitPolicyEntryTest, RateLimitPolicyEntryMembers) {
  std::string json = R"EOF(
  {
    "stage": 2,
    "disable_key": "no_ratelimit",
    "actions": [
      {
        "type": "remote_address"
      }
    ]
  }
  )EOF";

  SetUpTest(json);

  EXPECT_EQ(2UL, rate_limit_entry_->stage());
  EXPECT_EQ("no_ratelimit", rate_limit_entry_->disableKey());
}

TEST_F(RateLimitPolicyEntryTest, RemoteAddress) {
  std::string json = R"EOF(
  {
    "actions": [
      {
        "type": "remote_address"
      }
    ]
  }
  )EOF";

  SetUpTest(json);

  rate_limit_entry_->populateDescriptors(route_, descriptors_, "", header_,
                                         default_remote_address_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"remote_address", "10.0.0.1"}}}}),
              testing::ContainerEq(descriptors_));
}

// Verify no descriptor is emitted if remote is a pipe.
TEST_F(RateLimitPolicyEntryTest, PipeAddress) {
  std::string json = R"EOF(
  {
    "actions": [
      {
        "type": "remote_address"
      }
    ]
  }
  )EOF";

  SetUpTest(json);

  Network::Address::PipeInstance pipe_address("/hello");
  rate_limit_entry_->populateDescriptors(route_, descriptors_, "", header_, pipe_address);
  EXPECT_TRUE(descriptors_.empty());
}

TEST_F(RateLimitPolicyEntryTest, SourceService) {
  std::string json = R"EOF(
  {
    "actions": [
      {
        "type": "source_cluster"
      }
    ]
  }
  )EOF";

  SetUpTest(json);

  rate_limit_entry_->populateDescriptors(route_, descriptors_, "service_cluster", header_,
                                         default_remote_address_);
  EXPECT_THAT(
      std::vector<Envoy::RateLimit::Descriptor>({{{{"source_cluster", "service_cluster"}}}}),
      testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, DestinationService) {
  std::string json = R"EOF(
  {
    "actions": [
      {
        "type": "destination_cluster"
      }
    ]
  }
  )EOF";

  SetUpTest(json);

  rate_limit_entry_->populateDescriptors(route_, descriptors_, "service_cluster", header_,
                                         default_remote_address_);
  EXPECT_THAT(
      std::vector<Envoy::RateLimit::Descriptor>({{{{"destination_cluster", "fake_cluster"}}}}),
      testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, RequestHeaders) {
  std::string json = R"EOF(
  {
    "actions": [
      {
        "type": "request_headers",
        "header_name": "x-header-name",
        "descriptor_key": "my_header_name"
      }
    ]
  }
  )EOF";

  SetUpTest(json);
  Http::TestHeaderMapImpl header{{"x-header-name", "test_value"}};

  rate_limit_entry_->populateDescriptors(route_, descriptors_, "service_cluster", header,
                                         default_remote_address_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"my_header_name", "test_value"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, RequestHeadersNoMatch) {
  std::string json = R"EOF(
  {
    "actions": [
      {
        "type": "request_headers",
        "header_name": "x-header",
        "descriptor_key": "my_header_name"
      }
    ]
  }
  )EOF";

  SetUpTest(json);
  Http::TestHeaderMapImpl header{{"x-header-name", "test_value"}};

  rate_limit_entry_->populateDescriptors(route_, descriptors_, "service_cluster", header,
                                         default_remote_address_);
  EXPECT_TRUE(descriptors_.empty());
}

TEST_F(RateLimitPolicyEntryTest, RateLimitKey) {
  std::string json = R"EOF(
  {
    "actions": [
      {
        "type": "generic_key",
        "descriptor_value": "fake_key"
      }
    ]
  }
  )EOF";

  SetUpTest(json);

  rate_limit_entry_->populateDescriptors(route_, descriptors_, "", header_,
                                         default_remote_address_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"generic_key", "fake_key"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, HeaderValueMatch) {
  std::string json = R"EOF(
  {
    "actions": [
      {
        "type": "header_value_match",
        "descriptor_value": "fake_value",
        "headers": [
          {
            "name": "x-header-name",
            "value": "test_value",
            "regex": false
          }
        ]
      }
    ]
  }
  )EOF";

  SetUpTest(json);
  Http::TestHeaderMapImpl header{{"x-header-name", "test_value"}};

  rate_limit_entry_->populateDescriptors(route_, descriptors_, "", header, default_remote_address_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"header_match", "fake_value"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, HeaderValueMatchNoMatch) {
  std::string json = R"EOF(
  {
    "actions": [
      {
        "type": "header_value_match",
        "descriptor_value": "fake_value",
        "headers": [
          {
            "name": "x-header-name",
            "value": "test_value",
            "regex": false
          }
        ]
      }
    ]
  }
  )EOF";

  SetUpTest(json);
  Http::TestHeaderMapImpl header{{"x-header-name", "not_same_value"}};

  rate_limit_entry_->populateDescriptors(route_, descriptors_, "", header, default_remote_address_);
  EXPECT_TRUE(descriptors_.empty());
}

TEST_F(RateLimitPolicyEntryTest, HeaderValueMatchHeadersNotPresent) {
  std::string json = R"EOF(
  {
    "actions": [
      {
        "type": "header_value_match",
        "descriptor_value": "fake_value",
        "expect_match": false,
        "headers": [
          {
            "name": "x-header-name",
            "value": "test_value",
            "regex": false
          }
        ]
      }
    ]
  }
  )EOF";

  SetUpTest(json);
  Http::TestHeaderMapImpl header{{"x-header-name", "not_same_value"}};

  rate_limit_entry_->populateDescriptors(route_, descriptors_, "", header, default_remote_address_);
  EXPECT_THAT(std::vector<Envoy::RateLimit::Descriptor>({{{{"header_match", "fake_value"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, HeaderValueMatchHeadersPresent) {
  std::string json = R"EOF(
  {
    "actions": [
      {
        "type": "header_value_match",
        "descriptor_value": "fake_value",
        "expect_match": false,
        "headers": [
          {
            "name": "x-header-name",
            "value": "test_value",
            "regex": false
          }
        ]
      }
    ]
  }
  )EOF";

  SetUpTest(json);
  Http::TestHeaderMapImpl header{{"x-header-name", "test_value"}};

  rate_limit_entry_->populateDescriptors(route_, descriptors_, "", header, default_remote_address_);
  EXPECT_TRUE(descriptors_.empty());
}

TEST_F(RateLimitPolicyEntryTest, CompoundActions) {
  std::string json = R"EOF(
  {
    "actions": [
      {
        "type": "destination_cluster"
      },
      {
        "type": "source_cluster"
      }
    ]
  }
  )EOF";

  SetUpTest(json);

  rate_limit_entry_->populateDescriptors(route_, descriptors_, "service_cluster", header_,
                                         default_remote_address_);
  EXPECT_THAT(
      std::vector<Envoy::RateLimit::Descriptor>(
          {{{{"destination_cluster", "fake_cluster"}, {"source_cluster", "service_cluster"}}}}),
      testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, CompoundActionsNoDescriptor) {
  std::string json = R"EOF(
  {
    "actions": [
      {
        "type": "destination_cluster"
      },
      {
        "type": "header_value_match",
        "descriptor_value": "fake_value",
        "headers": [
          {
            "name": "x-header-name",
            "value": "test_value",
            "regex": false
          }
        ]
      }
    ]
  }
  )EOF";

  SetUpTest(json);

  rate_limit_entry_->populateDescriptors(route_, descriptors_, "service_cluster", header_,
                                         default_remote_address_);
  EXPECT_TRUE(descriptors_.empty());
}

} // namespace
} // namespace Router
} // namespace Envoy
