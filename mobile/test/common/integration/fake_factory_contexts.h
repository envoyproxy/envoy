#pragma once

#include "envoy/api/api.h"
#include "envoy/server/factory_context.h"
#include "envoy/server/lifecycle_notifier.h"
#include "envoy/server/options.h"

#include "source/common/common/assert.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/secret/secret_manager_impl.h"
#include "source/common/singleton/manager_impl.h"
#include "source/common/tls/context_manager_impl.h"

namespace Envoy {

/**
 * Minimal fake implementation of ServerFactoryContext for use by the mobile test server's TLS
 * setup. Only the methods actually exercised in that code path are meaningfully implemented;
 * all other methods call PANIC("not implemented").
 *
 * This avoids the need for gmock-based MockServerFactoryContext, which transitively pulls in
 * large mock object files (router_mocks, network_mocks, http_mocks, stream_info_mocks, etc.)
 * that inflate the link input size for mobile Swift integration test bundles.
 */
class FakeServerFactoryContext : public Server::Configuration::ServerFactoryContext {
public:
  FakeServerFactoryContext(Api::Api& api, TimeSource& time_source, Stats::Scope& server_scope)
      : api_(api), time_source_(time_source), server_scope_(server_scope),
        secret_manager_(std::make_unique<Secret::SecretManagerImpl>(std::nullopt)),
        singleton_manager_(std::make_unique<Singleton::ManagerImpl>()),
        dispatcher_(api_.allocateDispatcher("test_server")), context_manager_(*this) {}

  // CommonFactoryContext
  const Server::Options& options() override { return options_; }
  Event::Dispatcher& mainThreadDispatcher() override { return *dispatcher_; }
  Api::Api& api() override { return api_; }
  const LocalInfo::LocalInfo& localInfo() const override { PANIC("not implemented"); }
  OptRef<Server::Admin> admin() override { return {}; }
  Envoy::Runtime::Loader& runtime() override { PANIC("not implemented"); }
  Singleton::Manager& singletonManager() override { return *singleton_manager_; }
  ProtobufMessage::ValidationContext& messageValidationContext() override {
    PANIC("not implemented");
  }
  ProtobufMessage::ValidationVisitor& messageValidationVisitor() override {
    return ProtobufMessage::getNullValidationVisitor();
  }
  Stats::Scope& scope() override { return server_scope_; }
  Stats::Scope& serverScope() override { return server_scope_; }
  ThreadLocal::Instance& threadLocal() override { PANIC("not implemented"); }
  Upstream::ClusterManager& clusterManager() override { PANIC("not implemented"); }
  Config::XdsManager& xdsManager() override { PANIC("not implemented"); }
  Http::HttpServerPropertiesCacheManager& httpServerPropertiesCacheManager() override {
    PANIC("not implemented");
  }
  TimeSource& timeSource() override { return time_source_; }
  AccessLog::AccessLogManager& accessLogManager() override { PANIC("not implemented"); }
  Server::ServerLifecycleNotifier& lifecycleNotifier() override { return lifecycle_notifier_; }
  Regex::Engine& regexEngine() override { PANIC("not implemented"); }

  // ServerFactoryContext
  Http::Context& httpContext() override { PANIC("not implemented"); }
  Grpc::Context& grpcContext() override { PANIC("not implemented"); }
  Router::Context& routerContext() override { PANIC("not implemented"); }
  ProcessContextOptRef processContext() override { return {}; }
  Init::Manager& initManager() override { PANIC("not implemented"); }
  Envoy::Server::DrainManager& drainManager() override { PANIC("not implemented"); }
  Server::Configuration::StatsConfig& statsConfig() override { PANIC("not implemented"); }
  envoy::config::bootstrap::v3::Bootstrap& bootstrap() override { return bootstrap_; }
  Server::OverloadManager& overloadManager() override { PANIC("not implemented"); }
  Server::OverloadManager& nullOverloadManager() override { PANIC("not implemented"); }
  bool healthCheckFailed() const override { PANIC("not implemented"); }
  Ssl::ContextManager& sslContextManager() override { return context_manager_; }
  // secretManager() returns a real SecretManagerImpl that supports static/inline TLS certificates
  // only. Any SDS-based secret path will panic because initManager() and localInfo() are not
  // implemented.
  Secret::SecretManager& secretManager() override { return *secret_manager_; }

private:
  // Minimal NullOptions: all methods return default values or PANIC; none are called in practice
  // for the test server's TLS setup.
  struct NullOptions : public Server::Options {
    uint64_t baseId() const override { return 0; }
    bool useDynamicBaseId() const override { return false; }
    bool skipHotRestartOnNoParent() const override { return false; }
    bool skipHotRestartParentStats() const override { return false; }
    const std::string& baseIdPath() const override { return empty_string_; }
    uint32_t concurrency() const override { return 1; }
    std::chrono::seconds drainTime() const override { return std::chrono::seconds(0); }
    Server::DrainStrategy drainStrategy() const override { return Server::DrainStrategy::Gradual; }
    std::chrono::seconds parentShutdownTime() const override { return std::chrono::seconds(0); }
    const std::string& configPath() const override { return empty_string_; }
    const std::string& configYaml() const override { return empty_string_; }
    const envoy::config::bootstrap::v3::Bootstrap& configProto() const override {
      return options_bootstrap_;
    }
    bool allowUnknownStaticFields() const override { return false; }
    bool rejectUnknownDynamicFields() const override { return false; }
    bool ignoreUnknownDynamicFields() const override { return false; }
    bool skipDeprecatedLogs() const override { return false; }
    bool logStacktraceSingleEntry() const override { return false; }
    const std::string& adminAddressPath() const override { return empty_string_; }
    Network::Address::IpVersion localAddressIpVersion() const override {
      return Network::Address::IpVersion::v4;
    }
    spdlog::level::level_enum logLevel() const override { return spdlog::level::level_enum::warn; }
    const std::vector<std::pair<std::string, spdlog::level::level_enum>>&
    componentLogLevels() const override {
      return component_log_levels_;
    }
    const std::string& logFormat() const override { return empty_string_; }
    bool logFormatSet() const override { return false; }
    bool logFormatEscaped() const override { return false; }
    bool enableFineGrainLogging() const override { return false; }
    const std::string& logPath() const override { return empty_string_; }
    uint64_t restartEpoch() const override { return 0; }
    Server::Mode mode() const override { return Server::Mode::Serve; }
    std::chrono::milliseconds fileFlushIntervalMsec() const override {
      return std::chrono::milliseconds(0);
    }
    uint64_t fileFlushMinSizeKB() const override { return 0; }
    const std::string& serviceClusterName() const override { return empty_string_; }
    const std::string& serviceNodeName() const override { return empty_string_; }
    const std::string& serviceZone() const override { return empty_string_; }
    bool hotRestartDisabled() const override { return true; }
    bool signalHandlingEnabled() const override { return false; }
    bool mutexTracingEnabled() const override { return false; }
    bool coreDumpEnabled() const override { return false; }
    bool cpusetThreadsEnabled() const override { return false; }
    const std::vector<std::string>& disabledExtensions() const override {
      return disabled_extensions_;
    }
    Server::CommandLineOptionsPtr toCommandLineOptions() const override { return nullptr; }
    const std::string& socketPath() const override { return empty_string_; }
    mode_t socketMode() const override { return 0; }
    const Stats::TagVector& statsTags() const override { return stats_tags_; }

    const std::string empty_string_;
    const std::vector<std::pair<std::string, spdlog::level::level_enum>> component_log_levels_;
    const std::vector<std::string> disabled_extensions_;
    const Stats::TagVector stats_tags_;
    const envoy::config::bootstrap::v3::Bootstrap options_bootstrap_;
  };

  // Minimal lifecycle notifier: stores a reference but none of its methods are called in practice
  // for the test server's TLS setup.
  struct NullLifecycleNotifier : public Server::ServerLifecycleNotifier {
    HandlePtr registerCallback(Stage, StageCallback) override { PANIC("not implemented"); }
    HandlePtr registerCallback(Stage, StageCallbackWithCompletion) override {
      PANIC("not implemented");
    }
  };

  Api::Api& api_;
  TimeSource& time_source_;
  Stats::Scope& server_scope_;
  NullOptions options_;
  NullLifecycleNotifier lifecycle_notifier_;
  std::unique_ptr<Secret::SecretManagerImpl> secret_manager_;
  std::unique_ptr<Singleton::ManagerImpl> singleton_manager_;
  // Dispatcher exists only to satisfy the mainThreadDispatcher() interface requirement. It is never
  // run. Callbacks posted to it (e.g. ContextManagerImpl's cross-thread context removal) are
  // intentionally never executed in this test context.
  Event::DispatcherPtr dispatcher_;
  envoy::config::bootstrap::v3::Bootstrap bootstrap_;
  Extensions::TransportSockets::Tls::ContextManagerImpl context_manager_;
};

/**
 * Minimal fake implementation of GenericFactoryContext (== TransportSocketFactoryContext) for use
 * by the mobile test server's TLS setup.
 */
class FakeTransportSocketFactoryContext : public Server::Configuration::GenericFactoryContext {
public:
  FakeTransportSocketFactoryContext(FakeServerFactoryContext& server_ctx, Stats::Scope& stats_scope)
      : server_ctx_(server_ctx), stats_scope_(stats_scope) {}

  Server::Configuration::ServerFactoryContext& serverFactoryContext() override {
    return server_ctx_;
  }
  ProtobufMessage::ValidationVisitor& messageValidationVisitor() override {
    return ProtobufMessage::getNullValidationVisitor();
  }
  Init::Manager& initManager() override { PANIC("not implemented"); }
  Stats::Scope& scope() override { return stats_scope_; }

private:
  FakeServerFactoryContext& server_ctx_;
  Stats::Scope& stats_scope_;
};

} // namespace Envoy
