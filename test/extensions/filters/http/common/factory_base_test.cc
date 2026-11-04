#include "envoy/extensions/filters/http/router/v3/router.pb.h"
#include "envoy/extensions/filters/http/router/v3/router.pb.validate.h"
#include "envoy/http/filter.h"

#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/filters/http/common/factory_base.h"

#include "test/mocks/init/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Common {
namespace {

using ::Envoy::StatusHelpers::HasStatus;
using ::Envoy::StatusHelpers::IsOkAndHolds;
using RouterProto = envoy::extensions::filters::http::router::v3::Router;

// A minimal concrete filter factory used to test the default (non-overridden) behavior of
// FactoryBase. It only implements the pure `createFilterFactoryFromProtoTyped` method and relies on
// the base class defaults for everything else.
class TestFactoryBase : public FactoryBase<RouterProto> {
public:
  TestFactoryBase() : FactoryBase("test.factory_base") {}

  Envoy::Http::FilterFactoryCb
  createFilterFactoryFromProtoTyped(const RouterProto&, const std::string&,
                                    Server::Configuration::FactoryContext&) override {
    return [](Envoy::Http::FilterChainFactoryCallbacks&) {};
  }
};

// A concrete filter factory used to test the default behavior of ExceptionFreeFactoryBase.
class TestExceptionFreeFactoryBase : public ExceptionFreeFactoryBase<RouterProto> {
public:
  TestExceptionFreeFactoryBase() : ExceptionFreeFactoryBase("test.exception_free_factory_base") {}

  absl::StatusOr<Envoy::Http::FilterFactoryCb>
  createFilterFactoryFromProtoTyped(const RouterProto&, const std::string&,
                                    Server::Configuration::FactoryContext&) override {
    return [](Envoy::Http::FilterChainFactoryCallbacks&) {};
  }
};

// A concrete filter factory used to test the GenericFactoryContext entry point of
// UnifiedFactoryBase. It records what the generic context resolves to.
class TestUnifiedFactoryBase : public UnifiedFactoryBase<RouterProto> {
public:
  TestUnifiedFactoryBase() : UnifiedFactoryBase("test.unified_factory_base") {}

  absl::StatusOr<Envoy::Http::FilterFactoryCb>
  createFilterFactoryTyped(const RouterProto&,
                           Server::Configuration::GenericFactoryContext& context) override {
    server_context_ = &context.serverFactoryContext();
    visitor_ = &context.messageValidationVisitor();
    init_manager_ = &context.initManager();
    scope_ = &context.scope();
    stats_scope_ = &context.statsScope();
    return [](Envoy::Http::FilterChainFactoryCallbacks&) {};
  }

  absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
  createRouteConfigTyped(const RouterProto&,
                         Server::Configuration::GenericFactoryContext& context) override {
    server_context_ = &context.serverFactoryContext();
    visitor_ = &context.messageValidationVisitor();
    init_manager_ = &context.initManager();
    scope_ = &context.scope();
    stats_scope_ = &context.statsScope();
    return std::make_shared<Router::RouteSpecificFilterConfig>();
  }

  Server::Configuration::ServerFactoryContext* server_context_{};
  ProtobufMessage::ValidationVisitor* visitor_{};
  Init::Manager* init_manager_{};
  Stats::Scope* scope_{};
  Stats::Scope* stats_scope_{};
};

// A concrete filter factory that overrides none of the UnifiedFactoryBase entry points.
class TestEmptyUnifiedFactoryBase : public UnifiedFactoryBase<RouterProto> {
public:
  TestEmptyUnifiedFactoryBase() : UnifiedFactoryBase("test.empty_unified_factory_base") {}
};

// A concrete filter factory used to test the default behavior of DualFactoryBase.
class TestDualFactoryBase : public DualFactoryBase<RouterProto> {
public:
  TestDualFactoryBase() : DualFactoryBase("test.dual_factory_base") {}

  absl::StatusOr<Envoy::Http::FilterFactoryCb>
  createFilterFactoryFromProtoTyped(const RouterProto&, const std::string&, DualInfo,
                                    Server::Configuration::ServerFactoryContext&) override {
    return [](Envoy::Http::FilterChainFactoryCallbacks&) {};
  }
};

// Exercises the shared CommonFactoryBase helpers: proto factory methods, name and the default
// terminal-filter and route-config behaviors.
TEST(FactoryBaseTest, CommonBehavior) {
  TestFactoryBase factory;
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> server_context;

  EXPECT_EQ("test.factory_base", factory.name());

  // Empty config/route-config protos are created with the templated proto types.
  EXPECT_NE(nullptr, factory.createEmptyConfigProto());
  EXPECT_NE(nullptr, factory.createEmptyRouteConfigProto());

  // The default terminal-filter implementation returns false.
  RouterProto proto_config;
  EXPECT_FALSE(factory.isTerminalFilterByProto(proto_config, server_context));

  // The default route-specific config implementation returns a nullptr config.
  auto route_config = factory.createRouteSpecificFilterConfig(
      proto_config, server_context, server_context.messageValidationVisitor());
  ASSERT_THAT(route_config, IsOkAndHolds(nullptr));
}

// FactoryBase falls back to createFilterFactoryFromProtoWithServerContextTyped for the
// server-context based creation, which by default throws.
TEST(FactoryBaseTest, ServerContextNotSupported) {
  TestFactoryBase factory;
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> server_context;
  RouterProto proto_config;

  EXPECT_THROW_WITH_MESSAGE(
      factory.createFilterFactoryFromProtoWithServerContext(proto_config, "stats", server_context),
      EnvoyException, "Creating filter factory from server factory context is not supported");
  Server::Configuration::ExtraFactoryContext extra_context{
      server_context.messageValidationVisitor(), "stats"};

  // createHttpFilterFactoryFromProto delegates to the typed variant, which in turn delegates to the
  // (throwing) server-context implementation.
  EXPECT_THROW_WITH_MESSAGE(
      factory.createHttpFilterFactoryFromProto(proto_config, server_context, extra_context)
          .IgnoreError(),
      EnvoyException, "Creating filter factory from server factory context is not supported");
}

// FactoryBase's downstream FactoryContext path works and returns a valid factory callback.
TEST(FactoryBaseTest, FactoryContextCreation) {
  TestFactoryBase factory;
  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  RouterProto proto_config;

  auto cb = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  ASSERT_THAT(cb, IsOkAndHolds(::testing::NotNull()));
}

// ExceptionFreeFactoryBase returns an error status (rather than throwing) when server-context based
// creation is not supported.
TEST(FactoryBaseTest, ExceptionFreeServerContextNotSupported) {
  TestExceptionFreeFactoryBase factory;
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> server_context;
  RouterProto proto_config;

  EXPECT_EQ("test.exception_free_factory_base", factory.name());
  Server::Configuration::ExtraFactoryContext extra_context{
      server_context.messageValidationVisitor(), "stats"};

  auto result =
      factory.createHttpFilterFactoryFromProto(proto_config, server_context, extra_context);
  EXPECT_THAT(
      result,
      HasStatus(absl::StatusCode::kInvalidArgument,
                "Creating HTTP filter factory from server factory context is not supported"));
}

// ExceptionFreeFactoryBase's downstream FactoryContext path works.
TEST(FactoryBaseTest, ExceptionFreeFactoryContextCreation) {
  TestExceptionFreeFactoryBase factory;
  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  RouterProto proto_config;

  auto cb = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  ASSERT_THAT(cb, IsOkAndHolds(::testing::NotNull()));
}

// DualFactoryBase's downstream and upstream FactoryContext paths both work.
TEST(FactoryBaseTest, DualFactoryContextCreation) {
  TestDualFactoryBase factory;
  RouterProto proto_config;

  EXPECT_EQ("test.dual_factory_base", factory.name());

  testing::NiceMock<Server::Configuration::MockFactoryContext> downstream_context;
  auto downstream_cb =
      factory.createFilterFactoryFromProto(proto_config, "stats", downstream_context);
  ASSERT_THAT(downstream_cb, IsOkAndHolds(::testing::NotNull()));

  testing::NiceMock<Server::Configuration::MockUpstreamFactoryContext> upstream_context;
  auto upstream_cb = factory.createFilterFactoryFromProto(proto_config, "stats", upstream_context);
  ASSERT_THAT(upstream_cb, IsOkAndHolds(::testing::NotNull()));
}

// DualFactoryBase falls back to the (throwing) server-context typed implementation for both the
// server-context and HTTP filter factory creation paths.
TEST(FactoryBaseTest, DualServerContextNotSupported) {
  TestDualFactoryBase factory;
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> server_context;
  RouterProto proto_config;

  EXPECT_THROW_WITH_MESSAGE(
      factory.createFilterFactoryFromProtoWithServerContext(proto_config, "stats", server_context),
      EnvoyException,
      "DualFactoryBase: creating filter factory from server factory context is not supported");
  Server::Configuration::ExtraFactoryContext extra_context{
      server_context.messageValidationVisitor(), "stats"};

  EXPECT_THROW_WITH_MESSAGE(
      factory.createHttpFilterFactoryFromProto(proto_config, server_context, extra_context)
          .IgnoreError(),
      EnvoyException,
      "DualFactoryBase: creating filter factory from server factory context is not supported");
}

TEST(FactoryBaseTest, UnifiedGenericContextWithStatsPrefixScope) {
  TestUnifiedFactoryBase factory;
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> server_context;
  testing::NiceMock<ProtobufMessage::MockValidationVisitor> visitor;
  testing::NiceMock<Init::MockManager> init_manager;
  Stats::IsolatedStoreImpl store;
  Stats::ScopeSharedPtr prefix_scope = store.createScope("http.hcm.");
  RouterProto proto_config;

  Server::Configuration::ExtraFactoryContext extra_context{visitor, "http.hcm."};
  extra_context.init_manager = init_manager;
  extra_context.scope = *store.rootScope();
  extra_context.stats_prefix_scope = *prefix_scope;

  auto cb = factory.createHttpFilterFactoryFromProto(proto_config, server_context, extra_context);
  ASSERT_THAT(cb, IsOkAndHolds(::testing::NotNull()));
  EXPECT_EQ(&server_context, factory.server_context_);
  EXPECT_EQ(&visitor, factory.visitor_);
  EXPECT_EQ(&init_manager, factory.init_manager_);
  EXPECT_EQ(prefix_scope.get(), factory.scope_);
  EXPECT_EQ(prefix_scope.get(), factory.stats_scope_);
}

TEST(FactoryBaseTest, UnifiedGenericContextFallsBackToServerContext) {
  TestUnifiedFactoryBase factory;
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> server_context;
  RouterProto proto_config;

  Server::Configuration::ExtraFactoryContext extra_context{
      server_context.messageValidationVisitor(), "stats."};

  auto cb = factory.createHttpFilterFactoryFromProto(proto_config, server_context, extra_context);
  ASSERT_THAT(cb, IsOkAndHolds(::testing::NotNull()));
  EXPECT_EQ(&server_context.initManager(), factory.init_manager_);
  EXPECT_EQ(&server_context.scope(), factory.scope_);
  EXPECT_EQ(&server_context.scope(), factory.stats_scope_);
}

TEST(FactoryBaseTest, UnifiedGenericContextFromFactoryContext) {
  TestUnifiedFactoryBase factory;
  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  RouterProto proto_config;

  auto cb = factory.createFilterFactoryFromProto(proto_config, "stats.", context);
  ASSERT_THAT(cb, IsOkAndHolds(::testing::NotNull()));
  EXPECT_EQ(&context.scope(), factory.scope_);
  EXPECT_EQ(&context.scope(), factory.stats_scope_);
}

TEST(FactoryBaseTest, UnifiedNoEntryPointImplemented) {
  TestEmptyUnifiedFactoryBase factory;
  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  RouterProto proto_config;

  EXPECT_THAT(factory.createFilterFactoryFromProto(proto_config, "stats.", context),
              HasStatus(absl::StatusCode::kUnimplemented,
                        "UnifiedFactoryBase: no filter factory creation is implemented for "
                        "test.empty_unified_factory_base"));
}

TEST(FactoryBaseTest, UnifiedGenericContextForRouteConfig) {
  TestUnifiedFactoryBase factory;
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> server_context;
  testing::NiceMock<ProtobufMessage::MockValidationVisitor> visitor;
  testing::NiceMock<Init::MockManager> init_manager;
  RouterProto proto_config;

  Server::Configuration::ExtraFactoryContext extra_context{visitor, EMPTY_STRING, init_manager};

  auto config = factory.createHttpFilterRouteConfig(proto_config, server_context, extra_context);
  ASSERT_THAT(config, IsOkAndHolds(::testing::NotNull()));
  EXPECT_EQ(&server_context, factory.server_context_);
  EXPECT_EQ(&visitor, factory.visitor_);
  EXPECT_EQ(&init_manager, factory.init_manager_);
  EXPECT_EQ(&server_context.scope(), factory.scope_);
  EXPECT_EQ(&server_context.scope(), factory.stats_scope_);
}

TEST(FactoryBaseTest, UnifiedRouteSpecificFilterConfigSkipsGenericContext) {
  TestUnifiedFactoryBase factory;
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> server_context;
  RouterProto proto_config;

  // The legacy entry point only reaches createRouteSpecificFilterConfigTyped().
  EXPECT_THAT(factory.createRouteSpecificFilterConfig(proto_config, server_context,
                                                      server_context.messageValidationVisitor()),
              IsOkAndHolds(nullptr));
  EXPECT_EQ(nullptr, factory.visitor_);
}

TEST(FactoryBaseTest, UnifiedRouteConfigDefaultsToNullptr) {
  TestEmptyUnifiedFactoryBase factory;
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> server_context;
  RouterProto proto_config;

  Server::Configuration::ExtraFactoryContext extra_context{
      server_context.messageValidationVisitor(), EMPTY_STRING};
  EXPECT_THAT(factory.createHttpFilterRouteConfig(proto_config, server_context, extra_context),
              IsOkAndHolds(nullptr));
  EXPECT_THAT(factory.createRouteSpecificFilterConfig(proto_config, server_context,
                                                      server_context.messageValidationVisitor()),
              IsOkAndHolds(nullptr));
}

} // namespace
} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
