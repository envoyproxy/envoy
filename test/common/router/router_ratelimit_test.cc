#include "common/http/header_map_impl.h"
#include "common/json/json_loader.h"
#include "common/router/config_impl.h"
#include "common/router/router_ratelimit.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/ratelimit/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"

using testing::_;
using testing::NiceMock;
using testing::ReturnRef;

namespace Router {

TEST(BadRateLimitConfiguration, MissingActions) {
  std::string json = R"EOF(
{
"virtual_hosts": [
{
  "name": "www2",
  "domains": ["www.lyft.com"],
  "routes": [
    {
      "prefix": "/",
      "cluster": "www2",
      "rate_limit": {
        "global": true
      },
      "rate_limits": [
      { }
      ]
    }
  ]
}
]
}
)EOF";

  Json::ObjectPtr loader = Json::Factory::LoadFromString(json);
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  EXPECT_THROW(ConfigImpl(*loader, runtime, cm), EnvoyException);
}

TEST(BadRateLimitConfiguration, BadType) {
  std::string json = R"EOF(
{
"virtual_hosts": [
  {
    "name": "www2",
    "domains": ["www.lyft.com"],
    "routes": [
      {
        "prefix": "/",
        "cluster": "www2",
        "rate_limits": [
        { "actions":[ {"type": "bad_type"}] }
        ]
      }
    ]
  }
]
}
)EOF";

  Json::ObjectPtr loader = Json::Factory::LoadFromString(json);
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  EXPECT_THROW(ConfigImpl(*loader, runtime, cm), EnvoyException);
}

TEST(BadRateLimitConfiguration, ActionsMissingRequiredFields) {
  std::string json_one = R"EOF(
{
"virtual_hosts": [
  {
    "name": "www2",
    "domains": ["www.lyft.com"],
    "routes": [
      {
        "prefix": "/",
        "cluster": "www2",
        "rate_limits": [
        { "actions":[ {"type": "request_headers"}] }
        ]
      }
    ]
  }
]
}
)EOF";

  Json::ObjectPtr loader = Json::Factory::LoadFromString(json_one);
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  EXPECT_THROW(ConfigImpl(*loader, runtime, cm), EnvoyException);

  std::string json_two = R"EOF(
{
"virtual_hosts": [
  {
    "name": "www2",
    "domains": ["www.lyft.com"],
    "routes": [
      {
        "prefix": "/",
        "cluster": "www2",
        "rate_limits": [
        { "actions":[
            {"type": "request_headers",
              "header_name" : "test"}]
          }
        ]
      }
    ]
  }
]
}
)EOF";
  loader = Json::Factory::LoadFromString(json_two);
  EXPECT_THROW(ConfigImpl(*loader, runtime, cm), EnvoyException);
  std::string json_three = R"EOF(
{
"virtual_hosts": [
{
  "name": "www2",
  "domains": ["www.lyft.com"],
  "routes": [
    {
      "prefix": "/",
      "cluster": "www2",
      "rate_limits": [
      { "actions":[
          {"type": "request_headers",
            "descriptor_key" : "test" }]
        }
      ]
    }
  ]
}
]
}
)EOF";
  loader = Json::Factory::LoadFromString(json_three);
  EXPECT_THROW(ConfigImpl(*loader, runtime, cm), EnvoyException);
}

static Http::TestHeaderMapImpl genHeaders(const std::string& host, const std::string& path,
                                          const std::string& method) {
  return Http::TestHeaderMapImpl{{":authority", host}, {":path", path}, {":method", method}};
}
class RateLimitConfiguration : public testing::Test {
public:
  void SetUpTest(const std::string json) {
    Json::ObjectPtr loader = Json::Factory::LoadFromString(json);
    config_.reset(new ConfigImpl(*loader, runtime_, cm_));
  }
  std::unique_ptr<ConfigImpl> config_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  NiceMock<Router::MockRouteEntry> route_;
  Http::TestHeaderMapImpl header_;
};

TEST_F(RateLimitConfiguration, RemoteAddress) {
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
        "rate_limit": {
          "global": true
        },
        "rate_limits": [
        { "actions":[ {"type": "remote_address"}] }
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
  route_.rate_limit_policy_.route_key_ = "";
  std::string address = "10.0.0.1";
  EXPECT_CALL(callbacks_, downstreamAddress()).WillOnce(ReturnRef(address));

  EXPECT_EQ(0u, config_->routeForRequest(genHeaders("www.lyft.com", "/bar", "GET"), 0)
                    ->rateLimitPolicy()
                    .getApplicableRateLimit(0)
                    .size());
  std::vector<std::reference_wrapper<RateLimitPolicyEntry>> rate_limits =
      config_->routeForRequest(genHeaders("www.lyft.com", "/foo", "GET"), 0)
          ->rateLimitPolicy()
          .getApplicableRateLimit(0);
  EXPECT_EQ(1u, rate_limits.size());
  std::vector<::RateLimit::Descriptor> descriptors;
  for (const RateLimitPolicyEntry& rate_limit : rate_limits) {
    rate_limit.populateDescriptors(route_, descriptors, "", header_, callbacks_);
  }
  EXPECT_THAT(std::vector<::RateLimit::Descriptor>({{{{"remote_address", address}}}}),
              testing::ContainerEq(descriptors));
}
TEST_F(RateLimitConfiguration, NoAddress) {
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
        "rate_limit": {
          "global": true
        },
        "rate_limits": [
        { "actions":[ {"type": "remote_address"}] }
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
  route_.rate_limit_policy_.route_key_ = "";
  std::string address = "10.0.0.1";
  EXPECT_CALL(callbacks_, downstreamAddress()).WillOnce(ReturnRef(EMPTY_STRING));

  EXPECT_EQ(0u, config_->routeForRequest(genHeaders("www.lyft.com", "/bar", "GET"), 0)
                    ->rateLimitPolicy()
                    .getApplicableRateLimit(0)
                    .size());
  std::vector<std::reference_wrapper<RateLimitPolicyEntry>> rate_limits =
      config_->routeForRequest(genHeaders("www.lyft.com", "/foo", "GET"), 0)
          ->rateLimitPolicy()
          .getApplicableRateLimit(0);
  EXPECT_EQ(1u, rate_limits.size());
  std::vector<::RateLimit::Descriptor> descriptors;
  for (const RateLimitPolicyEntry& rate_limit : rate_limits) {
    rate_limit.populateDescriptors(route_, descriptors, "", header_, callbacks_);
  }
  EXPECT_TRUE(descriptors.empty());
}

TEST_F(RateLimitConfiguration, ServiceToService) {
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
        "rate_limit": {
          "global": true
        },
        "rate_limits": [
        { "actions":[ {"type": "service_to_service"}] }
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
  route_.rate_limit_policy_.route_key_ = "my_route";
  std::vector<std::reference_wrapper<RateLimitPolicyEntry>> rate_limits =
      config_->routeForRequest(genHeaders("www.lyft.com", "/foo", "GET"), 0)
          ->rateLimitPolicy()
          .getApplicableRateLimit(0);
  EXPECT_EQ(1u, rate_limits.size());
  std::vector<::RateLimit::Descriptor> descriptors;
  for (const RateLimitPolicyEntry& rate_limit : rate_limits) {
    rate_limit.populateDescriptors(route_, descriptors, "service_cluster", header_, callbacks_);
  }
  std::vector<::RateLimit::Descriptor> expected_descriptors = {
      {{{"to_cluster", "fake_cluster"}}},
      {{{"to_cluster", "fake_cluster"}, {"from_cluster", "service_cluster"}}}};
  EXPECT_THAT(expected_descriptors, testing::ContainerEq(descriptors));
}
TEST_F(RateLimitConfiguration, RequestHeaders) {
  std::string json = R"EOF(
{
"virtual_hosts": [
  {
    "name": "www2",
    "domains": ["www.lyft.com"],
    "routes": [
      {
        "prefix": "/",
        "cluster": "www2",
        "rate_limit": {
          "global": true
        },
        "rate_limits": [
        { "actions": [
          {
            "type": "request_headers",
            "header_name": "x-header-name",
            "descriptor_key" : "my_header_name"
          }
        ]}
        ]
      }
    ]
  }
]
}
)EOF";
  SetUpTest(json);
  Http::TestHeaderMapImpl header{{"x-header-name", "test_value"}};
  route_.rate_limit_policy_.route_key_ = "test_key";
  std::vector<std::reference_wrapper<RateLimitPolicyEntry>> rate_limits =
      config_->routeForRequest(genHeaders("www.lyft.com", "/foo", "GET"), 0)
          ->rateLimitPolicy()
          .getApplicableRateLimit(0);
  EXPECT_EQ(1u, rate_limits.size());
  std::vector<::RateLimit::Descriptor> descriptors;
  for (const RateLimitPolicyEntry& rate_limit : rate_limits) {
    rate_limit.populateDescriptors(route_, descriptors, "service_cluster", header, callbacks_);
  }
  std::vector<::RateLimit::Descriptor> expected_descriptors = {
      {{{"my_header_name", "test_value"}}},
      {{{"route_key", "test_key"}, {"my_header_name", "test_value"}}}};
  EXPECT_THAT(expected_descriptors, testing::ContainerEq(descriptors));
}

TEST_F(RateLimitConfiguration, RequestHeadersNoMatch) {
  std::string json = R"EOF(
{
"virtual_hosts": [
  {
    "name": "www2",
    "domains": ["www.lyft.com"],
    "routes": [
      {
        "prefix": "/",
        "cluster": "www2",
        "rate_limit": {
          "global": true
        },
        "rate_limits": [
        { "actions": [
          {
            "type": "request_headers",
            "header_name": "x-header",
            "descriptor_key" : "my_header_name"
          }
        ]}
        ]
      }
    ]
  }
]
}
)EOF";
  SetUpTest(json);
  Http::TestHeaderMapImpl header{{"x-header-name", "test_value"}};
  route_.rate_limit_policy_.route_key_ = "test_key";
  std::vector<std::reference_wrapper<RateLimitPolicyEntry>> rate_limits =
      config_->routeForRequest(genHeaders("www.lyft.com", "/foo", "GET"), 0)
          ->rateLimitPolicy()
          .getApplicableRateLimit(0);
  EXPECT_EQ(1u, rate_limits.size());
  std::vector<::RateLimit::Descriptor> descriptors;
  for (const RateLimitPolicyEntry& rate_limit : rate_limits) {
    rate_limit.populateDescriptors(route_, descriptors, "service_cluster", header, callbacks_);
  }
  EXPECT_TRUE(descriptors.empty());
}
}