#include "source/server/config_validation/server.h"

#include <memory>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"

#include "source/common/common/utility.h"
#include "source/common/config/utility.h"
#include "source/common/config/well_known_names.h"
#include "source/common/event/real_time_system.h"
#include "source/common/listener_manager/listener_info_impl.h"
#include "source/common/local_info/local_info_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/common/stats/tag_producer_impl.h"
#include "source/common/tls/context_manager_impl.h"
#include "source/common/version/version.h"
#include "source/server/admin/admin_factory_context.h"
#include "source/server/listener_manager_factory.h"
#include "source/server/null_overload_manager.h"
#include "source/server/overload_manager_impl.h"
#include "source/server/regex_engine.h"
#include "source/server/utils.h"

namespace Envoy {
namespace Server {

bool validateConfig(const Options& options,
                    const Network::Address::InstanceConstSharedPtr& local_address,
                    ComponentFactory& component_factory, Thread::ThreadFactory& thread_factory,
                    Filesystem::Instance& file_system,
                    const ProcessContextOptRef& process_context) {
  Thread::MutexBasicLockable access_log_lock;
  Stats::IsolatedStoreImpl stats_store;

  TRY_ASSERT_MAIN_THREAD {
    Event::RealTimeSystem time_system;
    ValidationInstance server(options, time_system, local_address, stats_store, access_log_lock,
                              component_factory, thread_factory, file_system, process_context);
    std::cout << "configuration '" << options.configPath() << "' OK" << std::endl;
    server.shutdown();
    return true;
  }
  END_TRY
  catch (const EnvoyException& e) {
    return false;
  }
}

ValidationInstance::ValidationInstance(
    const Options& options, Event::TimeSystem& time_system,
    const Network::Address::InstanceConstSharedPtr& local_address, Stats::IsolatedStoreImpl& store,
    Thread::BasicLockable& access_log_lock, ComponentFactory& component_factory,
    Thread::ThreadFactory& thread_factory, Filesystem::Instance& file_system,
    const ProcessContextOptRef& process_context)
    : options_(options), validation_context_(options_.allowUnknownStaticFields(),
                                             !options.rejectUnknownDynamicFields(),
                                             !options.ignoreUnknownDynamicFields()),
      stats_store_(store),
      api_(new Api::ValidationImpl(thread_factory, store, time_system, file_system,
                                   random_generator_, bootstrap_, process_context)),
      dispatcher_(api_->allocateDispatcher("main_thread")),
      access_log_manager_(options.fileFlushIntervalMsec(), *api_, *dispatcher_, access_log_lock,
                          store),
      grpc_context_(stats_store_.symbolTable()), http_context_(stats_store_.symbolTable()),
      router_context_(stats_store_.symbolTable()), time_system_(time_system),
      server_contexts_(*this), quic_stat_names_(stats_store_.symbolTable()) {
  TRY_ASSERT_MAIN_THREAD { initialize(options, local_address, component_factory); }
  END_TRY
  catch (const EnvoyException& e) {
    ENVOY_LOG(critical, "error initializing configuration '{}': {}", options.configPath(),
              e.what());
    shutdown();
    throw;
  }
}

void ValidationInstance::initialize(const Options& options,
                                    const Network::Address::InstanceConstSharedPtr& local_address,
                                    ComponentFactory& component_factory) {
  // See comments on InstanceImpl::initialize() for the overall flow here.
  //
  // For validation, we only do a subset of normal server initialization: everything that could fail
  // on a malformed config (e.g. JSON parsing and all the object construction that follows), but
  // more importantly nothing with observable effects (e.g. binding to ports or shutting down any
  // other Envoy process).
  //
  // If we get all the way through that stripped-down initialization flow, to the point where we'd
  // be ready to serve, then the config has passed validation.
  // Handle configuration that needs to take place prior to the main configuration load.
  THROW_IF_NOT_OK(InstanceUtil::loadBootstrapConfig(
      bootstrap_, options, messageValidationContext().staticValidationVisitor(), *api_));

  if (bootstrap_.has_application_log_config()) {
    THROW_IF_NOT_OK(
        Utility::assertExclusiveLogFormatMethod(options_, bootstrap_.application_log_config()));
    THROW_IF_NOT_OK(Utility::maybeSetApplicationLogFormat(bootstrap_.application_log_config()));
  }

  // Inject regex engine to singleton.
  regex_engine_ = createRegexEngine(
      bootstrap_, messageValidationContext().staticValidationVisitor(), serverFactoryContext());

  auto producer_or_error =
      Stats::TagProducerImpl::createTagProducer(bootstrap_.stats_config(), options_.statsTags());
  THROW_IF_NOT_OK_REF(producer_or_error.status());
  if (!bootstrap_.node().user_agent_build_version().has_version()) {
    *bootstrap_.mutable_node()->mutable_user_agent_build_version() = VersionInfo::buildVersion();
  }

  local_info_ = std::make_unique<LocalInfo::LocalInfoImpl>(
      stats().symbolTable(), bootstrap_.node(), bootstrap_.node_context_params(), local_address,
      options.serviceZone(), options.serviceClusterName(), options.serviceNodeName());

  overload_manager_ = THROW_OR_RETURN_VALUE(
      OverloadManagerImpl::create(
          dispatcher(), *stats().rootScope(), threadLocal(), bootstrap_.overload_manager(),
          messageValidationContext().staticValidationVisitor(), *api_, options_),
      std::unique_ptr<OverloadManagerImpl>);
  null_overload_manager_ = std::make_unique<NullOverloadManager>(threadLocal(), false);
  absl::Status creation_status = absl::OkStatus();
  Configuration::InitialImpl initial_config(bootstrap_, creation_status);
  THROW_IF_NOT_OK_REF(creation_status);
  AdminFactoryContext factory_context(*this, std::make_shared<ListenerInfoImpl>());
  initial_config.initAdminAccessLog(bootstrap_, factory_context);
  admin_ = std::make_unique<Server::ValidationAdmin>(initial_config.admin().address());
  listener_manager_ = Config::Utility::getAndCheckFactoryByName<ListenerManagerFactory>(
                          Config::ServerExtensionValues::get().VALIDATION_LISTENER)
                          .createListenerManager(*this, nullptr, *this, false, quic_stat_names_);
  thread_local_.registerThread(*dispatcher_, true);

  runtime_ = component_factory.createRuntime(*this, initial_config);
  ENVOY_BUG(runtime_ != nullptr,
            "Component factory should not return nullptr from createRuntime()");
  drain_manager_ = component_factory.createDrainManager(*this);
  ENVOY_BUG(drain_manager_ != nullptr,
            "Component factory should not return nullptr from createDrainManager()");

  secret_manager_ = std::make_unique<Secret::SecretManagerImpl>(admin()->getConfigTracker());
  ssl_context_manager_ =
      std::make_unique<Extensions::TransportSockets::Tls::ContextManagerImpl>(server_contexts_);

  http_server_properties_cache_manager_ =
      std::make_unique<Http::HttpServerPropertiesCacheManagerImpl>(
          serverFactoryContext(), messageValidationContext().staticValidationVisitor(),
          thread_local_);

  cluster_manager_factory_ = std::make_unique<Upstream::ValidationClusterManagerFactory>(
      server_contexts_, stats(), threadLocal(), http_context_,
      [this]() -> Network::DnsResolverSharedPtr { return this->dnsResolver(); },
      sslContextManager(), *secret_manager_, quic_stat_names_, *this);
  THROW_IF_NOT_OK(config_.initialize(bootstrap_, *this, *cluster_manager_factory_));
  THROW_IF_NOT_OK(runtime().initialize(clusterManager()));
  clusterManager().setInitializedCb([this]() -> void { init_manager_.initialize(init_watcher_); });
}

void ValidationInstance::shutdown() {
  // This normally happens at the bottom of InstanceImpl::run(), but we don't have a run(). We can
  // do an abbreviated shutdown here since there's less to clean up -- for example, no workers to
  // exit.
  thread_local_.shutdownGlobalThreading();
  if (config_.clusterManager() != nullptr) {
    config_.clusterManager()->shutdown();
  }
  thread_local_.shutdownThread();
  dispatcher_->shutdown();
}

Network::DnsResolverSharedPtr ValidationInstance::dnsResolver() {
  envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config;
  Network::DnsResolverFactory& dns_resolver_factory =
      Network::createDefaultDnsResolverFactory(typed_dns_resolver_config);
  return THROW_OR_RETURN_VALUE(
      dns_resolver_factory.createDnsResolver(dispatcher(), api(), typed_dns_resolver_config),
      Network::DnsResolverSharedPtr);
}

} // namespace Server
} // namespace Envoy
