#include "source/common/router/deferred_virtual_host.h"

#include <cstdint>
#include <functional>
#include <vector>

#include "source/common/config/datasource.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/common/router/config_impl.h"

#include "absl/algorithm/container.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "udpa/type/v1/typed_struct.pb.h"
#include "xds/type/v3/typed_struct.pb.h"

namespace Envoy {
namespace Router {
namespace {

class NoOpSlot : public ThreadLocal::Slot {
public:
  bool currentThreadRegistered() override { return false; }
  ThreadLocal::ThreadLocalObjectSharedPtr get() override { return nullptr; }
  void set(InitializeCb) override {}
  void runOnAllThreads(const UpdateCb&) override {}
  void runOnAllThreads(const UpdateCb&, const std::function<void()>& complete_cb) override {
    if (complete_cb) {
      complete_cb();
    }
  }
  bool isShutdown() const override { return false; }
};

class NullInitManager : public Init::Manager {
public:
  State state() const override { return State::Initialized; }
  void add(const Init::Target&) override {}
  void initialize(const Init::Watcher& watcher) override {
    watcher.createHandle("null_init_manager")->ready();
  }
  void updateWatcher(const Init::Watcher&) override {}
  void dumpUnreadyTargets(envoy::admin::v3::UnreadyTargetsDumps&) override {}
};

class WorkerSafeFactoryContext : public Server::Configuration::ServerFactoryContext {
public:
  explicit WorkerSafeFactoryContext(Server::Configuration::ServerFactoryContext& parent)
      : parent_(parent) {}

  ProtobufMessage::ValidationVisitor& messageValidationVisitor() override {
    return ProtobufMessage::getNullValidationVisitor();
  }
  Init::Manager& initManager() override { return null_init_manager_; }

  Stats::Scope& scope() override { return parent_.scope(); }
  Stats::Scope& serverScope() override { return parent_.serverScope(); }
  Singleton::Manager& singletonManager() override { return parent_.singletonManager(); }
  ThreadLocal::Instance& threadLocal() override { return parent_.threadLocal(); }
  Secret::SecretManager& secretManager() override { return parent_.secretManager(); }
  Upstream::ClusterManager& clusterManager() override { return parent_.clusterManager(); }
  Envoy::Config::XdsManager& xdsManager() override { return parent_.xdsManager(); }
  Http::HttpServerPropertiesCacheManager& httpServerPropertiesCacheManager() override {
    return parent_.httpServerPropertiesCacheManager();
  }
  Event::Dispatcher& mainThreadDispatcher() override { return parent_.mainThreadDispatcher(); }
  const Server::Options& options() override { return parent_.options(); }
  const LocalInfo::LocalInfo& localInfo() const override { return parent_.localInfo(); }
  ProtobufMessage::ValidationContext& messageValidationContext() override {
    return parent_.messageValidationContext();
  }
  Envoy::Runtime::Loader& runtime() override { return parent_.runtime(); }
  OptRef<Server::Admin> admin() override { return parent_.admin(); }
  TimeSource& timeSource() override { return parent_.timeSource(); }
  AccessLog::AccessLogManager& accessLogManager() override { return parent_.accessLogManager(); }
  Api::Api& api() override { return parent_.api(); }
  Http::Context& httpContext() override { return parent_.httpContext(); }
  Grpc::Context& grpcContext() override { return parent_.grpcContext(); }
  Router::Context& routerContext() override { return parent_.routerContext(); }
  ProcessContextOptRef processContext() override { return parent_.processContext(); }
  Envoy::Server::DrainManager& drainManager() override { return parent_.drainManager(); }
  Server::ServerLifecycleNotifier& lifecycleNotifier() override {
    return parent_.lifecycleNotifier();
  }
  Regex::Engine& regexEngine() override { return parent_.regexEngine(); }
  Server::Configuration::StatsConfig& statsConfig() override { return parent_.statsConfig(); }
  envoy::config::bootstrap::v3::Bootstrap& bootstrap() override { return parent_.bootstrap(); }
  Server::OverloadManager& overloadManager() override { return parent_.overloadManager(); }
  Server::OverloadManager& nullOverloadManager() override { return parent_.nullOverloadManager(); }
  bool healthCheckFailed() const override { return parent_.healthCheckFailed(); }
  Ssl::ContextManager& sslContextManager() override { return parent_.sslContextManager(); }

private:
  Server::Configuration::ServerFactoryContext& parent_;
  NullInitManager null_init_manager_;
};

template <int... Fields> constexpr uint64_t makeFieldMask() {
  static_assert(((Fields > 0 && Fields < 64) && ...),
                "Field numbers in bitmask must be in range [1, 63]");
  return ((uint64_t{1} << Fields) | ... | uint64_t{0});
}

bool hasUnrecognizedFields(const Protobuf::Message& message, uint64_t allowed_mask) {
#if defined(ENVOY_ENABLE_FULL_PROTOS)
  const auto* reflection = message.GetReflection();
  std::vector<const Protobuf::FieldDescriptor*> set_fields;
  reflection->ListFields(message, &set_fields);
  for (const auto* field : set_fields) {
    const int num = field->number();
    if (num <= 0 || num >= 64 || (allowed_mask & (uint64_t{1} << num)) == 0) {
      return true;
    }
  }
  return !reflection->GetUnknownFields(message).empty();
#else
  UNREFERENCED_PARAMETER(message);
  UNREFERENCED_PARAMETER(allowed_mask);
  return true;
#endif
}

bool isMainThreadOnlyTypeUrl(absl::string_view type_url) {
  const absl::string_view type_name = TypeUtil::typeUrlToDescriptorFullName(type_url);
  return absl::StrContains(type_name, ".dynamic_modules.") ||
         absl::StrContains(type_name, ".filters.http.on_demand.") ||
         absl::StrContains(type_name, ".formatter.file_content.");
}

bool isStructWrappedMainThreadOnlyFilterConfig(absl::string_view inner_type_url,
                                               const Protobuf::Struct& value) {
  if (isMainThreadOnlyTypeUrl(inner_type_url)) {
    return true;
  }
  if (TypeUtil::typeUrlToDescriptorFullName(inner_type_url) !=
      "envoy.config.route.v3.FilterConfig") {
    return false;
  }
  const auto& fields = value.fields();
  if (const auto disabled_it = fields.find("disabled");
      disabled_it != fields.end() && disabled_it->second.bool_value()) {
    return false;
  }
  const auto config_it = fields.find("config");
  if (config_it == fields.end() || !config_it->second.has_struct_value()) {
    return false;
  }
  const auto& config_fields = config_it->second.struct_value().fields();
  const auto type_it = config_fields.find("@type");
  return type_it != config_fields.end() && isMainThreadOnlyTypeUrl(type_it->second.string_value());
}

bool isMainThreadOnlyAny(const Protobuf::Any& any) {
  if (any.type_url().empty()) {
    return false;
  }
  if (isMainThreadOnlyTypeUrl(any.type_url())) {
    return true;
  }
  const absl::string_view type_name = TypeUtil::typeUrlToDescriptorFullName(any.type_url());
  if (type_name == "xds.type.v3.TypedStruct") {
    xds::type::v3::TypedStruct typed_struct;
    return MessageUtil::unpackTo(any, typed_struct).ok() &&
           isStructWrappedMainThreadOnlyFilterConfig(typed_struct.type_url(), typed_struct.value());
  }
  if (type_name == "udpa.type.v1.TypedStruct") {
    udpa::type::v1::TypedStruct typed_struct;
    return MessageUtil::unpackTo(any, typed_struct).ok() &&
           isStructWrappedMainThreadOnlyFilterConfig(typed_struct.type_url(), typed_struct.value());
  }
  return false;
}

template <typename ExtensionConfig> bool isMainThreadOnlyExtension(const ExtensionConfig& ext) {
  return absl::StrContains(ext.name(), "dynamic_modules") ||
         ext.name() == "envoy.filters.http.on_demand" ||
         ext.name() == "envoy.formatter.file_content" ||
         (ext.has_typed_config() && isMainThreadOnlyAny(ext.typed_config()));
}

bool hasMainThreadOnlyPerFilterConfig(
    const Protobuf::Map<std::string, Protobuf::Any>& typed_configs) {
  for (const auto& [name, any] : typed_configs) {
    UNREFERENCED_PARAMETER(name);
    if (TypeUtil::typeUrlToDescriptorFullName(any.type_url()) ==
        "envoy.config.route.v3.FilterConfig") {
      envoy::config::route::v3::FilterConfig filter_config;
      if (MessageUtil::unpackTo(any, filter_config).ok() && !filter_config.disabled() &&
          filter_config.has_config() && isMainThreadOnlyAny(filter_config.config())) {
        return true;
      }
    } else if (isMainThreadOnlyAny(any)) {
      return true;
    }
  }
  return false;
}

bool routeRequiresMainThreadInit(const envoy::config::route::v3::Route& route) {
  if (!route.route_specifiers().empty()) {
    return true;
  }
  if (route.has_direct_response()) {
    if (route.direct_response().has_body() &&
        Envoy::Config::DataSource::usesFileWatching(route.direct_response().body(), {})) {
      return true;
    }
    if (route.direct_response().has_body_format() &&
        absl::c_any_of(route.direct_response().body_format().formatters(),
                       [](const auto& ext) { return isMainThreadOnlyExtension(ext); })) {
      return true;
    }
  }
  if (hasMainThreadOnlyPerFilterConfig(route.typed_per_filter_config())) {
    return true;
  }
  if (route.has_route()) {
    if (route.route().has_inline_cluster_specifier_plugin() &&
        route.route().inline_cluster_specifier_plugin().has_extension() &&
        isMainThreadOnlyExtension(route.route().inline_cluster_specifier_plugin().extension())) {
      return true;
    }
    if (route.route().has_weighted_clusters()) {
      for (const auto& cluster : route.route().weighted_clusters().clusters()) {
        if (hasMainThreadOnlyPerFilterConfig(cluster.typed_per_filter_config())) {
          return true;
        }
      }
    }
  }
  return false;
}

bool predicateRequiresMainThreadInit(
    const xds::type::matcher::v3::Matcher::MatcherList::Predicate& pred) {
  switch (pred.match_type_case()) {
  case xds::type::matcher::v3::Matcher_MatcherList_Predicate::kSinglePredicate: {
    const auto& sp = pred.single_predicate();
    if (sp.has_input() && isMainThreadOnlyExtension(sp.input())) {
      return true;
    }
    if (sp.has_custom_match() && isMainThreadOnlyExtension(sp.custom_match())) {
      return true;
    }
    if (sp.has_value_match() && sp.value_match().has_custom() &&
        isMainThreadOnlyExtension(sp.value_match().custom())) {
      return true;
    }
    return false;
  }
  case xds::type::matcher::v3::Matcher_MatcherList_Predicate::kOrMatcher:
    return absl::c_any_of(pred.or_matcher().predicate(), predicateRequiresMainThreadInit);
  case xds::type::matcher::v3::Matcher_MatcherList_Predicate::kAndMatcher:
    return absl::c_any_of(pred.and_matcher().predicate(), predicateRequiresMainThreadInit);
  case xds::type::matcher::v3::Matcher_MatcherList_Predicate::kNotMatcher:
    return predicateRequiresMainThreadInit(pred.not_matcher());
  case xds::type::matcher::v3::Matcher_MatcherList_Predicate::MATCH_TYPE_NOT_SET:
    return false;
  }
  return false;
}

bool matcherRequiresMainThreadInit(const xds::type::matcher::v3::Matcher& matcher);

bool onMatchRequiresMainThreadInit(const xds::type::matcher::v3::Matcher::OnMatch& on_match) {
  switch (on_match.on_match_case()) {
  case xds::type::matcher::v3::Matcher_OnMatch::kMatcher:
    return matcherRequiresMainThreadInit(on_match.matcher());
  case xds::type::matcher::v3::Matcher_OnMatch::kAction: {
    if (!on_match.action().has_typed_config()) {
      return false;
    }
    const auto& typed_config = on_match.action().typed_config();
    const absl::string_view type_name =
        TypeUtil::typeUrlToDescriptorFullName(typed_config.type_url());
    if (type_name == "envoy.config.route.v3.Route") {
      envoy::config::route::v3::Route route;
      return MessageUtil::unpackTo(typed_config, route).ok() && routeRequiresMainThreadInit(route);
    }
    if (type_name == "envoy.config.route.v3.RouteList") {
      envoy::config::route::v3::RouteList route_list;
      return MessageUtil::unpackTo(typed_config, route_list).ok() &&
             absl::c_any_of(route_list.routes(), routeRequiresMainThreadInit);
    }
    // Custom match tree actions or TypedStruct-encoded actions receive RouteActionContext (with
    // Init::Manager&), so conservatively initialize eagerly on the main thread.
    return true;
  }
  case xds::type::matcher::v3::Matcher_OnMatch::ON_MATCH_NOT_SET:
    return false;
  }
  return false;
}

bool matcherRequiresMainThreadInit(const xds::type::matcher::v3::Matcher& matcher) {
  if (matcher.has_matcher_list()) {
    for (const auto& field_matcher : matcher.matcher_list().matchers()) {
      if ((field_matcher.has_predicate() &&
           predicateRequiresMainThreadInit(field_matcher.predicate())) ||
          (field_matcher.has_on_match() &&
           onMatchRequiresMainThreadInit(field_matcher.on_match()))) {
        return true;
      }
    }
  } else if (matcher.has_matcher_tree()) {
    const auto& tree = matcher.matcher_tree();
    if (tree.has_input() && isMainThreadOnlyExtension(tree.input())) {
      return true;
    }
    if (tree.has_exact_match_map()) {
      for (const auto& [key, on_match] : tree.exact_match_map().map()) {
        UNREFERENCED_PARAMETER(key);
        if (onMatchRequiresMainThreadInit(on_match)) {
          return true;
        }
      }
    } else if (tree.has_prefix_match_map()) {
      for (const auto& [key, on_match] : tree.prefix_match_map().map()) {
        UNREFERENCED_PARAMETER(key);
        if (onMatchRequiresMainThreadInit(on_match)) {
          return true;
        }
      }
    } else if (tree.has_custom_match()) {
      return true;
    }
  }
  return matcher.has_on_no_match() && onMatchRequiresMainThreadInit(matcher.on_no_match());
}

} // namespace

IsolatedValidationContext::IsolatedValidationContext(
    Server::Configuration::ServerFactoryContext& parent_context)
    : parent_context_(parent_context), isolated_store_(parent_context.scope().symbolTable()),
      isolated_scope_(isolated_store_.rootScope()),
      tracking_singleton_manager_(parent_context.singletonManager(), used_main_thread_resource_),
      tracking_thread_local_(parent_context.threadLocal(), used_main_thread_resource_) {}

Singleton::InstanceSharedPtr IsolatedValidationContext::TrackingSingletonManager::get(
    const std::string& name, Singleton::SingletonFactoryCb cb, bool pin) {
  used_main_thread_resource_ = true;
  auto instance = local_manager_.get(name, std::move(cb), pin);
  if (instance != nullptr) {
    return instance;
  }
  return parent_.get(name, []() -> Singleton::InstanceSharedPtr { return nullptr; }, false);
}

ThreadLocal::SlotSharedPtr IsolatedValidationContext::TrackingThreadLocalInstance::allocateSlot() {
  used_main_thread_resource_ = true;
  return std::make_shared<NoOpSlot>();
}

ScopedValidationServerContext::ScopedValidationServerContext(
    IsolatedValidationContext& validation_context)
    : proxy_(validation_context),
      prev_(Server::Configuration::ServerFactoryContextInstance::getExisting()) {
  if (prev_ != nullptr) {
    Server::Configuration::ServerFactoryContextInstance::clear();
    Server::Configuration::ServerFactoryContextInstance::initialize(&proxy_);
  }
}

ScopedValidationServerContext::~ScopedValidationServerContext() {
  if (prev_ != nullptr) {
    Server::Configuration::ServerFactoryContextInstance::clear();
    Server::Configuration::ServerFactoryContextInstance::initialize(prev_);
  }
}

bool requiresMainThreadInit(const envoy::config::route::v3::VirtualHost& vhost_proto) {
  if (!vhost_proto.route_specifiers().empty() ||
      hasMainThreadOnlyPerFilterConfig(vhost_proto.typed_per_filter_config())) {
    return true;
  }
  if (vhost_proto.has_matcher() && matcherRequiresMainThreadInit(vhost_proto.matcher())) {
    return true;
  }
  return absl::c_any_of(vhost_proto.routes(), routeRequiresMainThreadInit);
}

bool requiresProbeValidation(const envoy::config::route::v3::VirtualHost& vhost_proto,
                             const CommonConfigSharedPtr& global_route_config,
                             bool validate_clusters,
                             Server::Configuration::ServerFactoryContext& factory_context) {
  if (global_route_config == nullptr || requiresMainThreadInit(vhost_proto)) {
    return true;
  }

  constexpr uint64_t kAllowedVhostFields =
      makeFieldMask<envoy::config::route::v3::VirtualHost::kNameFieldNumber,
                    envoy::config::route::v3::VirtualHost::kDomainsFieldNumber,
                    envoy::config::route::v3::VirtualHost::kRoutesFieldNumber,
                    envoy::config::route::v3::VirtualHost::kRequireTlsFieldNumber>();

  if (hasUnrecognizedFields(vhost_proto, kAllowedVhostFields)) {
    return true;
  }

  if (!envoy::config::route::v3::VirtualHost_TlsRequirementType_IsValid(
          vhost_proto.require_tls())) {
    return true;
  }

  if (validate_clusters) {
    for (const auto& shadow_policy : global_route_config->shadowPolicies()) {
      if (!shadow_policy->cluster().empty() &&
          !factory_context.clusterManager().hasCluster(shadow_policy->cluster())) {
        return true;
      }
    }
  }

  constexpr uint64_t kAllowedRouteFields =
      makeFieldMask<envoy::config::route::v3::Route::kNameFieldNumber,
                    envoy::config::route::v3::Route::kMatchFieldNumber,
                    envoy::config::route::v3::Route::kRouteFieldNumber>();

  constexpr uint64_t kAllowedMatchFields =
      makeFieldMask<envoy::config::route::v3::RouteMatch::kPrefixFieldNumber,
                    envoy::config::route::v3::RouteMatch::kPathFieldNumber,
                    envoy::config::route::v3::RouteMatch::kPathSeparatedPrefixFieldNumber,
                    envoy::config::route::v3::RouteMatch::kCaseSensitiveFieldNumber>();

  constexpr uint64_t kAllowedActionFields =
      makeFieldMask<envoy::config::route::v3::RouteAction::kClusterFieldNumber,
                    envoy::config::route::v3::RouteAction::kClusterNotFoundResponseCodeFieldNumber,
                    envoy::config::route::v3::RouteAction::kPriorityFieldNumber,
                    envoy::config::route::v3::RouteAction::kTimeoutFieldNumber,
                    envoy::config::route::v3::RouteAction::kIdleTimeoutFieldNumber,
                    envoy::config::route::v3::RouteAction::kFlushTimeoutFieldNumber,
                    envoy::config::route::v3::RouteAction::kMaxGrpcTimeoutFieldNumber,
                    envoy::config::route::v3::RouteAction::kGrpcTimeoutOffsetFieldNumber,
                    envoy::config::route::v3::RouteAction::kMaxStreamDurationFieldNumber,
                    envoy::config::route::v3::RouteAction::kInternalRedirectActionFieldNumber>();

  constexpr uint64_t kAllowedMaxStreamDurationFields = makeFieldMask<
      envoy::config::route::v3::RouteAction::MaxStreamDuration::kMaxStreamDurationFieldNumber,
      envoy::config::route::v3::RouteAction::MaxStreamDuration::kGrpcTimeoutHeaderMaxFieldNumber,
      envoy::config::route::v3::RouteAction::MaxStreamDuration::
          kGrpcTimeoutHeaderOffsetFieldNumber>();

  for (const auto& route : vhost_proto.routes()) {
    if (route.action_case() != envoy::config::route::v3::Route::ActionCase::kRoute ||
        hasUnrecognizedFields(route, kAllowedRouteFields)) {
      return true;
    }

    if (route.match().path_specifier_case() ==
            envoy::config::route::v3::RouteMatch::PathSpecifierCase::PATH_SPECIFIER_NOT_SET ||
        hasUnrecognizedFields(route.match(), kAllowedMatchFields)) {
      return true;
    }

    const auto& route_action = route.route();
    if (route_action.cluster_specifier_case() !=
            envoy::config::route::v3::RouteAction::ClusterSpecifierCase::kCluster ||
        hasUnrecognizedFields(route_action, kAllowedActionFields)) {
      return true;
    }

    if (route_action.cluster().empty()) {
      return true;
    }

    if (validate_clusters && !factory_context.clusterManager().hasCluster(route_action.cluster())) {
      return true;
    }

    if (!envoy::config::route::v3::RouteAction_ClusterNotFoundResponseCode_IsValid(
            route_action.cluster_not_found_response_code()) ||
        !envoy::config::core::v3::RoutingPriority_IsValid(route_action.priority())) {
      return true;
    }

    if (route_action.internal_redirect_action() !=
        envoy::config::route::v3::RouteAction::PASS_THROUGH_INTERNAL_REDIRECT) {
      return true;
    }

    if ((route_action.has_timeout() &&
         !DurationUtil::durationToMillisecondsNoThrow(route_action.timeout()).ok()) ||
        (route_action.has_idle_timeout() &&
         !DurationUtil::durationToMillisecondsNoThrow(route_action.idle_timeout()).ok()) ||
        (route_action.has_flush_timeout() &&
         !DurationUtil::durationToMillisecondsNoThrow(route_action.flush_timeout()).ok()) ||
        (route_action.has_max_grpc_timeout() &&
         !DurationUtil::durationToMillisecondsNoThrow(route_action.max_grpc_timeout()).ok()) ||
        (route_action.has_grpc_timeout_offset() &&
         !DurationUtil::durationToMillisecondsNoThrow(route_action.grpc_timeout_offset()).ok())) {
      return true;
    }

    if (route_action.has_max_stream_duration()) {
      const auto& msd = route_action.max_stream_duration();
      if (hasUnrecognizedFields(msd, kAllowedMaxStreamDurationFields) ||
          (msd.has_max_stream_duration() &&
           !DurationUtil::durationToMillisecondsNoThrow(msd.max_stream_duration()).ok()) ||
          (msd.has_grpc_timeout_header_max() &&
           !DurationUtil::durationToMillisecondsNoThrow(msd.grpc_timeout_header_max()).ok()) ||
          (msd.has_grpc_timeout_header_offset() &&
           !DurationUtil::durationToMillisecondsNoThrow(msd.grpc_timeout_header_offset()).ok())) {
        return true;
      }
    }
  }

  return false;
}

DomainEntry::~DomainEntry() = default;

void DomainEntry::initEager(VirtualHostImplConstSharedPtr vhost) {
  active_vhost_ = std::move(vhost);
  is_inflated_.store(true, std::memory_order_relaxed);
}

void DomainEntry::initDeferred(const envoy::config::route::v3::VirtualHost* vhost_proto) {
  deferred_vhost_proto_ = vhost_proto;
}

VirtualHostImplConstSharedPtr
DomainEntry::inflateVirtualHost(const CommonConfigSharedPtr& global_route_config,
                                Server::Configuration::ServerFactoryContext& factory_context,
                                const Stats::ScopeSharedPtr& vhost_scope) const {
  if (deferred_vhost_proto_ == nullptr) {
    return nullptr;
  }

  absl::MutexLock lock(init_mutex_);
  if (is_inflated_.load(std::memory_order_relaxed)) {
    return active_vhost_;
  }

  struct ScopedWorkerContext {
    ScopedWorkerContext(Server::Configuration::ServerFactoryContext& parent,
                        Stats::ScopeSharedPtr vhost_scope)
        : context(parent), scope(std::move(vhost_scope)) {}
    WorkerSafeFactoryContext context;
    Stats::ScopeSharedPtr scope;
  };
  auto worker_ctx = std::make_shared<ScopedWorkerContext>(factory_context, vhost_scope);
  Stats::ScopeSharedPtr aliased_scope(worker_ctx, worker_ctx->scope.get());

  absl::Status creation_status = absl::OkStatus();
  auto vhost = std::make_shared<VirtualHostImpl>(
      *deferred_vhost_proto_, global_route_config, worker_ctx->context, std::move(aliased_scope),
      ProtobufMessage::getNullValidationVisitor(), worker_ctx->context.initManager(),
      /*validate_clusters=*/false, creation_status);
  if (!creation_status.ok()) {
    ENVOY_LOG(error, "Failed to initialize deferred virtual host '{}': {}",
              deferred_vhost_proto_->name(), creation_status.message());
    return nullptr;
  }

  active_vhost_ = std::move(vhost);
  is_inflated_.store(true, std::memory_order_release);
  return active_vhost_;
}

} // namespace Router
} // namespace Envoy
