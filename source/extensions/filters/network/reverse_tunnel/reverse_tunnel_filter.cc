#include "source/extensions/filters/network/reverse_tunnel/reverse_tunnel_filter.h"

#include <algorithm>

#include "envoy/buffer/buffer.h"
#include "envoy/config/core/v3/substitution_format_string.pb.h"
#include "envoy/event/timer.h"
#include "envoy/extensions/filters/http/jwt_authn/v3/config.pb.h"
#include "envoy/formatter/http_formatter_context.h"
#include "envoy/init/manager.h"
#include "envoy/network/connection.h"
#include "envoy/server/overload/overload_manager.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/lock_guard.h"
#include "source/common/common/thread.h"
#include "source/common/config/datasource.h"
#include "source/common/formatter/substitution_format_string.h"
#include "source/common/formatter/substitution_formatter.h"
#include "source/common/http/codes.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/headers.h"
#include "source/common/http/http1/codec_impl.h"
#include "source/common/http/utility.h"
#include "source/common/init/target_impl.h"
#include "source/common/jwt/check_audience.h"
#include "source/common/jwt/jwt.h"
#include "source/common/jwt/status.h"
#include "source/common/jwt/verify.h"
#include "source/common/network/connection_socket_impl.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/router/retry_policy_impl.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/common/tracing/null_span_impl.h"
#include "source/extensions/bootstrap/reverse_tunnel/common/reverse_connection_utility.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor_extension.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/upstream_socket_manager.h"
#include "source/extensions/filters/http/common/jwks_fetcher.h"
#include "source/server/generic_factory_context.h"

#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ReverseTunnel {

namespace {

Extensions::Bootstrap::ReverseConnection::UpstreamSocketManager* getThreadLocalSocketManager() {
  const auto* acceptor = getAcceptor();
  if (acceptor == nullptr) {
    return nullptr;
  }
  auto* tls_registry = acceptor->getLocalRegistry();
  if (tls_registry == nullptr) {
    return nullptr;
  }
  return tls_registry->socketManager();
}

class RequestDecoderHandleImpl : public Http::RequestDecoderHandle {
public:
  explicit RequestDecoderHandleImpl(Http::RequestDecoder& decoder) : decoder_(decoder) {}
  OptRef<Http::RequestDecoder> get() override { return decoder_; }

private:
  Http::RequestDecoder& decoder_;
};

// These are the default intervals for the remote JWKS background refresh. They match jwt_authn's
// async fetcher.
constexpr std::chrono::seconds DefaultCacheDuration{600};
constexpr std::chrono::seconds RefetchBeforeExpired{5};
constexpr std::chrono::seconds DefaultFailedRefetch{1};
// This is the shortest interval the cache refresh or the failed refetch may use. The proto allows
// smaller values, down to zero, which would start a new fetch immediately and repeat without pause.
constexpr std::chrono::milliseconds MinRefetchInterval{1000};
// After this many failures in a row, log at warning level again instead of staying at debug, so
// that a long JWKS outage keeps showing up in the log instead of going quiet after the first line.
constexpr uint32_t WarnEveryFailures{60};

// Builds the retry policy for a remote_jwks fetch, or a null policy when none is configured.
absl::StatusOr<Router::RetryPolicyConstSharedPtr> buildRemoteJwksRetryPolicy(
    const envoy::extensions::filters::http::jwt_authn::v3::RemoteJwks& remote_jwks,
    Server::Configuration::ServerFactoryContext& server_context) {
  if (!remote_jwks.has_retry_policy()) {
    return Router::RetryPolicyConstSharedPtr{};
  }
  if (absl::Status status = Http::Utility::validateCoreRetryPolicy(remote_jwks.retry_policy());
      !status.ok()) {
    return status;
  }
  const envoy::config::route::v3::RetryPolicy route_retry_policy =
      Http::Utility::convertCoreToRouteRetryPolicy(remote_jwks.retry_policy(),
                                                   "5xx,gateway-error,connect-failure,reset");
  auto policy_or_error = Router::RetryPolicyImpl::create(
      route_retry_policy, ProtobufMessage::getNullValidationVisitor(), server_context);
  if (!policy_or_error.status().ok()) {
    return policy_or_error.status();
  }
  return Router::RetryPolicyConstSharedPtr{std::move(policy_or_error.value())};
}

} // namespace

// Counters for the remote JWKS background fetcher. They live on the config and are updated on the
// main thread, unlike the per-connection handshake counters in ReverseTunnelStats.
#define ALL_REVERSE_TUNNEL_JWKS_STATS(COUNTER)                                                     \
  COUNTER(jwt_jwks_fetch_success)                                                                  \
  COUNTER(jwt_jwks_fetch_failed)

struct ReverseTunnelJwksStats {
  ALL_REVERSE_TUNNEL_JWKS_STATS(GENERATE_COUNTER_STRUCT)
};

// Fetches a remote JWKS in the background, at startup and then every cache_duration, so that
// verification during the handshake stays synchronous. This is like jwt_authn's JwksAsyncFetcher,
// except it always fetches because the filter never fetches on demand, and it keeps its own copy of
// the keys that worker threads can read safely.
class JwtRemoteJwksProvider : public Logger::Loggable<Logger::Id::filter>,
                              public Extensions::HttpFilters::Common::JwksFetcher::JwksReceiver {
public:
  JwtRemoteJwksProvider(
      const envoy::extensions::filters::http::jwt_authn::v3::RemoteJwks& remote_jwks,
      Router::RetryPolicyConstSharedPtr retry_policy,
      Server::Configuration::ServerFactoryContext& server_context, Init::Manager& init_manager,
      Stats::Scope& scope, JwksFetcherFactory create_fetcher_fn)
      : remote_jwks_(remote_jwks), retry_policy_(std::move(retry_policy)),
        server_context_(server_context), create_fetcher_fn_(std::move(create_fetcher_fn)),
        stats_{
            ALL_REVERSE_TUNNEL_JWKS_STATS(POOL_COUNTER_PREFIX(scope, "reverse_tunnel.handshake."))},
        debug_name_(
            absl::StrCat("reverse_tunnel jwt remote_jwks uri=", remote_jwks_.http_uri().uri())) {
    good_refetch_duration_ = getCacheDuration(remote_jwks_);
    if (good_refetch_duration_ > RefetchBeforeExpired) {
      good_refetch_duration_ -= RefetchBeforeExpired;
    }
    failed_refetch_duration_ =
        remote_jwks_.has_async_fetch() && remote_jwks_.async_fetch().has_failed_refetch_duration()
            ? std::max(MinRefetchInterval,
                       std::chrono::milliseconds(DurationUtil::durationToMilliseconds(
                           remote_jwks_.async_fetch().failed_refetch_duration())))
            : std::chrono::duration_cast<std::chrono::milliseconds>(DefaultFailedRefetch);

    refetch_timer_ =
        server_context_.mainThreadDispatcher().createTimer([this]() -> void { fetch(); });

    // The handshake never fetches on demand, so the keys are only refreshed in the background. By
    // default the listener waits for the first fetch through the init target. Setting
    // async_fetch.fast_listener skips that wait. Either way the wait ends after the first attempt,
    // whether it succeeds or not. If it fails, the listener still starts and rejects every
    // handshake until a later fetch succeeds.
    if (remote_jwks_.has_async_fetch() && remote_jwks_.async_fetch().fast_listener()) {
      fetch();
      return;
    }
    init_target_ = std::make_unique<Init::TargetImpl>(debug_name_, [this]() -> void { fetch(); });
    init_manager.add(*init_target_);
  }

  // The most recently fetched keys, or nullptr before the first successful fetch. Worker threads
  // read this during the handshake and the main thread updates it when a fetch completes.
  JwksConstSharedPtr currentJwks() {
    Thread::LockGuard lock(mutex_);
    return jwks_;
  }

  // These implement JwksFetcher::JwksReceiver. Do not free fetcher_ inside them. The fetcher calls
  // these callbacks and then resets itself, so freeing it here would crash. The next fetch replaces
  // it after cancelling the old one.
  void onJwksSuccess(JwtVerify::JwksPtr&& jwks) override {
    // create_fetcher_fn_ is a public seam. A real fetcher only reports success with valid keys.
    ASSERT(jwks != nullptr && jwks->getStatus() == JwtVerify::Status::Ok);
    {
      Thread::LockGuard lock(mutex_);
      jwks_ = std::move(jwks);
    }
    ENVOY_LOG(debug, "{}: fetched", debug_name_);
    consecutive_failures_ = 0;
    handleFetchDone();
    refetch_timer_->enableTimer(good_refetch_duration_);
    stats_.jwt_jwks_fetch_success_.inc();
  }
  void onJwksError(Failure) override {
    if (consecutive_failures_ % WarnEveryFailures == 0) {
      ENVOY_LOG(warn, "{}: fetch failed", debug_name_);
    } else {
      ENVOY_LOG(debug, "{}: fetch failed", debug_name_);
    }
    ++consecutive_failures_;
    handleFetchDone();
    refetch_timer_->enableTimer(failed_refetch_duration_);
    stats_.jwt_jwks_fetch_failed_.inc();
  }

private:
  static std::chrono::milliseconds
  getCacheDuration(const envoy::extensions::filters::http::jwt_authn::v3::RemoteJwks& remote_jwks) {
    if (!remote_jwks.has_cache_duration()) {
      return std::chrono::duration_cast<std::chrono::milliseconds>(DefaultCacheDuration);
    }
    // Work in milliseconds rather than whole seconds so that a value like 1.5 seconds is not
    // rounded down to 1 second.
    return std::max(MinRefetchInterval,
                    std::chrono::milliseconds(
                        DurationUtil::durationToMilliseconds(remote_jwks.cache_duration())));
  }

  void fetch() {
    if (fetcher_ != nullptr) {
      fetcher_->cancel();
    }
    ENVOY_LOG(debug, "{}: starting fetch", debug_name_);
    fetcher_ = create_fetcher_fn_(server_context_.clusterManager(), retry_policy_, remote_jwks_);
    fetcher_->fetch(Tracing::NullSpan::instance(), *this);
  }

  void handleFetchDone() {
    if (init_target_ != nullptr) {
      init_target_->ready();
      init_target_.reset();
    }
  }

  // This keeps its own copy because ReverseTunnelFilterConfig does not hold the proto config after
  // create() returns.
  const envoy::extensions::filters::http::jwt_authn::v3::RemoteJwks remote_jwks_;
  const Router::RetryPolicyConstSharedPtr retry_policy_;
  Server::Configuration::ServerFactoryContext& server_context_;
  const JwksFetcherFactory create_fetcher_fn_;
  ReverseTunnelJwksStats stats_;

  Thread::MutexBasicLockable mutex_;
  JwksConstSharedPtr jwks_ ABSL_GUARDED_BY(mutex_);

  std::chrono::milliseconds good_refetch_duration_;
  std::chrono::milliseconds failed_refetch_duration_;
  // The number of failures since the last success. Only touched on the main thread.
  uint32_t consecutive_failures_{0};
  Event::TimerPtr refetch_timer_;
  Extensions::HttpFilters::Common::JwksFetcherPtr fetcher_;
  std::unique_ptr<Init::TargetImpl> init_target_;
  const std::string debug_name_;
};

// Stats helper implementation.
ReverseTunnelFilter::ReverseTunnelStats
ReverseTunnelFilter::ReverseTunnelStats::generateStats(const std::string& prefix,
                                                       Stats::Scope& scope) {
  return {ALL_REVERSE_TUNNEL_HANDSHAKE_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
}

// ReverseTunnelFilterConfig implementation.
absl::StatusOr<std::shared_ptr<ReverseTunnelFilterConfig>> ReverseTunnelFilterConfig::create(
    const envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel& proto_config,
    Server::Configuration::FactoryContext& context, JwksFetcherFactory create_fetcher_fn) {

  Formatter::FormatterConstSharedPtr node_id_formatter;
  Formatter::FormatterConstSharedPtr cluster_id_formatter;
  Formatter::FormatterConstSharedPtr tenant_id_formatter;

  // Create formatters for validation if configured.
  if (proto_config.has_validation()) {
    Server::GenericFactoryContextImpl generic_context(context.serverFactoryContext(),
                                                      context.messageValidationVisitor());

    const auto& validation = proto_config.validation();

    // Create node_id formatter if configured.
    if (!validation.node_id_format().empty()) {
      envoy::config::core::v3::SubstitutionFormatString node_id_format_config;
      node_id_format_config.mutable_text_format_source()->set_inline_string(
          validation.node_id_format());

      auto formatter_or_error = Formatter::SubstitutionFormatStringUtils::fromProtoConfig(
          node_id_format_config, generic_context);
      if (!formatter_or_error.ok()) {
        return absl::InvalidArgumentError(fmt::format("Failed to parse node_id_format: {}",
                                                      formatter_or_error.status().message()));
      }
      node_id_formatter = std::move(formatter_or_error.value());
    }

    // Create cluster_id formatter if configured.
    if (!validation.cluster_id_format().empty()) {
      envoy::config::core::v3::SubstitutionFormatString cluster_id_format_config;
      cluster_id_format_config.mutable_text_format_source()->set_inline_string(
          validation.cluster_id_format());

      auto formatter_or_error = Formatter::SubstitutionFormatStringUtils::fromProtoConfig(
          cluster_id_format_config, generic_context);
      if (!formatter_or_error.ok()) {
        return absl::InvalidArgumentError(fmt::format("Failed to parse cluster_id_format: {}",
                                                      formatter_or_error.status().message()));
      }
      cluster_id_formatter = std::move(formatter_or_error.value());
    }

    // Create tenant_id formatter if configured.
    if (!validation.tenant_id_format().empty()) {
      envoy::config::core::v3::SubstitutionFormatString tenant_id_format_config;
      tenant_id_format_config.mutable_text_format_source()->set_inline_string(
          validation.tenant_id_format());

      auto formatter_or_error = Formatter::SubstitutionFormatStringUtils::fromProtoConfig(
          tenant_id_format_config, generic_context);
      if (!formatter_or_error.ok()) {
        return absl::InvalidArgumentError(fmt::format("Failed to parse tenant_id_format: {}",
                                                      formatter_or_error.status().message()));
      }
      tenant_id_formatter = std::move(formatter_or_error.value());
    }
  }

  // Set up the JWKS for handshake JWT authentication if it is configured. This runs here rather
  // than in the constructor so that a bad configuration fails at config load time. An inline
  // local_jwks is parsed now, and a remote_jwks starts a background fetcher. Verification during
  // the handshake stays synchronous in both cases.
  using JwtProto = envoy::extensions::filters::network::reverse_tunnel::v3::JwtHandshakeValidation;
  JwksConstSharedPtr jwt_local_jwks;
  std::unique_ptr<JwtRemoteJwksProvider> jwt_remote_provider;
  if (proto_config.has_jwt_validation()) {
    const auto& jwt = proto_config.jwt_validation();
    // An issuer is required. Without one, a validly signed token from any issuer would be accepted.
    if (jwt.issuer().empty()) {
      return absl::InvalidArgumentError("reverse_tunnel jwt_validation: `issuer` is required");
    }
    switch (jwt.jwks_source_specifier_case()) {
    case JwtProto::kLocalJwks: {
      auto jwks_or_error = Config::DataSource::read(jwt.local_jwks(), /*allow_empty=*/false,
                                                    context.serverFactoryContext().api());
      if (!jwks_or_error.ok()) {
        return absl::InvalidArgumentError(
            fmt::format("reverse_tunnel jwt_validation: failed to load local_jwks: {}",
                        jwks_or_error.status().message()));
      }
      auto jwks = JwtVerify::Jwks::createFrom(jwks_or_error.value(), JwtVerify::Jwks::JWKS);
      if (jwks->getStatus() != JwtVerify::Status::Ok) {
        return absl::InvalidArgumentError(
            fmt::format("reverse_tunnel jwt_validation: invalid local_jwks: {}",
                        JwtVerify::getStatusString(jwks->getStatus())));
      }
      jwt_local_jwks = std::move(jwks);
      break;
    }
    case JwtProto::kRemoteJwks: {
      auto retry_policy_or_error =
          buildRemoteJwksRetryPolicy(jwt.remote_jwks(), context.serverFactoryContext());
      if (!retry_policy_or_error.ok()) {
        return absl::InvalidArgumentError(
            fmt::format("reverse_tunnel jwt_validation: invalid remote_jwks retry_policy: {}",
                        retry_policy_or_error.status().message()));
      }
      jwt_remote_provider = std::make_unique<JwtRemoteJwksProvider>(
          jwt.remote_jwks(), std::move(retry_policy_or_error.value()),
          context.serverFactoryContext(), context.initManager(), context.scope(),
          create_fetcher_fn
              ? std::move(create_fetcher_fn)
              : JwksFetcherFactory(&Extensions::HttpFilters::Common::JwksFetcher::create));
      break;
    }
    case JwtProto::JWKS_SOURCE_SPECIFIER_NOT_SET:
      return absl::InvalidArgumentError(
          "reverse_tunnel jwt_validation: a JWKS source (local_jwks or remote_jwks) is required");
    }
  }

  return std::shared_ptr<ReverseTunnelFilterConfig>(new ReverseTunnelFilterConfig(
      proto_config, std::move(node_id_formatter), std::move(cluster_id_formatter),
      std::move(tenant_id_formatter), std::move(jwt_local_jwks), std::move(jwt_remote_provider)));
}

ReverseTunnelFilterConfig::ReverseTunnelFilterConfig(
    const envoy::extensions::filters::network::reverse_tunnel::v3::ReverseTunnel& proto_config,
    Formatter::FormatterConstSharedPtr node_id_formatter,
    Formatter::FormatterConstSharedPtr cluster_id_formatter,
    Formatter::FormatterConstSharedPtr tenant_id_formatter, JwksConstSharedPtr jwt_local_jwks,
    std::unique_ptr<JwtRemoteJwksProvider> jwt_remote_provider)
    : ping_interval_(proto_config.has_ping_interval()
                         ? std::chrono::milliseconds(
                               DurationUtil::durationToMilliseconds(proto_config.ping_interval()))
                         : std::chrono::milliseconds(2000)),
      auto_close_connections_(proto_config.auto_close_connections()),
      request_path_(
          proto_config.request_path().empty()
              ? std::string(::Envoy::Extensions::Bootstrap::ReverseConnection::
                                ReverseConnectionUtility::DEFAULT_REVERSE_TUNNEL_REQUEST_PATH)
              : proto_config.request_path()),
      request_method_string_([&proto_config]() -> std::string {
        envoy::config::core::v3::RequestMethod method = proto_config.request_method();
        if (method == envoy::config::core::v3::METHOD_UNSPECIFIED) {
          method = envoy::config::core::v3::GET;
        }
        return envoy::config::core::v3::RequestMethod_Name(method);
      }()),
      node_id_formatter_(std::move(node_id_formatter)),
      cluster_id_formatter_(std::move(cluster_id_formatter)),
      tenant_id_formatter_(std::move(tenant_id_formatter)),
      emit_dynamic_metadata_(proto_config.has_validation() &&
                             proto_config.validation().emit_dynamic_metadata()),
      dynamic_metadata_namespace_(
          proto_config.has_validation() &&
                  !proto_config.validation().dynamic_metadata_namespace().empty()
              ? proto_config.validation().dynamic_metadata_namespace()
              : "envoy.filters.network.reverse_tunnel"),
      required_cluster_name_(proto_config.required_cluster_name()),
      use_http_upgrade_(proto_config.use_http_upgrade()),
      skip_rebalancing_(proto_config.skip_rebalancing()),
      enable_connection_limit_(proto_config.enable_connection_limit()),
      jwt_enabled_(proto_config.has_jwt_validation()),
      jwt_required_(proto_config.has_jwt_validation() &&
                    !proto_config.jwt_validation().allow_missing_or_failed()),
      jwt_issuer_(proto_config.has_jwt_validation() ? proto_config.jwt_validation().issuer()
                                                    : std::string()),
      jwt_audiences_([&proto_config]() {
        std::vector<std::string> audiences;
        if (proto_config.has_jwt_validation()) {
          audiences.assign(proto_config.jwt_validation().audiences().begin(),
                           proto_config.jwt_validation().audiences().end());
        }
        return JwtVerify::CheckAudience(audiences);
      }()),
      jwt_clock_skew_seconds_(proto_config.has_jwt_validation() &&
                                      proto_config.jwt_validation().clock_skew_seconds() > 0
                                  ? proto_config.jwt_validation().clock_skew_seconds()
                                  : JwtVerify::kClockSkewInSecond),
      jwt_token_header_(proto_config.has_jwt_validation() &&
                                !proto_config.jwt_validation().token_header().empty()
                            ? Http::LowerCaseString(proto_config.jwt_validation().token_header())
                            : Http::LowerCaseString("authorization")),
      jwt_claims_namespace_(
          proto_config.has_jwt_validation() &&
                  !proto_config.jwt_validation().claims_metadata_namespace().empty()
              ? proto_config.jwt_validation().claims_metadata_namespace()
              : "envoy.filters.network.reverse_tunnel.jwt"),
      jwt_local_jwks_(std::move(jwt_local_jwks)),
      jwt_remote_provider_(std::move(jwt_remote_provider)) {}

ReverseTunnelFilterConfig::~ReverseTunnelFilterConfig() = default;

JwksConstSharedPtr ReverseTunnelFilterConfig::currentJwks() const {
  if (jwt_remote_provider_ != nullptr) {
    return jwt_remote_provider_->currentJwks();
  }
  return jwt_local_jwks_;
}

bool ReverseTunnelFilterConfig::validateConnectionLimit(absl::string_view node_id,
                                                        absl::string_view tenant_id) const {
  if (!enable_connection_limit_) {
    return true;
  }

  if (auto socket_manager = getThreadLocalSocketManager()) {
    return socket_manager->canAcceptConnection(node_id, tenant_id);
  }
  ENVOY_LOG(warn,
            "reverse_tunnel: no socket manager found with connection limit enabled, rejecting.");
  return false;
}

bool ReverseTunnelFilterConfig::validateIdentifiers(
    absl::string_view node_id, absl::string_view cluster_id, absl::string_view tenant_id,
    const Http::RequestHeaderMap& request_headers,
    const StreamInfo::StreamInfo& stream_info) const {

  if (!validateConnectionLimit(node_id, tenant_id)) {
    ENVOY_LOG(debug, "reverse_tunnel: connection limit reached. node_id: {}, tenant_id: {}",
              node_id, tenant_id);
    return false;
  }

  // If no validation configured, pass validation.
  if (!node_id_formatter_ && !cluster_id_formatter_ && !tenant_id_formatter_) {
    return true;
  }

  // Give the formatter the parsed handshake headers so validation strings can read them with
  // %REQ(...)%, and %DYNAMIC_METADATA(namespace:claim)% can pick up any JWT claims published
  // earlier in the handshake.
  const Formatter::Context context(&request_headers);

  // Validate node_id if formatter is configured.
  if (node_id_formatter_) {
    const std::string expected_node_id = node_id_formatter_->format(context, stream_info);
    if (!expected_node_id.empty() && expected_node_id != node_id) {
      ENVOY_LOG(debug, "reverse_tunnel: node_id validation failed. Expected: '{}', Actual: '{}'",
                expected_node_id, node_id);
      return false;
    }
  }

  // Validate cluster_id if formatter is configured.
  if (cluster_id_formatter_) {
    const std::string expected_cluster_id = cluster_id_formatter_->format(context, stream_info);
    if (!expected_cluster_id.empty() && expected_cluster_id != cluster_id) {
      ENVOY_LOG(debug, "reverse_tunnel: cluster_id validation failed. Expected: '{}', Actual: '{}'",
                expected_cluster_id, cluster_id);
      return false;
    }
  }

  // Validate tenant_id if formatter is configured.
  if (tenant_id_formatter_) {
    const std::string expected_tenant_id = tenant_id_formatter_->format(context, stream_info);
    if (!expected_tenant_id.empty() && expected_tenant_id != tenant_id) {
      ENVOY_LOG(debug, "reverse_tunnel: tenant_id validation failed. Expected: '{}', Actual: '{}'",
                expected_tenant_id, tenant_id);
      return false;
    }
  }

  return true;
}

// TODO(kanurag94): this verifies the handshake token inline, on top of //source/common/jwt. If
// another synchronous L4 caller ever needs the same thing, move it into a shared helper rather than
// copying it.
bool ReverseTunnelFilterConfig::verifyHandshakeJwt(const Http::RequestHeaderMap& headers,
                                                   StreamInfo::StreamInfo& stream_info) const {
  ASSERT(jwt_enabled_);

  // Extract the token from the configured header.
  const auto token_values = headers.get(jwt_token_header_);
  if (token_values.empty()) {
    ENVOY_LOG(debug, "reverse_tunnel: jwt: missing token header '{}'", jwt_token_header_.get());
    return false;
  }
  // A well-formed client sends one token header. If there are several, use the first (as jwt_authn
  // does) and just note it at debug.
  if (token_values.size() > 1) {
    ENVOY_LOG(debug, "reverse_tunnel: jwt: {} values on token header '{}'; using the first",
              token_values.size(), jwt_token_header_.get());
  }
  absl::string_view token = token_values[0]->value().getStringView();
  // Strip a leading "Bearer " prefix when present (case-insensitive), matching jwt_authn.
  static constexpr absl::string_view bearer_prefix = "Bearer ";
  if (absl::StartsWithIgnoreCase(token, bearer_prefix)) {
    token = token.substr(bearer_prefix.size());
  }

  JwtVerify::Jwt jwt;
  if (jwt.parseFromString(std::string(token)) != JwtVerify::Status::Ok) {
    ENVOY_LOG(debug, "reverse_tunnel: jwt: token parse failed");
    return false;
  }

  // Verify the signature and the time constraints, meaning exp and nbf, against the configured JWKS
  // before trusting any claim in the payload. With remote_jwks the keys may not be fetched yet, or
  // a refresh may be failing with nothing cached. Treat either case as a verification failure.
  const JwksConstSharedPtr jwks = currentJwks();
  if (jwks == nullptr) {
    ENVOY_LOG(debug, "reverse_tunnel: jwt: no JWKS available yet");
    return false;
  }
  const uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                           stream_info.timeSource().systemTime().time_since_epoch())
                           .count();
  const JwtVerify::Status status = JwtVerify::verifyJwt(jwt, *jwks, now, jwt_clock_skew_seconds_);
  if (status != JwtVerify::Status::Ok) {
    ENVOY_LOG(debug, "reverse_tunnel: jwt: verification failed: {}",
              JwtVerify::getStatusString(status));
    return false;
  }

  // An expiration is required. A token without exp would otherwise be valid forever.
  if (jwt.exp_ == 0) {
    ENVOY_LOG(debug, "reverse_tunnel: jwt: token has no `exp` claim");
    return false;
  }

  // The issuer must match. Checked after signature verification so the payload can be trusted.
  if (jwt.iss_ != jwt_issuer_) {
    ENVOY_LOG(debug, "reverse_tunnel: jwt: issuer mismatch (expected '{}', got '{}')", jwt_issuer_,
              jwt.iss_);
    return false;
  }

  // Check the audience, normalized (scheme / trailing slash) like jwt_authn. Passes when no
  // audience is configured.
  if (!jwt_audiences_.areAudiencesAllowed(jwt.audiences_)) {
    ENVOY_LOG(debug, "reverse_tunnel: jwt: audience not allowed");
    return false;
  }

  // The token is verified. Publish the claims as dynamic metadata so the validation block can match
  // a claimed identifier against a real claim with %DYNAMIC_METADATA(namespace:claim)%. This runs
  // just before validateIdentifiers(), which is where those bindings are read.
  stream_info.setDynamicMetadata(jwt_claims_namespace_, jwt.payload_pb_);
  ENVOY_LOG(debug, "reverse_tunnel: jwt: verified; claims published to namespace '{}'",
            jwt_claims_namespace_);
  return true;
}

void ReverseTunnelFilterConfig::emitValidationMetadata(absl::string_view node_id,
                                                       absl::string_view cluster_id,
                                                       absl::string_view tenant_id,
                                                       bool validation_passed,
                                                       StreamInfo::StreamInfo& stream_info) const {
  if (!emit_dynamic_metadata_) {
    return;
  }

  Protobuf::Struct metadata;
  auto& fields = *metadata.mutable_fields();

  // Emit actual identifiers.
  fields["node_id"].set_string_value(std::string(node_id));
  fields["cluster_id"].set_string_value(std::string(cluster_id));
  fields["tenant_id"].set_string_value(std::string(tenant_id));

  // Emit validation result.
  fields["validation_result"].set_string_value(validation_passed ? "allowed" : "denied");

  // Set dynamic metadata on the stream info.
  stream_info.setDynamicMetadata(dynamic_metadata_namespace_, metadata);

  ENVOY_LOG(trace,
            "reverse_tunnel: emitted dynamic metadata to namespace '{}': node_id={}, "
            "cluster_id={}, tenant_id={}, validation_result={}",
            dynamic_metadata_namespace_, node_id, cluster_id, tenant_id,
            validation_passed ? "allowed" : "denied");
}

// ReverseTunnelFilter implementation.
ReverseTunnelFilter::ReverseTunnelFilter(ReverseTunnelFilterConfigSharedPtr config,
                                         Stats::Scope& stats_scope,
                                         Server::OverloadManager& overload_manager)
    : config_(std::move(config)), stats_scope_(stats_scope), overload_manager_(overload_manager),
      stats_(ReverseTunnelStats::generateStats("reverse_tunnel.handshake.", stats_scope_)) {}

Network::FilterStatus ReverseTunnelFilter::onNewConnection() {
  ENVOY_CONN_LOG(debug, "reverse_tunnel: new connection established",
                 read_callbacks_->connection());
  return Network::FilterStatus::Continue;
}

Network::FilterStatus ReverseTunnelFilter::onData(Buffer::Instance& data, bool) {
  if (!codec_) {
    Http::Http1Settings http1_settings;
    Http::Http1::CodecStats::AtomicPtr http1_stats_ptr;
    auto& http1_stats = Http::Http1::CodecStats::atomicGet(http1_stats_ptr, stats_scope_);
    codec_ = std::make_unique<Http::Http1::ServerConnectionImpl>(
        read_callbacks_->connection(), http1_stats, *this, http1_settings,
        Http::DEFAULT_MAX_REQUEST_HEADERS_KB, Http::DEFAULT_MAX_HEADERS_COUNT,
        envoy::config::core::v3::HttpProtocolOptions::ALLOW, overload_manager_);
  }

  const Http::Status status = codec_->dispatch(data);
  if (!status.ok()) {
    ENVOY_CONN_LOG(debug, "reverse_tunnel: codec dispatch error: {}", read_callbacks_->connection(),
                   status.message());
    // Close connection on codec error.
    read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
    return Network::FilterStatus::StopIteration;
  }
  return Network::FilterStatus::StopIteration;
}

void ReverseTunnelFilter::initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) {
  read_callbacks_ = &callbacks;
}

Http::RequestDecoder& ReverseTunnelFilter::newStream(Http::ResponseEncoder& response_encoder,
                                                     bool) {
  active_decoder_ = std::make_unique<RequestDecoderImpl>(*this, response_encoder);
  return *active_decoder_;
}

// Private methods.

// RequestDecoderImpl
void ReverseTunnelFilter::RequestDecoderImpl::decodeHeaders(
    Http::RequestHeaderMapSharedPtr&& headers, bool end_stream) {
  headers_ = std::move(headers);
  // For an Upgrade request, the HTTP/1 server codec calls decodeHeaders with
  // end_stream=false because it now considers the connection a tunnel awaiting
  // more bytes. The handshake has no body, so process the headers immediately
  // rather than waiting for an end-of-stream that never comes.
  if (end_stream || Http::Utility::isUpgrade(*headers_)) {
    processIfComplete(true);
  }
}

void ReverseTunnelFilter::RequestDecoderImpl::decodeData(Buffer::Instance& data, bool end_stream) {
  body_.add(data);
  if (end_stream) {
    processIfComplete(true);
  }
}

void ReverseTunnelFilter::RequestDecoderImpl::decodeTrailers(Http::RequestTrailerMapPtr&&) {
  processIfComplete(true);
}

void ReverseTunnelFilter::RequestDecoderImpl::decodeMetadata(Http::MetadataMapPtr&&) {}

void ReverseTunnelFilter::RequestDecoderImpl::sendLocalReply(
    Http::Code code, absl::string_view body,
    const std::function<void(Http::ResponseHeaderMap& headers)>& modify_headers,
    const std::optional<Grpc::Status::GrpcStatus>, absl::string_view) {
  auto headers = Http::ResponseHeaderMapImpl::create();
  headers->setStatus(static_cast<uint64_t>(code));
  headers->setReferenceContentType(Http::Headers::get().ContentTypeValues.Text);
  if (modify_headers) {
    modify_headers(*headers);
  }
  const bool end_stream = body.empty();
  encoder_.encodeHeaders(*headers, end_stream);
  if (!end_stream) {
    Buffer::OwnedImpl buf(body);
    encoder_.encodeData(buf, true);
  }
}

StreamInfo::StreamInfo& ReverseTunnelFilter::RequestDecoderImpl::streamInfo() {
  return stream_info_;
}

AccessLog::InstanceSharedPtrVector ReverseTunnelFilter::RequestDecoderImpl::accessLogHandlers() {
  return {};
}

Http::RequestDecoderHandlePtr ReverseTunnelFilter::RequestDecoderImpl::getRequestDecoderHandle() {
  return std::make_unique<RequestDecoderHandleImpl>(*this);
}

void ReverseTunnelFilter::RequestDecoderImpl::processIfComplete(bool end_stream) {
  if (!end_stream || complete_) {
    return;
  }
  complete_ = true;

  // Validate method/path.
  const absl::string_view method = headers_->getMethodValue();
  const absl::string_view path = headers_->getPathValue();
  ENVOY_LOG(trace,
            "ReverseTunnelFilter::RequestDecoderImpl::processIfComplete: method: {}, path: {}",
            method, path);
  if (!absl::EqualsIgnoreCase(method, parent_.config_->requestMethod()) ||
      path != parent_.config_->requestPath()) {
    sendLocalReply(Http::Code::NotFound, "Not a reverse tunnel request", nullptr, std::nullopt,
                   "reverse_tunnel_not_found");
    // Close the connection after sending the response.
    parent_.read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
    return;
  }

  // When upgrade negotiation is enabled, require the request to advertise the
  // `reverse-tunnel` upgrade. The HTTP/1 server codec already validates the
  // `Connection: Upgrade` paired token, so we only re-check the `Upgrade` value here.
  if (parent_.config_->useHttpUpgrade()) {
    const auto upgrade = headers_->getUpgradeValue();
    if (!absl::EqualsIgnoreCase(upgrade, Bootstrap::ReverseConnection::ReverseConnectionUtility::
                                             REVERSE_TUNNEL_UPGRADE_PROTOCOL)) {
      parent_.stats_.parse_error_.inc();
      ENVOY_CONN_LOG(debug,
                     "reverse_tunnel: upgrade negotiation enabled but Upgrade header missing or "
                     "unexpected (got '{}')",
                     parent_.read_callbacks_->connection(), upgrade);
      sendLocalReply(
          Http::Code::UpgradeRequired, "Upgrade: reverse-tunnel required",
          [](Http::ResponseHeaderMap& h) {
            h.setReferenceKey(Http::Headers::get().Upgrade,
                              Bootstrap::ReverseConnection::ReverseConnectionUtility::
                                  REVERSE_TUNNEL_UPGRADE_PROTOCOL);
          },
          std::nullopt, "reverse_tunnel_upgrade_required");
      parent_.read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
      return;
    }
  }

  // Extract node/cluster/tenant identifiers from HTTP headers.
  const auto node_vals =
      headers_->get(Extensions::Bootstrap::ReverseConnection::reverseTunnelNodeIdHeader());
  const auto cluster_vals =
      headers_->get(Extensions::Bootstrap::ReverseConnection::reverseTunnelClusterIdHeader());
  const auto tenant_vals =
      headers_->get(Extensions::Bootstrap::ReverseConnection::reverseTunnelTenantIdHeader());

  if (node_vals.empty() || cluster_vals.empty() || tenant_vals.empty()) {
    parent_.stats_.parse_error_.inc();
    ENVOY_CONN_LOG(debug, "reverse_tunnel: missing required headers (node/cluster/tenant)",
                   parent_.read_callbacks_->connection());
    sendLocalReply(Http::Code::BadRequest, "Missing required reverse tunnel headers", nullptr,
                   std::nullopt, "reverse_tunnel_missing_headers");
    // Close the connection after sending the response.
    parent_.read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
    return;
  }

  const absl::string_view node_id = node_vals[0]->value().getStringView();
  const absl::string_view cluster_id = cluster_vals[0]->value().getStringView();
  const absl::string_view tenant_id = tenant_vals[0]->value().getStringView();

  // Get tenant isolation setting from socket manager (configured at bootstrap level).
  bool tenant_isolation_enabled = false;
  if (auto* socket_manager = getThreadLocalSocketManager()) {
    tenant_isolation_enabled = socket_manager->tenantIsolationEnabled();
  }

  if (tenant_isolation_enabled) {
    const absl::string_view delimiter = ReverseTunnelFilterConfig::tenantDelimiter();
    const auto contains_delimiter = [&](absl::string_view value) -> bool {
      return value.find(delimiter) != absl::string_view::npos;
    };
    if (contains_delimiter(node_id) || contains_delimiter(cluster_id) ||
        contains_delimiter(tenant_id)) {
      parent_.stats_.parse_error_.inc();
      ENVOY_CONN_LOG(debug,
                     "reverse_tunnel: identifier contains reserved delimiter '{}' while tenant "
                     "isolation is enabled",
                     parent_.read_callbacks_->connection(), delimiter);
      sendLocalReply(
          Http::Code::BadRequest,
          fmt::format("Reverse tunnel identifiers must not contain '{}' when tenant isolation is "
                      "enabled",
                      delimiter),
          nullptr, std::nullopt, "reverse_tunnel_invalid_identifier");
      parent_.read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
      return;
    }
  }

  // Check for upstream cluster name header and validate if required.
  if (!parent_.config_->requiredClusterName().empty()) {
    const auto upstream_cluster_vals = headers_->get(
        Extensions::Bootstrap::ReverseConnection::reverseTunnelUpstreamClusterNameHeader());

    if (upstream_cluster_vals.empty()) {
      parent_.stats_.parse_error_.inc();
      ENVOY_CONN_LOG(
          debug, "reverse_tunnel: missing upstream cluster name header when enforcement is enabled",
          parent_.read_callbacks_->connection());
      sendLocalReply(Http::Code::BadRequest, "Missing upstream cluster name header", nullptr,
                     std::nullopt, "reverse_tunnel_missing_cluster_name_header");
      parent_.read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
      return;
    }

    const absl::string_view upstream_cluster_name =
        upstream_cluster_vals[0]->value().getStringView();
    if (upstream_cluster_name != parent_.config_->requiredClusterName()) {
      parent_.stats_.validation_failed_.inc();
      ENVOY_CONN_LOG(debug,
                     "reverse_tunnel: upstream cluster name mismatch. Expected: '{}', Actual: '{}'",
                     parent_.read_callbacks_->connection(), parent_.config_->requiredClusterName(),
                     upstream_cluster_name);
      sendLocalReply(Http::Code::BadRequest, "Cluster name mismatch", nullptr, std::nullopt,
                     "reverse_tunnel_cluster_mismatch");
      parent_.read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
      return;
    }
  }

  auto& connection = parent_.read_callbacks_->connection();

  // Authenticate the handshake's bearer token (if configured) before validation and before the
  // socket is registered, so a forged or expired token can never yield a usable reverse tunnel.
  // On success this publishes the verified claims as dynamic metadata for %DYNAMIC_METADATA%
  // binding in the validation block below.
  if (parent_.config_->jwtEnabled() &&
      !parent_.config_->verifyHandshakeJwt(*headers_, connection.streamInfo())) {
    if (parent_.config_->jwtRequired()) {
      parent_.stats_.jwt_denied_.inc();
      ENVOY_CONN_LOG(debug, "reverse_tunnel: jwt authentication failed", connection);
      sendLocalReply(Http::Code::Unauthorized, "JWT authentication failed", nullptr, std::nullopt,
                     "reverse_tunnel_jwt_denied");
      connection.close(Network::ConnectionCloseType::FlushWrite);
      return;
    }
    // In audit mode (allow_missing_or_failed) count what enforcement would have rejected but let
    // the handshake proceed. No claims were published, so any %DYNAMIC_METADATA% binding will not
    // match.
    parent_.stats_.jwt_would_deny_.inc();
    ENVOY_CONN_LOG(debug, "reverse_tunnel: jwt authentication failed (audit mode, allowing)",
                   connection);
  }

  // Validate node_id, cluster_id, and tenant_id if validation is configured.
  const bool validation_passed = parent_.config_->validateIdentifiers(
      node_id, cluster_id, tenant_id, *headers_, connection.streamInfo());

  // Emit validation metadata if configured.
  parent_.config_->emitValidationMetadata(node_id, cluster_id, tenant_id, validation_passed,
                                          connection.streamInfo());

  if (!validation_passed) {
    parent_.stats_.validation_failed_.inc();
    ENVOY_CONN_LOG(debug,
                   "reverse_tunnel: validation failed for node '{}', cluster '{}', tenant '{}'",
                   parent_.read_callbacks_->connection(), node_id, cluster_id, tenant_id);
    sendLocalReply(Http::Code::Forbidden, "Validation failed", nullptr, std::nullopt,
                   "reverse_tunnel_validation_failed");
    parent_.read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
    return;
  }

  // Respond. In upgrade mode we send `101 Switching Protocols` with the upgrade headers
  // echoed; the HTTP/1 server codec (created with allow_upgrade) flushes them and stops
  // parsing further bytes as HTTP, leaving the spliced connection for the tunnel.
  auto resp_headers = Http::ResponseHeaderMapImpl::create();
  if (parent_.config_->useHttpUpgrade()) {
    resp_headers->setStatus(101);
    resp_headers->setReferenceKey(Http::Headers::get().Connection,
                                  Http::Headers::get().ConnectionValues.Upgrade);
    resp_headers->setReferenceKey(
        Http::Headers::get().Upgrade,
        Bootstrap::ReverseConnection::ReverseConnectionUtility::REVERSE_TUNNEL_UPGRADE_PROTOCOL);
  } else {
    resp_headers->setStatus(200);
  }
  encoder_.encodeHeaders(*resp_headers, true);

  // Extract DP-side tunnel initiation timestamp if present. Defaults to 0 (absent).
  int64_t initiation_time_ms = 0;
  const auto initiation_time_vals =
      headers_->get(Bootstrap::ReverseConnection::reverseTunnelInitiationTimeHeader());
  if (!initiation_time_vals.empty()) {
    if (!absl::SimpleAtoi(initiation_time_vals[0]->value().getStringView(), &initiation_time_ms)) {
      ENVOY_CONN_LOG(warn, "reverse_tunnel: failed to parse initiation-time header value '{}'",
                     parent_.read_callbacks_->connection(),
                     initiation_time_vals[0]->value().getStringView());
      initiation_time_ms = 0;
    }
  }

  parent_.processAcceptedConnection(node_id, cluster_id, tenant_id, initiation_time_ms);
  parent_.stats_.accepted_.inc();

  // Close the listener-side connection so tunnel bytes go to the duped fd, not back into
  // this filter's codec. Required in upgrade mode; opt-in otherwise.
  if (parent_.config_->useHttpUpgrade() || parent_.config_->autoCloseConnections()) {
    auto& connection = parent_.read_callbacks_->connection();
    Bootstrap::ReverseConnection::ReverseConnectionUtility::applySslQuietClose(connection);
    connection.close(Network::ConnectionCloseType::FlushWrite);
  }
}

void ReverseTunnelFilter::processAcceptedConnection(absl::string_view node_id,
                                                    absl::string_view cluster_id,
                                                    absl::string_view tenant_id,
                                                    int64_t initiation_time_ms) {
  ENVOY_CONN_LOG(debug,
                 "reverse_tunnel: connection accepted for node '{}' in cluster '{}' (tenant: '{}')",
                 read_callbacks_->connection(), node_id, cluster_id, tenant_id);

  Network::Connection& connection = read_callbacks_->connection();

  // Lookup the reverse tunnel acceptor socket interface to retrieve the TLS registry.
  // Note: This is a global lookup that should be thread-safe but may return nullptr
  // if the socket interface isn't registered or we're in a test environment.
  auto* socket_manager = getThreadLocalSocketManager();
  if (socket_manager == nullptr) {
    ENVOY_CONN_LOG(debug, "reverse_tunnel: socket manager not available, skipping socket reuse",
                   connection);
    return;
  }

  // Wrap the downstream socket with our custom IO handle to manage its lifecycle.
  const Network::ConnectionSocketPtr& socket = connection.getSocket();
  if (!socket || !socket->isOpen()) {
    ENVOY_CONN_LOG(debug, "reverse_tunnel: original socket not available or not open",
                   read_callbacks_->connection());
    return;
  }

  // Duplicate the original socket's IO handle for reuse.
  Network::IoHandlePtr wrapped_handle = socket->ioHandle().duplicate();
  if (!wrapped_handle || !wrapped_handle->isOpen()) {
    ENVOY_CONN_LOG(error, "reverse_tunnel: failed to duplicate socket handle", connection);
    return;
  }

  // Build a new ConnectionSocket from the duplicated handle, preserving addressing info.
  auto wrapped_socket = std::make_unique<Network::ConnectionSocketImpl>(
      std::move(wrapped_handle), socket->connectionInfoProvider().localAddress(),
      socket->connectionInfoProvider().remoteAddress());

  // Reset file events on the new socket.
  wrapped_socket->ioHandle().resetFileEvents();

  // Convert ping interval to seconds as required by the manager API.
  const std::chrono::seconds ping_seconds =
      std::chrono::duration_cast<std::chrono::seconds>(config_->pingInterval());

  // Register the wrapped socket for reuse under the original identifiers. The socket manager
  // derives any tenant-scoped internal keys itself so lifecycle logging can retain the original
  // node, cluster, and tenant fields.
  // Note: The socket manager is expected to be thread-safe.
  // Get tenant isolation setting from socket manager (configured at bootstrap level).
  const bool tenant_isolation_enabled = socket_manager->tenantIsolationEnabled();
  const std::string socket_node_id =
      tenant_isolation_enabled
          ? Extensions::Bootstrap::ReverseConnection::ReverseConnectionUtility::
                buildTenantScopedIdentifier(tenant_id, node_id)
          : std::string(node_id);
  const std::string socket_cluster_id =
      tenant_isolation_enabled
          ? Extensions::Bootstrap::ReverseConnection::ReverseConnectionUtility::
                buildTenantScopedIdentifier(tenant_id, cluster_id)
          : std::string(cluster_id);

  ENVOY_CONN_LOG(trace, "reverse_tunnel: registering wrapped socket for reuse", connection);
  socket_manager->addConnectionSocket(std::string(node_id), std::string(cluster_id),
                                      std::move(wrapped_socket), ping_seconds,
                                      /* rebalanced= */ config_->skipRebalancing(), tenant_id);
  ENVOY_CONN_LOG(debug, "reverse_tunnel: successfully registered wrapped socket for reuse",
                 connection);

  // Report the connection to the extension -> reporter.
  if (auto extension = socket_manager->getUpstreamExtension()) {
    extension->reportConnection(socket_node_id, socket_cluster_id, tenant_id, initiation_time_ms);
  }
}

} // namespace ReverseTunnel
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
