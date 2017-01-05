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
  "rate_limits": [{}]
}
)EOF";

  EXPECT_THROW(RateLimitPolicyImpl(*Json::Factory::LoadFromString(json)), EnvoyException);
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

  EXPECT_THROW(RateLimitPolicyEntryImpl(*Json::Factory::LoadFromString(json)), EnvoyException);
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

  EXPECT_THROW(RateLimitPolicyEntryImpl(*Json::Factory::LoadFromString(json_one)), EnvoyException);

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

  EXPECT_THROW(RateLimitPolicyEntryImpl(*Json::Factory::LoadFromString(json_two)), EnvoyException);

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

  EXPECT_THROW(RateLimitPolicyEntryImpl(*Json::Factory::LoadFromString(json_three)),
               EnvoyException);
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
  Http::TestHeaderMapImpl header_;
  const RouteEntry* route_;
};

TEST_F(RateLimitConfiguration, NoRateLimit) {
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

  EXPECT_EQ(0U, config_->routeForRequest(genHeaders("www.lyft.com", "/bar", "GET"), 0)
                    ->rateLimitPolicy()
                    .getApplicableRateLimit(0)
                    .size());
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
  std::string address = "10.0.0.1";

  route_ = config_->routeForRequest(genHeaders("www.lyft.com", "/foo", "GET"), 0);
  std::vector<std::reference_wrapper<const RateLimitPolicyEntry>> rate_limits =
      route_->rateLimitPolicy().getApplicableRateLimit(0);
  EXPECT_EQ(1U, rate_limits.size());

  std::vector<::RateLimit::Descriptor> descriptors;
  for (const RateLimitPolicyEntry& rate_limit : rate_limits) {
    rate_limit.populateDescriptors(*route_, descriptors, "", header_, address);
  }
  EXPECT_THAT(std::vector<::RateLimit::Descriptor>({{{{"remote_address", address}}}}),
              testing::ContainerEq(descriptors));
}

class RateLimitPolicyEntryTest : public testing::Test {
public:
  void SetUpTest(const std::string json) {
    Json::ObjectPtr loader = Json::Factory::LoadFromString(json);
    rate_limit_entry_.reset(new RateLimitPolicyEntryImpl(*loader));
    descriptors_.clear();
  }

  std::unique_ptr<RateLimitPolicyEntryImpl> rate_limit_entry_;
  Http::TestHeaderMapImpl header_;
  NiceMock<MockRouteEntry> route_;
  std::vector<::RateLimit::Descriptor> descriptors_;
};

TEST_F(RateLimitPolicyEntryTest, RateLimitPolicyEntryMembers) {
  std::string json = R"EOF(
{
  "stage": 2,
  "kill_switch_key": "no_ratelimit",
  "route_key": "my_route",
  "actions":[
    {
      "type": "remote_address"
    }
  ]
}
)EOF";

  SetUpTest(json);

  EXPECT_EQ(2, rate_limit_entry_->stage());
  EXPECT_EQ("no_ratelimit", rate_limit_entry_->killSwitchKey());
  EXPECT_EQ("my_route", rate_limit_entry_->routeKey());
}

TEST_F(RateLimitPolicyEntryTest, RemoteAddress) {
  std::string json = R"EOF(
{
  "actions":[
    {
      "type": "remote_address"
    }
  ]
}
)EOF";

  SetUpTest(json);
  std::string address = "10.0.0.1";

  rate_limit_entry_->populateDescriptors(route_, descriptors_, "", header_, address);
  EXPECT_THAT(std::vector<::RateLimit::Descriptor>({{{{"remote_address", address}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, RemoteAddressRouteKey) {
  std::string json = R"EOF(
{
  "route_key": "my_route",
  "actions":[
    {
      "type": "remote_address"
    }
  ]
}
)EOF";

  SetUpTest(json);
  std::string address = "10.0.1";

  rate_limit_entry_->populateDescriptors(route_, descriptors_, "", header_, address);
  EXPECT_THAT(std::vector<::RateLimit::Descriptor>(
                  {{{{"remote_address", address}}},
                   {{{"route_key", "my_route"}, {"remote_address", address}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, NoAddress) {
  std::string json = R"EOF(
{
  "actions":[
    {
      "type": "remote_address"
    }
  ]
}
)EOF";

  SetUpTest(json);

  rate_limit_entry_->populateDescriptors(route_, descriptors_, "", header_, "");
  EXPECT_TRUE(descriptors_.empty());
}

TEST_F(RateLimitPolicyEntryTest, ServiceToService) {
  std::string json = R"EOF(
{
  "actions":[
    {
      "type": "service_to_service"
    }
  ]
}
)EOF";

  SetUpTest(json);

  rate_limit_entry_->populateDescriptors(route_, descriptors_, "service_cluster", header_, "");
  EXPECT_THAT(std::vector<::RateLimit::Descriptor>(
                  {{{{"to_cluster", "fake_cluster"}}},
                   {{{"to_cluster", "fake_cluster"}, {"from_cluster", "service_cluster"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, RequestHeaders) {
  std::string json = R"EOF(
{
  "actions": [
    {
      "type": "request_headers",
      "header_name": "x-header-name",
      "descriptor_key" : "my_header_name"
    }
  ]
}
)EOF";

  SetUpTest(json);
  Http::TestHeaderMapImpl header{{"x-header-name", "test_value"}};

  rate_limit_entry_->populateDescriptors(route_, descriptors_, "service_cluster", header, "");
  EXPECT_THAT(std::vector<::RateLimit::Descriptor>({{{{"my_header_name", "test_value"}}}}),
              testing::ContainerEq(descriptors_));
}
TEST_F(RateLimitPolicyEntryTest, RequestHeadersRouteKey) {
  std::string json = R"EOF(
{
  "route_key": "my_route",
  "actions": [
    {
      "type": "request_headers",
      "header_name": "x-header-name",
      "descriptor_key" : "my_header_name"
    }
  ]
}
)EOF";

  SetUpTest(json);
  Http::TestHeaderMapImpl header{{"x-header-name", "test_value"}};

  rate_limit_entry_->populateDescriptors(route_, descriptors_, "service_cluster", header, "");
  EXPECT_THAT(std::vector<::RateLimit::Descriptor>(
                  {{{{"my_header_name", "test_value"}}},
                   {{{"route_key", "my_route"}, {"my_header_name", "test_value"}}}}),
              testing::ContainerEq(descriptors_));
}

TEST_F(RateLimitPolicyEntryTest, RequestHeadersNoMatch) {
  std::string json = R"EOF(
{
  "actions": [
    {
      "type": "request_headers",
      "header_name": "x-header",
      "descriptor_key" : "my_header_name"
    }
  ]
}
)EOF";

  SetUpTest(json);
  Http::TestHeaderMapImpl header{{"x-header-name", "test_value"}};

  rate_limit_entry_->populateDescriptors(route_, descriptors_, "service_cluster", header, "");
  EXPECT_TRUE(descriptors_.empty());
}

} // Router