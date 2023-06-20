#include "envoy/extensions/filters/network/dubbo_proxy/v3/dubbo_proxy.pb.h"
#include "envoy/extensions/filters/network/dubbo_proxy/v3/dubbo_proxy.pb.validate.h"
#include "envoy/extensions/filters/network/dubbo_proxy/v3/route.pb.h"
#include "envoy/extensions/filters/network/dubbo_proxy/v3/route.pb.validate.h"

#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/network/dubbo_proxy/message_impl.h"
#include "source/extensions/filters/network/dubbo_proxy/router/route_matcher.h"

#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {
namespace Router {
namespace {

envoy::extensions::filters::network::dubbo_proxy::v3::RouteConfiguration
parseRouteConfigurationFromV2Yaml(const std::string& yaml) {
  envoy::extensions::filters::network::dubbo_proxy::v3::RouteConfiguration route_config;
  TestUtility::loadFromYaml(yaml, route_config);
  TestUtility::validate(route_config);
  return route_config;
}

envoy::extensions::filters::network::dubbo_proxy::v3::DubboProxy
parseDubboProxyFromV2Yaml(const std::string& yaml) {
  envoy::extensions::filters::network::dubbo_proxy::v3::DubboProxy config;
  TestUtility::loadFromYaml(yaml, config);
  TestUtility::validate(config);
  return config;
}

void setDefaultInvocationCallback(RpcInvocationImpl& invo) {
  invo.setParametersLazyCallback([]() -> RpcInvocationImpl::ParametersPtr {
    return std::make_unique<RpcInvocationImpl::Parameters>();
  });

  invo.setAttachmentLazyCallback([]() -> RpcInvocationImpl::AttachmentPtr {
    auto map = std::make_unique<RpcInvocationImpl::Attachment::Map>();
    return std::make_unique<RpcInvocationImpl::Attachment>(std::move(map), 0);
  });
}

} // namespace

TEST(DubboRouteMatcherTest, RouteByServiceNameWithWildcard) {
  {
    const std::string yaml = R"EOF(
name: local_route
interface: "*"
routes:
  - match:
      method:
        name:
          exact: "add"
    route:
        cluster: user_service_dubbo_server
)EOF";
    envoy::extensions::filters::network::dubbo_proxy::v3::RouteConfiguration config =
        parseRouteConfigurationFromV2Yaml(yaml);

    NiceMock<Server::Configuration::MockServerFactoryContext> context;
    SingleRouteMatcherImpl matcher(config, context);
    auto invo = std::make_shared<RpcInvocationImpl>();
    MessageMetadata metadata;
    metadata.setInvocationInfo(invo);
    invo->setMethodName("add");
    EXPECT_EQ(nullptr, matcher.route(metadata, 0));

    invo->setServiceName("unknown");

    EXPECT_EQ("user_service_dubbo_server", matcher.route(metadata, 0)->routeEntry()->clusterName());

    invo->setServiceName("random");

    EXPECT_EQ("user_service_dubbo_server", matcher.route(metadata, 0)->routeEntry()->clusterName());

    invo->setServiceName("fake_service");

    EXPECT_EQ("user_service_dubbo_server", matcher.route(metadata, 0)->routeEntry()->clusterName());
  }
  {
    const std::string yaml = R"EOF(
name: local_route
interface: "*.test.com"
routes:
  - match:
      method:
        name:
          exact: "add"
    route:
        cluster: user_service_dubbo_server
)EOF";
    envoy::extensions::filters::network::dubbo_proxy::v3::RouteConfiguration config =
        parseRouteConfigurationFromV2Yaml(yaml);

    NiceMock<Server::Configuration::MockServerFactoryContext> context;
    SingleRouteMatcherImpl matcher(config, context);
    auto invo = std::make_shared<RpcInvocationImpl>();
    MessageMetadata metadata;
    metadata.setInvocationInfo(invo);
    invo->setMethodName("add");
    EXPECT_EQ(nullptr, matcher.route(metadata, 0));

    invo->setServiceName(".test.com");
    EXPECT_EQ(nullptr, matcher.route(metadata, 0));

    invo->setServiceName("code.test.com");

    EXPECT_EQ("user_service_dubbo_server", matcher.route(metadata, 0)->routeEntry()->clusterName());

    invo->setServiceName("fake.test.com");

    EXPECT_EQ("user_service_dubbo_server", matcher.route(metadata, 0)->routeEntry()->clusterName());

    invo->setServiceName("fake_service");

    EXPECT_EQ(nullptr, matcher.route(metadata, 0));
  }
  {
    const std::string yaml = R"EOF(
name: local_route
interface: "com.test.*"
routes:
  - match:
      method:
        name:
          exact: "add"
    route:
        cluster: user_service_dubbo_server
)EOF";
    envoy::extensions::filters::network::dubbo_proxy::v3::RouteConfiguration config =
        parseRouteConfigurationFromV2Yaml(yaml);

    NiceMock<Server::Configuration::MockServerFactoryContext> context;
    SingleRouteMatcherImpl matcher(config, context);
    auto invo = std::make_shared<RpcInvocationImpl>();
    MessageMetadata metadata;
    metadata.setInvocationInfo(invo);
    invo->setMethodName("add");
    EXPECT_EQ(nullptr, matcher.route(metadata, 0));

    invo->setServiceName("com.test.");
    EXPECT_EQ(nullptr, matcher.route(metadata, 0));

    invo->setServiceName("com.test.code");

    EXPECT_EQ("user_service_dubbo_server", matcher.route(metadata, 0)->routeEntry()->clusterName());

    invo->setServiceName("com.test.fake");

    EXPECT_EQ("user_service_dubbo_server", matcher.route(metadata, 0)->routeEntry()->clusterName());

    invo->setServiceName("fake_service");

    EXPECT_EQ(nullptr, matcher.route(metadata, 0));
  }
}

TEST(DubboRouteMatcherTest, RouteByServiceNameWithAnyMethod) {
  ScopedInjectableLoader<Regex::Engine> engine(std::make_unique<Regex::GoogleReEngine>());
  {
    const std::string yaml = R"EOF(
name: local_route
interface: org.apache.dubbo.demo.DemoService
routes:
  - match:
      method:
        name:
          safe_regex:
            regex: "(.*?)"
    route:
        cluster: user_service_dubbo_server
)EOF";

    envoy::extensions::filters::network::dubbo_proxy::v3::RouteConfiguration config =
        parseRouteConfigurationFromV2Yaml(yaml);

    NiceMock<Server::Configuration::MockServerFactoryContext> context;
    SingleRouteMatcherImpl matcher(config, context);
    auto invo = std::make_shared<RpcInvocationImpl>();
    MessageMetadata metadata;
    metadata.setInvocationInfo(invo);
    invo->setMethodName("test");
    EXPECT_EQ(nullptr, matcher.route(metadata, 0));

    invo->setServiceName("unknown");
    EXPECT_EQ(nullptr, matcher.route(metadata, 0));

    invo->setServiceGroup("test");
    EXPECT_EQ(nullptr, matcher.route(metadata, 0));

    invo->setServiceVersion("1.0.0");
    EXPECT_EQ(nullptr, matcher.route(metadata, 0));

    invo->setServiceName("org.apache.dubbo.demo.DemoService");
    EXPECT_EQ("user_service_dubbo_server", matcher.route(metadata, 0)->routeEntry()->clusterName());

    // Ignore version matches if there is no version field in the configuration information.
    invo->setServiceVersion("1.0.1");
    EXPECT_EQ("user_service_dubbo_server", matcher.route(metadata, 0)->routeEntry()->clusterName());

    invo->setServiceGroup("test_one");
    EXPECT_EQ("user_service_dubbo_server", matcher.route(metadata, 0)->routeEntry()->clusterName());
  }

  // Service name with optional(version and group) matches.
  {
    const std::string yaml = R"EOF(
name: local_route
interface: org.apache.dubbo.demo.DemoService
version: 1.0.0
group: test
routes:
  - match:
      method:
        name:
          safe_regex:
            regex: "(.*?)"
    route:
        cluster: user_service_dubbo_server
)EOF";

    envoy::extensions::filters::network::dubbo_proxy::v3::RouteConfiguration config =
        parseRouteConfigurationFromV2Yaml(yaml);

    NiceMock<Server::Configuration::MockServerFactoryContext> context;
    SingleRouteMatcherImpl matcher(config, context);
    auto invo = std::make_shared<RpcInvocationImpl>();
    MessageMetadata metadata;
    metadata.setInvocationInfo(invo);
    invo->setMethodName("test");
    invo->setServiceName("org.apache.dubbo.demo.DemoService");

    setDefaultInvocationCallback(*invo);

    EXPECT_EQ(nullptr, matcher.route(metadata, 0));

    invo->setServiceGroup("test");
    EXPECT_EQ(nullptr, matcher.route(metadata, 0));

    invo->setServiceVersion("1.0.0");

    EXPECT_EQ("user_service_dubbo_server", matcher.route(metadata, 0)->routeEntry()->clusterName());
  }

  // Service name with version matches.
  {
    const std::string yaml = R"EOF(
name: local_route
interface: org.apache.dubbo.demo.DemoService
version: 1.0.0
routes:
  - match:
      method:
        name:
          safe_regex:
            regex: "(.*?)"
    route:
        cluster: user_service_dubbo_server
)EOF";

    envoy::extensions::filters::network::dubbo_proxy::v3::RouteConfiguration config =
        parseRouteConfigurationFromV2Yaml(yaml);

    NiceMock<Server::Configuration::MockServerFactoryContext> context;
    SingleRouteMatcherImpl matcher(config, context);
    auto invo = std::make_shared<RpcInvocationImpl>();
    MessageMetadata metadata;
    metadata.setInvocationInfo(invo);
    invo->setMethodName("test");
    invo->setServiceName("org.apache.dubbo.demo.DemoService");
    EXPECT_EQ(nullptr, matcher.route(metadata, 0));

    invo->setServiceGroup("test");
    EXPECT_EQ(nullptr, matcher.route(metadata, 0));

    invo->setServiceVersion("1.0.0");
    EXPECT_NE(nullptr, matcher.route(metadata, 0));
    EXPECT_EQ("user_service_dubbo_server", matcher.route(metadata, 0)->routeEntry()->clusterName());

    // Ignore group matches if there is no group field in the configuration information.
    invo->setServiceGroup("test_1");
    EXPECT_EQ("user_service_dubbo_server", matcher.route(metadata, 0)->routeEntry()->clusterName());
  }

  // Service name with group matches.
  {
    const std::string yaml = R"EOF(
name: local_route
interface: org.apache.dubbo.demo.DemoService
group: HSF
routes:
  - match:
      method:
        name:
          safe_regex:
            regex: "(.*?)"
    route:
        cluster: user_service_dubbo_server
)EOF";

    envoy::extensions::filters::network::dubbo_proxy::v3::RouteConfiguration config =
        parseRouteConfigurationFromV2Yaml(yaml);

    NiceMock<Server::Configuration::MockServerFactoryContext> context;
    SingleRouteMatcherImpl matcher(config, context);
    auto invo = std::make_shared<RpcInvocationImpl>();
    MessageMetadata metadata;
    metadata.setInvocationInfo(invo);
    invo->setMethodName("test");
    invo->setServiceName("org.apache.dubbo.demo.DemoService");

    setDefaultInvocationCallback(*invo);

    EXPECT_EQ(nullptr, matcher.route(metadata, 0));

    invo->setServiceGroup("test");
    EXPECT_EQ(nullptr, matcher.route(metadata, 0));

    invo->setServiceVersion("1.0.0");
    EXPECT_EQ(nullptr, matcher.route(metadata, 0));

    invo->setServiceGroup("HSF");
    EXPECT_EQ("user_service_dubbo_server", matcher.route(metadata, 0)->routeEntry()->clusterName());
  }
}

TEST(DubboRouteMatcherTest, RouteByMethodWithExactMatch) {
  const std::string yaml = R"EOF(
name: local_route
interface: org.apache.dubbo.demo.DemoService
routes:
  - match:
      method:
        name:
          exact: "add"
    route:
        cluster: user_service_dubbo_server
)EOF";

  envoy::extensions::filters::network::dubbo_proxy::v3::RouteConfiguration config =
      parseRouteConfigurationFromV2Yaml(yaml);
  auto invo = std::make_shared<RpcInvocationImpl>();
  MessageMetadata metadata;
  metadata.setInvocationInfo(invo);
  invo->setServiceName("org.apache.dubbo.demo.DemoService");

  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  SingleRouteMatcherImpl matcher(config, context);
  EXPECT_EQ(nullptr, matcher.route(metadata, 0));

  invo->setMethodName("sub");
  EXPECT_EQ(nullptr, matcher.route(metadata, 0));

  invo->setMethodName("add");
  EXPECT_EQ("user_service_dubbo_server", matcher.route(metadata, 0)->routeEntry()->clusterName());
}

TEST(DubboRouteMatcherTest, RouteByMethodWithSuffixMatch) {
  const std::string yaml = R"EOF(
name: local_route
interface: org.apache.dubbo.demo.DemoService
routes:
  - match:
      method:
        name:
          suffix: "test"
    route:
        cluster: user_service_dubbo_server
)EOF";

  envoy::extensions::filters::network::dubbo_proxy::v3::RouteConfiguration config =
      parseRouteConfigurationFromV2Yaml(yaml);
  auto invo = std::make_shared<RpcInvocationImpl>();
  MessageMetadata metadata;
  metadata.setInvocationInfo(invo);
  invo->setServiceName("org.apache.dubbo.demo.DemoService");

  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  SingleRouteMatcherImpl matcher(config, context);
  EXPECT_EQ(nullptr, matcher.route(metadata, 0));

  invo->setMethodName("sub");
  EXPECT_EQ(nullptr, matcher.route(metadata, 0));

  invo->setMethodName("add123test");
  EXPECT_EQ("user_service_dubbo_server", matcher.route(metadata, 0)->routeEntry()->clusterName());
}

TEST(DubboRouteMatcherTest, RouteByMethodWithPrefixMatch) {
  const std::string yaml = R"EOF(
name: local_route
interface: org.apache.dubbo.demo.DemoService
routes:
  - match:
      method:
        name:
          prefix: "test"
    route:
        cluster: user_service_dubbo_server
)EOF";

  envoy::extensions::filters::network::dubbo_proxy::v3::RouteConfiguration config =
      parseRouteConfigurationFromV2Yaml(yaml);
  auto invo = std::make_shared<RpcInvocationImpl>();
  MessageMetadata metadata;
  metadata.setInvocationInfo(invo);
  invo->setServiceName("org.apache.dubbo.demo.DemoService");

  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  SingleRouteMatcherImpl matcher(config, context);
  EXPECT_EQ(nullptr, matcher.route(metadata, 0));

  invo->setMethodName("ab12test");
  EXPECT_EQ(nullptr, matcher.route(metadata, 0));

  invo->setMethodName("test12d2test");
  EXPECT_EQ("user_service_dubbo_server", matcher.route(metadata, 0)->routeEntry()->clusterName());

  invo->setMethodName("testme");
  EXPECT_EQ("user_service_dubbo_server", matcher.route(metadata, 0)->routeEntry()->clusterName());
}

TEST(DubboRouteMatcherTest, RouteByMethodWithRegexMatch) {
  ScopedInjectableLoader<Regex::Engine> engine(std::make_unique<Regex::GoogleReEngine>());
  const std::string yaml = R"EOF(
name: local_route
interface: org.apache.dubbo.demo.DemoService
routes:
  - match:
      method:
        name:
          safe_regex:
            regex: "\\d{3}test"
    route:
        cluster: user_service_dubbo_server
)EOF";

  envoy::extensions::filters::network::dubbo_proxy::v3::RouteConfiguration config =
      parseRouteConfigurationFromV2Yaml(yaml);
  auto invo = std::make_shared<RpcInvocationImpl>();
  MessageMetadata metadata;
  metadata.setInvocationInfo(invo);
  invo->setServiceName("org.apache.dubbo.demo.DemoService");

  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  SingleRouteMatcherImpl matcher(config, context);
  EXPECT_EQ(nullptr, matcher.route(metadata, 0));

  invo->setMethodName("12test");
  EXPECT_EQ(nullptr, matcher.route(metadata, 0));

  invo->setMethodName("456test");
  EXPECT_EQ("user_service_dubbo_server", matcher.route(metadata, 0)->routeEntry()->clusterName());

  invo->setMethodName("4567test");
  EXPECT_EQ(nullptr, matcher.route(metadata, 0));
}

TEST(DubboRouteMatcherTest, RouteByParamterWithNoParameter) {
  const std::string yaml = R"EOF(
name: local_route
interface: org.apache.dubbo.demo.DemoService
routes:
  - match:
      method:
        name:
          exact: "add"
        params_match:
          0:
            range_match:
              start: 100
              end: 200
    route:
        cluster: user_service_dubbo_server
)EOF";

  envoy::extensions::filters::network::dubbo_proxy::v3::RouteConfiguration config =
      parseRouteConfigurationFromV2Yaml(yaml);
  auto invo = std::make_shared<RpcInvocationImpl>();
  MessageMetadata metadata;
  metadata.setInvocationInfo(invo);
  invo->setServiceName("org.apache.dubbo.demo.DemoService");
  invo->setMethodName("add");

  setDefaultInvocationCallback(*invo);

  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  SingleRouteMatcherImpl matcher(config, context);
  EXPECT_EQ(nullptr, matcher.route(metadata, 0));
}

TEST(DubboRouteMatcherTest, RouteByParamterWithErrorIndex) {
  const std::string yaml = R"EOF(
name: local_route
interface: org.apache.dubbo.demo.DemoService
routes:
  - match:
      method:
        name:
          exact: "add"
        params_match:
          3:
            range_match:
              start: 100
              end: 200
    route:
        cluster: user_service_dubbo_server
)EOF";

  envoy::extensions::filters::network::dubbo_proxy::v3::RouteConfiguration config =
      parseRouteConfigurationFromV2Yaml(yaml);
  auto invo = std::make_shared<RpcInvocationImpl>();
  MessageMetadata metadata;
  metadata.setInvocationInfo(invo);
  invo->setServiceName("org.apache.dubbo.demo.DemoService");
  invo->setMethodName("add");

  setDefaultInvocationCallback(*invo);
  invo->mutableParameters()->push_back(std::make_unique<Hessian2::StringObject>("150"));

  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  SingleRouteMatcherImpl matcher(config, context);
  EXPECT_EQ(nullptr, matcher.route(metadata, 0));
}

TEST(DubboRouteMatcherTest, RouteByParameterWithErrorType) {
  const std::string yaml = R"EOF(
name: local_route
interface: org.apache.dubbo.demo.DemoService
routes:
  - match:
      method:
        name:
          exact: "add"
        params_match:
          0:
            range_match:
              start: 100
              end: 200
    route:
        cluster: user_service_dubbo_server
)EOF";

  envoy::extensions::filters::network::dubbo_proxy::v3::RouteConfiguration config =
      parseRouteConfigurationFromV2Yaml(yaml);
  auto invo = std::make_shared<RpcInvocationImpl>();
  MessageMetadata metadata;
  metadata.setInvocationInfo(invo);
  invo->setServiceName("org.apache.dubbo.demo.DemoService");
  invo->setMethodName("add");

  setDefaultInvocationCallback(*invo);
  invo->mutableParameters()->push_back(std::make_unique<Hessian2::NullObject>());

  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  SingleRouteMatcherImpl matcher(config, context);
  EXPECT_EQ(nullptr, matcher.route(metadata, 0));
}

TEST(DubboRouteMatcherTest, RouteByParameterWithRangeMatch) {
  const std::string yaml = R"EOF(
name: local_route
interface: org.apache.dubbo.demo.DemoService
routes:
  - match:
      method:
        name:
          exact: "add"
        params_match:
          0:
            range_match:
              start: 100
              end: 200
    route:
        cluster: user_service_dubbo_server
)EOF";

  envoy::extensions::filters::network::dubbo_proxy::v3::RouteConfiguration config =
      parseRouteConfigurationFromV2Yaml(yaml);
  auto invo = std::make_shared<RpcInvocationImpl>();
  MessageMetadata metadata;
  metadata.setInvocationInfo(invo);
  invo->setServiceName("org.apache.dubbo.demo.DemoService");
  invo->setMethodName("add");

  setDefaultInvocationCallback(*invo);
  invo->mutableParameters()->push_back(std::make_unique<Hessian2::StringObject>("150"));

  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  SingleRouteMatcherImpl matcher(config, context);
  EXPECT_EQ("user_service_dubbo_server", matcher.route(metadata, 0)->routeEntry()->clusterName());
}

TEST(DubboRouteMatcherTest, RouteByParameterWithRangeMatchButNotMatch) {
  const std::string yaml = R"EOF(
name: local_route
interface: org.apache.dubbo.demo.DemoService
routes:
  - match:
      method:
        name:
          exact: "add"
        params_match:
          0:
            range_match:
              start: 100
              end: 200
    route:
        cluster: user_service_dubbo_server
)EOF";

  envoy::extensions::filters::network::dubbo_proxy::v3::RouteConfiguration config =
      parseRouteConfigurationFromV2Yaml(yaml);
  auto invo = std::make_shared<RpcInvocationImpl>();
  MessageMetadata metadata;
  metadata.setInvocationInfo(invo);
  invo->setServiceName("org.apache.dubbo.demo.DemoService");
  invo->setMethodName("add");

  setDefaultInvocationCallback(*invo);
  invo->mutableParameters()->push_back(std::make_unique<Hessian2::StringObject>("300"));

  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  SingleRouteMatcherImpl matcher(config, context);
  EXPECT_EQ(nullptr, matcher.route(metadata, 0));
}

TEST(DubboRouteMatcherTest, RouteByParameterWithExactMatch) {
  const std::string yaml = R"EOF(
name: local_route
interface: org.apache.dubbo.demo.DemoService
routes:
  - match:
      method:
        name:
          exact: "add"
        params_match:
          1:
            exact_match: "user_id:94562"
    route:
        cluster: user_service_dubbo_server
)EOF";

  envoy::extensions::filters::network::dubbo_proxy::v3::RouteConfiguration config =
      parseRouteConfigurationFromV2Yaml(yaml);
  auto invo = std::make_shared<RpcInvocationImpl>();
  MessageMetadata metadata;
  metadata.setInvocationInfo(invo);
  invo->setServiceName("org.apache.dubbo.demo.DemoService");
  invo->setMethodName("add");

  setDefaultInvocationCallback(*invo);
  invo->mutableParameters()->resize(2);
  invo->mutableParameters()->back() = std::make_unique<Hessian2::StringObject>("user_id:94562");

  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  SingleRouteMatcherImpl matcher(config, context);
  EXPECT_EQ("user_service_dubbo_server", matcher.route(metadata, 0)->routeEntry()->clusterName());
}

TEST(DubboRouteMatcherTest, RouteWithHeaders) {
  const std::string yaml = R"EOF(
name: local_route
interface: org.apache.dubbo.demo.DemoService
routes:
  - match:
      method:
        name:
          exact: "add"
      headers:
      - name: custom
        string_match:
          exact: "123"
      - name: custom1
        string_match:
          exact: "123"
        invert_match: true
    route:
        cluster: user_service_dubbo_server
)EOF";

  envoy::extensions::filters::network::dubbo_proxy::v3::RouteConfiguration config =
      parseRouteConfigurationFromV2Yaml(yaml);
  auto invo = std::make_shared<RpcInvocationImpl>();
  MessageMetadata metadata;
  metadata.setInvocationInfo(invo);
  invo->setServiceName("org.apache.dubbo.demo.DemoService");
  invo->setMethodName("add");

  setDefaultInvocationCallback(*invo);

  invo->mutableAttachment()->insert("custom", "123");
  invo->mutableAttachment()->insert("custom1", "123");

  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  SingleRouteMatcherImpl matcher(config, context);
  EXPECT_EQ(nullptr, matcher.route(metadata, 0));

  invo->mutableAttachment()->insert("custom1", "456");
  EXPECT_EQ("user_service_dubbo_server", matcher.route(metadata, 0)->routeEntry()->clusterName());
  EXPECT_EQ(nullptr, matcher.route(metadata, 0)->routeEntry()->metadataMatchCriteria());

  invo->mutableAttachment()->insert("custom1", "123");
  EXPECT_EQ(nullptr, matcher.route(metadata, 0));
}

TEST(MultiDubboRouteMatcherTest, Route) {
  const std::string yaml = R"EOF(
stat_prefix: dubbo_incomming_stats
protocol_type: Dubbo
serialization_type: Hessian2
multiple_route_config:
  name: test_routes
  route_config:
    - name: test1
      interface: org.apache.dubbo.demo.DemoService
      routes:
        - match:
            method:
              name:
                exact: "add"
              params_match:
                1:
                  exact_match: "user_id"
          route:
              cluster: user_service_dubbo_server
    - name: test2
      interface: org.apache.dubbo.demo.FormatService
      routes:
        - match:
            method:
              name:
                exact: "format"
          route:
              cluster: format_service
)EOF";

  envoy::extensions::filters::network::dubbo_proxy::v3::DubboProxy config =
      parseDubboProxyFromV2Yaml(yaml);
  auto invo = std::make_shared<RpcInvocationImpl>();
  MessageMetadata metadata;
  metadata.setInvocationInfo(invo);
  invo->setServiceName("org.apache.dubbo.demo.DemoService");
  invo->setMethodName("add");

  setDefaultInvocationCallback(*invo);
  invo->mutableParameters()->resize(2);
  invo->mutableParameters()->back() = std::make_unique<Hessian2::StringObject>("user_id");

  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  RouteConfigImpl matcher(config.multiple_route_config(), context);
  EXPECT_EQ("user_service_dubbo_server", matcher.route(metadata, 0)->routeEntry()->clusterName());

  {
    envoy::extensions::filters::network::dubbo_proxy::v3::DubboProxy invalid_config;
    RouteConfigImpl matcher(invalid_config.multiple_route_config(), context);
    EXPECT_EQ(nullptr, matcher.route(metadata, 0));
  }
}

TEST(DubboRouteMatcherTest, RouteByInvalidParameter) {
  const std::string yaml = R"EOF(
name: local_route
interface: org.apache.dubbo.demo.DemoService
routes:
  - match:
      method:
        name:
          exact: "add"
        params_match:
          1:
            exact_match: "user_id:94562"
    route:
        cluster: user_service_dubbo_server
)EOF";

  envoy::extensions::filters::network::dubbo_proxy::v3::RouteConfiguration config =
      parseRouteConfigurationFromV2Yaml(yaml);
  auto invo = std::make_shared<RpcInvocationImpl>();
  MessageMetadata metadata;
  metadata.setInvocationInfo(invo);
  invo->setServiceName("org.apache.dubbo.demo.DemoService");
  invo->setMethodName("add");

  setDefaultInvocationCallback(*invo);

  // There is no parameter information in metadata.
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  SingleRouteMatcherImpl matcher(config, context);
  EXPECT_EQ(nullptr, matcher.route(metadata, 0));

  // The parameter is empty.
  invo->mutableParameters()->resize(2);
  invo->mutableParameters()->back() = std::make_unique<Hessian2::StringObject>("");

  EXPECT_EQ(nullptr, matcher.route(metadata, 0));

  {
    auto invo = std::make_shared<RpcInvocationImpl>();

    setDefaultInvocationCallback(*invo);

    // Error parameter.
    invo->mutableParameters()->resize(2);
    invo->mutableParameters()->back() = std::make_unique<Hessian2::StringObject>("user_id:562");

    MessageMetadata metadata;
    metadata.setInvocationInfo(invo);
    invo->setServiceName("org.apache.dubbo.demo.DemoService");
    invo->setMethodName("add");
    EXPECT_EQ(nullptr, matcher.route(metadata, 0));
  }
}

TEST(DubboRouteMatcherTest, WeightedClusters) {
  const std::string yaml = R"EOF(
name: local_route
interface: org.apache.dubbo.demo.DemoService
routes:
  - match:
      method:
        name:
          exact: "method1"
    route:
      weighted_clusters:
        clusters:
          - name: cluster1
            weight: 30
          - name: cluster2
            weight: 30
          - name: cluster3
            weight: 40
  - match:
      method:
        name:
          exact: "method2"
    route:
      weighted_clusters:
        clusters:
          - name: cluster1
            weight: 2000
          - name: cluster2
            weight: 3000
          - name: cluster3
            weight: 5000
)EOF";

  envoy::extensions::filters::network::dubbo_proxy::v3::RouteConfiguration config =
      parseRouteConfigurationFromV2Yaml(yaml);
  auto invo = std::make_shared<RpcInvocationImpl>();
  MessageMetadata metadata;
  metadata.setInvocationInfo(invo);
  invo->setServiceName("org.apache.dubbo.demo.DemoService");

  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  SingleRouteMatcherImpl matcher(config, context);

  {
    invo->setMethodName("method1");
    EXPECT_EQ("cluster1", matcher.route(metadata, 0)->routeEntry()->clusterName());
    EXPECT_EQ("cluster1", matcher.route(metadata, 29)->routeEntry()->clusterName());
    EXPECT_EQ("cluster2", matcher.route(metadata, 30)->routeEntry()->clusterName());
    EXPECT_EQ("cluster2", matcher.route(metadata, 59)->routeEntry()->clusterName());
    EXPECT_EQ("cluster3", matcher.route(metadata, 60)->routeEntry()->clusterName());
    EXPECT_EQ("cluster3", matcher.route(metadata, 99)->routeEntry()->clusterName());
    EXPECT_EQ("cluster1", matcher.route(metadata, 100)->routeEntry()->clusterName());
    EXPECT_EQ(nullptr, matcher.route(metadata, 100)->routeEntry()->metadataMatchCriteria());
  }

  {
    invo->setMethodName("method2");
    EXPECT_EQ("cluster1", matcher.route(metadata, 0)->routeEntry()->clusterName());
    EXPECT_EQ("cluster1", matcher.route(metadata, 1999)->routeEntry()->clusterName());
    EXPECT_EQ("cluster2", matcher.route(metadata, 2000)->routeEntry()->clusterName());
    EXPECT_EQ("cluster2", matcher.route(metadata, 4999)->routeEntry()->clusterName());
    EXPECT_EQ("cluster3", matcher.route(metadata, 5000)->routeEntry()->clusterName());
    EXPECT_EQ("cluster3", matcher.route(metadata, 9999)->routeEntry()->clusterName());
    EXPECT_EQ("cluster1", matcher.route(metadata, 10000)->routeEntry()->clusterName());
    EXPECT_EQ(nullptr, matcher.route(metadata, 10000)->routeEntry()->metadataMatchCriteria());
  }
}

TEST(DubboRouteMatcherTest, WeightedClusterMissingWeight) {
  const std::string yaml = R"EOF(
name: config
routes:
  - match:
      method:
        name:
          exact: "method1"
    route:
      weighted_clusters:
        clusters:
          - name: cluster1
            weight: 20000
          - name: cluster2
          - name: cluster3
            weight: 5000
)EOF";

  envoy::extensions::filters::network::dubbo_proxy::v3::RouteConfiguration config =
      parseRouteConfigurationFromV2Yaml(yaml);
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  EXPECT_THROW(SingleRouteMatcherImpl m(config, context), EnvoyException);
}

TEST(DubboRouteMatcherTest, RouteActionMetadataMatch) {
  const std::string yaml = R"EOF(
name: config
interface: org.apache.dubbo.demo.DemoService
routes:
  - match:
      method:
        name:
          exact: "method1"
    route:
      cluster: cluster1
      metadata_match:
        filter_metadata:
          envoy.lb:
            k1: v1
            k2: v2
  - match:
      method:
        name:
          exact: "method2"
    route:
      cluster: cluster2
)EOF";

  envoy::extensions::filters::network::dubbo_proxy::v3::RouteConfiguration config =
      parseRouteConfigurationFromV2Yaml(yaml);
  auto invo = std::make_shared<RpcInvocationImpl>();

  // Match with metadata.
  {
    MessageMetadata metadata;
    metadata.setInvocationInfo(invo);
    invo->setServiceName("org.apache.dubbo.demo.DemoService");
    invo->setMethodName("method1");

    setDefaultInvocationCallback(*invo);

    NiceMock<Server::Configuration::MockServerFactoryContext> context;
    SingleRouteMatcherImpl matcher(config, context);
    RouteConstSharedPtr route = matcher.route(metadata, 0);
    EXPECT_NE(nullptr, route);
    EXPECT_NE(nullptr, route->routeEntry());

    const Envoy::Router::MetadataMatchCriteria* criteria =
        route->routeEntry()->metadataMatchCriteria();
    EXPECT_NE(nullptr, criteria);
    const std::vector<Envoy::Router::MetadataMatchCriterionConstSharedPtr>& mmc =
        criteria->metadataMatchCriteria();
    EXPECT_EQ(2, mmc.size());

    ProtobufWkt::Value v1, v2;
    v1.set_string_value("v1");
    v2.set_string_value("v2");
    HashedValue hv1(v1), hv2(v2);

    EXPECT_EQ("k1", mmc[0]->name());
    EXPECT_EQ(hv1, mmc[0]->value());

    EXPECT_EQ("k2", mmc[1]->name());
    EXPECT_EQ(hv2, mmc[1]->value());
  }

  // Match with no metadata.
  {
    MessageMetadata metadata;
    metadata.setInvocationInfo(invo);
    invo->setServiceName("org.apache.dubbo.demo.DemoService");
    invo->setMethodName("method2");

    setDefaultInvocationCallback(*invo);

    NiceMock<Server::Configuration::MockServerFactoryContext> context;
    SingleRouteMatcherImpl matcher(config, context);
    RouteConstSharedPtr route = matcher.route(metadata, 0);
    EXPECT_NE(nullptr, route);
    EXPECT_NE(nullptr, route->routeEntry());
    EXPECT_EQ(nullptr, route->routeEntry()->metadataMatchCriteria());
  }
}

TEST(DubboRouteMatcherTest, WeightedClusterMetadataMatch) {
  const std::string yaml = R"EOF(
name: config
interface: org.apache.dubbo.demo.DemoService
routes:
  - match:
      method:
        name:
          exact: "method1"
    route:
      weighted_clusters:
        clusters:
          - name: cluster1
            weight: 2000
            metadata_match:
              filter_metadata:
                envoy.lb:
                  k1: v1
                  k2: v2
          - name: cluster2
            weight: 3000
            metadata_match:
              filter_metadata:
                not.envoy.lb:
                  k1: v1
                  k2: v2
          - name: cluster3
            weight: 5000
            metadata_match:
              filter_metadata:
                envoy.lb:
                  k3: v3
)EOF";

  envoy::extensions::filters::network::dubbo_proxy::v3::RouteConfiguration config =
      parseRouteConfigurationFromV2Yaml(yaml);
  auto invo = std::make_shared<RpcInvocationImpl>();
  MessageMetadata metadata;
  metadata.setInvocationInfo(invo);
  invo->setServiceName("org.apache.dubbo.demo.DemoService");
  invo->setMethodName("method1");

  setDefaultInvocationCallback(*invo);
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  SingleRouteMatcherImpl matcher(config, context);

  ProtobufWkt::Value v1, v2, v3;
  v1.set_string_value("v1");
  v2.set_string_value("v2");
  v3.set_string_value("v3");
  HashedValue hv1(v1), hv2(v2), hv3(v3);

  // Metadata match with multiple metadata entries.
  {

    RouteConstSharedPtr route = matcher.route(metadata, 0);
    EXPECT_NE(nullptr, route);
    EXPECT_NE(nullptr, route->routeEntry());

    const Envoy::Router::MetadataMatchCriteria* criteria =
        route->routeEntry()->metadataMatchCriteria();
    EXPECT_NE(nullptr, criteria);
    const std::vector<Envoy::Router::MetadataMatchCriterionConstSharedPtr>& mmc =
        criteria->metadataMatchCriteria();
    EXPECT_EQ(2, mmc.size());

    EXPECT_EQ("k1", mmc[0]->name());
    EXPECT_EQ(hv1, mmc[0]->value());

    EXPECT_EQ("k2", mmc[1]->name());
    EXPECT_EQ(hv2, mmc[1]->value());
  }

  // The case where none 'envoy.lb' metadata key is used in the 'metadata_match'.
  {
    RouteConstSharedPtr route = matcher.route(metadata, 2001);
    EXPECT_NE(nullptr, route);
    EXPECT_NE(nullptr, route->routeEntry());
    EXPECT_EQ(nullptr, route->routeEntry()->metadataMatchCriteria());
  }

  // Metadata match with single metadata entry.
  {
    RouteConstSharedPtr route = matcher.route(metadata, 5001);
    EXPECT_NE(nullptr, route);
    EXPECT_NE(nullptr, route->routeEntry());
    const Envoy::Router::MetadataMatchCriteria* criteria =
        route->routeEntry()->metadataMatchCriteria();
    EXPECT_NE(nullptr, criteria);
    const std::vector<Envoy::Router::MetadataMatchCriterionConstSharedPtr>& mmc =
        criteria->metadataMatchCriteria();
    EXPECT_EQ(1, mmc.size());

    EXPECT_EQ("k3", mmc[0]->name());
    EXPECT_EQ(hv3, mmc[0]->value());
  }
}

TEST(DubboRouteMatcherTest, WeightedClusterRouteActionMetadataMatchMerged) {
  const std::string yaml = R"EOF(
name: config
interface: org.apache.dubbo.demo.DemoService
routes:
  - match:
      method:
        name:
          exact: "method1"
    route:
      metadata_match:
        filter_metadata:
          envoy.lb:
            k1: v1
            k2: v2
      weighted_clusters:
        clusters:
          - name: cluster1
            weight: 2000
            metadata_match:
              filter_metadata:
                envoy.lb:
                  k3: v3
          - name: cluster2
            weight: 3000
          - name: cluster3
            weight: 5000
            metadata_match:
              filter_metadata:
                envoy.lb:
                  k2: v3
)EOF";

  envoy::extensions::filters::network::dubbo_proxy::v3::RouteConfiguration config =
      parseRouteConfigurationFromV2Yaml(yaml);
  auto invo = std::make_shared<RpcInvocationImpl>();
  MessageMetadata metadata;
  metadata.setInvocationInfo(invo);
  invo->setServiceName("org.apache.dubbo.demo.DemoService");
  invo->setMethodName("method1");

  setDefaultInvocationCallback(*invo);
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  SingleRouteMatcherImpl matcher(config, context);

  ProtobufWkt::Value v1, v2, v3;
  v1.set_string_value("v1");
  v2.set_string_value("v2");
  v3.set_string_value("v3");
  HashedValue hv1(v1), hv2(v2), hv3(v3);

  // 'metadata_match' of both weighted cluster and route action are configured.
  {

    RouteConstSharedPtr route = matcher.route(metadata, 0);
    EXPECT_NE(nullptr, route);
    EXPECT_NE(nullptr, route->routeEntry());

    const Envoy::Router::MetadataMatchCriteria* criteria =
        route->routeEntry()->metadataMatchCriteria();
    EXPECT_NE(nullptr, criteria);
    const std::vector<Envoy::Router::MetadataMatchCriterionConstSharedPtr>& mmc =
        criteria->metadataMatchCriteria();
    EXPECT_EQ(3, mmc.size());

    EXPECT_EQ("k1", mmc[0]->name());
    EXPECT_EQ(hv1, mmc[0]->value());

    EXPECT_EQ("k2", mmc[1]->name());
    EXPECT_EQ(hv2, mmc[1]->value());

    EXPECT_EQ("k3", mmc[2]->name());
    EXPECT_EQ(hv3, mmc[2]->value());
  }

  // Only 'metadata_match' of route action is configured.
  {
    RouteConstSharedPtr route = matcher.route(metadata, 2001);
    EXPECT_NE(nullptr, route);
    EXPECT_NE(nullptr, route->routeEntry());
    const Envoy::Router::MetadataMatchCriteria* criteria =
        route->routeEntry()->metadataMatchCriteria();
    EXPECT_NE(nullptr, criteria);
    const std::vector<Envoy::Router::MetadataMatchCriterionConstSharedPtr>& mmc =
        criteria->metadataMatchCriteria();
    EXPECT_EQ(2, mmc.size());

    EXPECT_EQ("k1", mmc[0]->name());
    EXPECT_EQ(hv1, mmc[0]->value());

    EXPECT_EQ("k2", mmc[1]->name());
    EXPECT_EQ(hv2, mmc[1]->value());
  }

  // 'metadata_match' of both weighted cluster and route action are configured and with same
  // metadata entry key.
  {
    RouteConstSharedPtr route = matcher.route(metadata, 5001);
    EXPECT_NE(nullptr, route);
    EXPECT_NE(nullptr, route->routeEntry());
    const Envoy::Router::MetadataMatchCriteria* criteria =
        route->routeEntry()->metadataMatchCriteria();
    EXPECT_NE(nullptr, criteria);
    const std::vector<Envoy::Router::MetadataMatchCriterionConstSharedPtr>& mmc =
        criteria->metadataMatchCriteria();
    EXPECT_EQ(2, mmc.size());

    EXPECT_EQ("k1", mmc[0]->name());
    EXPECT_EQ(hv1, mmc[0]->value());

    EXPECT_EQ("k2", mmc[1]->name());
    EXPECT_EQ(hv3, mmc[1]->value());
  }
}

} // namespace Router
} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
