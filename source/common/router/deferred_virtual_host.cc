#include "source/common/router/deferred_virtual_host.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/common/router/config_impl.h"

#include "absl/container/flat_hash_set.h"
#include "absl/strings/string_view.h"

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

class WorkerSafeFactoryContext : public DelegatingServerFactoryContext {
public:
  explicit WorkerSafeFactoryContext(Server::Configuration::ServerFactoryContext& parent)
      : DelegatingServerFactoryContext(parent) {}

  ProtobufMessage::ValidationVisitor& messageValidationVisitor() override {
    return ProtobufMessage::getNullValidationVisitor();
  }
  Init::Manager& initManager() override { return null_init_manager_; }

private:
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

#if defined(ENVOY_ENABLE_FULL_PROTOS)
constexpr int kMaxReflectionDepth = 64;

bool isDeferrableTypeName(absl::string_view type_name) {
  static const auto* const deferrable_types = new absl::flat_hash_set<absl::string_view>{
      // Route match tree actions.
      "envoy.config.route.v3.Route",
      "envoy.config.route.v3.RouteList",
      // HTTP and network match inputs.
      "envoy.type.matcher.v3.HttpRequestHeaderMatchInput",
      "envoy.type.matcher.v3.HttpRequestTrailerMatchInput",
      "envoy.type.matcher.v3.HttpResponseHeaderMatchInput",
      "envoy.type.matcher.v3.HttpResponseTrailerMatchInput",
      "envoy.type.matcher.v3.HttpRequestQueryParamMatchInput",
      "envoy.extensions.matching.common_inputs.network.v3.DestinationIPInput",
      "envoy.extensions.matching.common_inputs.network.v3.DestinationPortInput",
      "envoy.extensions.matching.common_inputs.network.v3.SourceIPInput",
      "envoy.extensions.matching.common_inputs.network.v3.SourcePortInput",
      "envoy.extensions.matching.common_inputs.network.v3.DirectSourceIPInput",
      "envoy.extensions.matching.common_inputs.network.v3.SourceTypeInput",
      "envoy.extensions.matching.common_inputs.network.v3.ServerNameInput",
      "envoy.extensions.matching.common_inputs.network.v3.FilterStateInput",
      "envoy.extensions.matching.common_inputs.network.v3.DynamicMetadataInput",
      // Input matchers.
      "envoy.extensions.matching.input_matchers.consistent_hashing.v3.ConsistentHashing",
      "envoy.extensions.matching.input_matchers.ip.v3.Ip",
      "envoy.extensions.matching.input_matchers.runtime_fraction.v3.RuntimeFraction",
      // Path match/rewrite, early-data, internal-redirect, retry, formatter, and cluster-specifier
      // extensions.
      "envoy.extensions.path.match.uri_template.v3.UriTemplateMatchConfig",
      "envoy.extensions.path.rewrite.uri_template.v3.UriTemplateRewriteConfig",
      "envoy.extensions.early_data.v3.DefaultEarlyDataPolicy",
      "envoy.extensions.internal_redirect.allow_listed_routes.v3.AllowListedRoutesConfig",
      "envoy.extensions.internal_redirect.filter_state.v3.FilterStateConfig",
      "envoy.extensions.internal_redirect.previous_routes.v3.PreviousRoutesConfig",
      "envoy.extensions.internal_redirect.safe_cross_scheme.v3.SafeCrossSchemeConfig",
      "envoy.extensions.retry.host.omit_canary_hosts.v3.OmitCanaryHostsPredicate",
      "envoy.extensions.retry.host.omit_host_metadata.v3.OmitHostMetadataConfig",
      "envoy.extensions.retry.host.previous_hosts.v3.PreviousHostsPredicate",
      "envoy.extensions.retry.priority.previous_priorities.v3.PreviousPrioritiesConfig",
      "envoy.extensions.formatter.metadata.v3.Metadata",
      "envoy.extensions.formatter.req_without_query.v3.ReqWithoutQuery",
      "envoy.extensions.http.stateful_session.envelope.v3.EnvelopeSessionState",
      "envoy.extensions.http.stateful_session.header.v3.HeaderBasedSessionState",
      "envoy.extensions.router.cluster_specifiers.matcher.v3.ClusterAction",
      "envoy.extensions.router.cluster_specifiers.matcher.v3.MatcherClusterSpecifier",
      "envoy.extensions.router.cluster_specifiers.priority_group.v3.PriorityGroupClusterSpecifier",
      // Per-route HTTP filter configurations.
      "envoy.extensions.filters.http.buffer.v3.BufferPerRoute",
      "envoy.extensions.filters.http.compressor.v3.CompressorPerRoute",
      "envoy.extensions.filters.http.cors.v3.CorsPolicy",
      "envoy.extensions.filters.http.ext_authz.v3.ExtAuthzPerRoute",
      "envoy.extensions.filters.http.fault.v3.HTTPFault",
      "envoy.extensions.filters.http.grpc_http1_reverse_bridge.v3.FilterConfigPerRoute",
      "envoy.extensions.filters.http.header_mutation.v3.HeaderMutationPerRoute",
      "envoy.extensions.filters.http.ratelimit.v3.RateLimitPerRoute",
      "envoy.extensions.filters.http.set_filter_state.v3.Config",
      "envoy.extensions.filters.http.set_metadata.v3.Config",
      "envoy.extensions.filters.http.stateful_session.v3.StatefulSessionPerRoute",
  };
  return deferrable_types->contains(type_name);
}

bool messageRequiresMainThreadInit(const Protobuf::Message& message, int depth = 0) {
  if (depth > kMaxReflectionDepth) {
    return true;
  }

  const Protobuf::Descriptor* desc = message.GetDescriptor();
  const Protobuf::Reflection* refl = message.GetReflection();
  if (desc == nullptr || refl == nullptr) {
    return true;
  }

  const absl::string_view full_name = desc->full_name();
  // Short-circuit configurations that open filesystem watches, xDS/SDS subscriptions, or ODCDS
  // handles so probe validation never triggers side effects on parent_context_.
  if (full_name == "envoy.config.core.v3.WatchedDirectory" ||
      full_name == "envoy.config.core.v3.ConfigSource" ||
      full_name == "envoy.extensions.filters.http.on_demand.v3.OnDemandCds") {
    return true;
  }

  // Direct JSON Struct/Value/ListValue fields (e.g., Metadata.filter_metadata,
  // SubstitutionFormatString.json_format, SetMetadata) only contain JSON primitives/containers and
  // cannot embed Any, TypedExtensionConfig, WatchedDirectory, or ConfigSource; Any-packed Struct
  // and TypedStruct configs are already rejected before unpacking in the google.protobuf.Any branch
  // below.
  if (full_name == "google.protobuf.Struct" || full_name == "google.protobuf.Value" ||
      full_name == "google.protobuf.ListValue") {
    return false;
  }

  if (full_name == "envoy.config.route.v3.FilterConfig") {
    const auto* disabled_field =
        desc->FindFieldByNumber(envoy::config::route::v3::FilterConfig::kDisabledFieldNumber);
    if (disabled_field != nullptr && refl->GetBool(message, disabled_field)) {
      return false;
    }
    const auto* config_field =
        desc->FindFieldByNumber(envoy::config::route::v3::FilterConfig::kConfigFieldNumber);
    if (config_field == nullptr || !refl->HasField(message, config_field)) {
      return false;
    }
    const Protobuf::Message& config_any = refl->GetMessage(message, config_field);
    const auto* any_desc = config_any.GetDescriptor();
    const auto* any_refl = config_any.GetReflection();
    const auto* type_url_field =
        any_desc != nullptr ? any_desc->FindFieldByNumber(Protobuf::Any::kTypeUrlFieldNumber)
                            : nullptr;
    std::string scratch;
    if (type_url_field != nullptr && any_refl != nullptr &&
        any_refl->GetStringReference(config_any, type_url_field, &scratch).empty()) {
      return false;
    }
    return messageRequiresMainThreadInit(config_any, depth + 1);
  }

  if (full_name == "envoy.config.core.v3.TypedExtensionConfig" ||
      full_name == "xds.core.v3.TypedExtensionConfig" ||
      full_name == "envoy.config.route.v3.RetryPolicy.RetryHostPredicate" ||
      full_name == "envoy.config.route.v3.RetryPolicy.RetryPriority") {
    const auto* typed_config_field = desc->FindFieldByName("typed_config");
    if (typed_config_field == nullptr || !refl->HasField(message, typed_config_field)) {
      return true;
    }
  }

  if (full_name == "google.protobuf.Any") {
    const auto* type_url_field = desc->FindFieldByNumber(Protobuf::Any::kTypeUrlFieldNumber);
    const auto* value_field = desc->FindFieldByNumber(Protobuf::Any::kValueFieldNumber);
    if (type_url_field == nullptr || value_field == nullptr) {
      return true;
    }
    std::string scratch_type_url;
    const absl::string_view type_url =
        refl->GetStringReference(message, type_url_field, &scratch_type_url);
    if (type_url.empty()) {
      return true;
    }
    const absl::string_view inner_type_name = TypeUtil::typeUrlToDescriptorFullName(type_url);
    if (inner_type_name != "envoy.config.route.v3.FilterConfig" &&
        !isDeferrableTypeName(inner_type_name)) {
      return true;
    }
    const Protobuf::Descriptor* inner_desc =
        Protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(inner_type_name);
    if (inner_desc == nullptr) {
      return true;
    }
    const Protobuf::Message* prototype =
        Protobuf::MessageFactory::generated_factory()->GetPrototype(inner_desc);
    if (prototype == nullptr) {
      return true;
    }
    std::unique_ptr<Protobuf::Message> unpacked(prototype->New());
    std::string scratch_value;
    const std::string& value_bytes = refl->GetStringReference(message, value_field, &scratch_value);
    if (!unpacked->ParseFromString(value_bytes)) {
      return true;
    }
    return messageRequiresMainThreadInit(*unpacked, depth + 1);
  }

  std::vector<const Protobuf::FieldDescriptor*> set_fields;
  refl->ListFields(message, &set_fields);
  for (const Protobuf::FieldDescriptor* field : set_fields) {
    if (field->name() == "route_specifiers") {
      return true;
    }
    if (field->cpp_type() != Protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
      continue;
    }
    if (field->is_repeated()) {
      const int count = refl->FieldSize(message, field);
      for (int i = 0; i < count; ++i) {
        if (messageRequiresMainThreadInit(refl->GetRepeatedMessage(message, field, i), depth + 1)) {
          return true;
        }
      }
    } else {
      if (messageRequiresMainThreadInit(refl->GetMessage(message, field), depth + 1)) {
        return true;
      }
    }
  }
  return false;
}
#endif

} // namespace

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

IsolatedValidationContext::IsolatedValidationContext(
    Server::Configuration::ServerFactoryContext& parent_context)
    : DelegatingServerFactoryContext(parent_context),
      isolated_store_(parent_context.scope().symbolTable()),
      isolated_scope_(isolated_store_.rootScope()),
      tracking_singleton_manager_(parent_context, used_main_thread_resource_),
      tracking_thread_local_(parent_context, used_main_thread_resource_) {}

Singleton::InstanceSharedPtr IsolatedValidationContext::TrackingSingletonManager::get(
    const std::string& name, Singleton::SingletonFactoryCb cb, bool pin) {
  used_main_thread_resource_ = true;
  auto instance = local_manager_.get(name, std::move(cb), pin);
  if (instance != nullptr) {
    return instance;
  }
  return parent_context_.singletonManager().get(
      name, []() -> Singleton::InstanceSharedPtr { return nullptr; }, false);
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
#if defined(ENVOY_ENABLE_FULL_PROTOS)
  return messageRequiresMainThreadInit(vhost_proto);
#else
  UNREFERENCED_PARAMETER(vhost_proto);
  return true;
#endif
}

bool requiresProbeValidation(const envoy::config::route::v3::VirtualHost& vhost_proto,
                             const CommonConfigSharedPtr& global_route_config,
                             bool validate_clusters,
                             Server::Configuration::ServerFactoryContext& factory_context) {
  if (global_route_config == nullptr) {
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

} // namespace Router
} // namespace Envoy
