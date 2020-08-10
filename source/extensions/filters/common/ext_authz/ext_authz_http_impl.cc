#include "extensions/filters/common/ext_authz/ext_authz_http_impl.h"

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.h"
#include "envoy/service/auth/v3/external_auth.pb.h"
#include "envoy/type/matcher/v3/string.pb.h"

#include "common/common/enum_to_int.h"
#include "common/common/fmt.h"
#include "common/common/matchers.h"
#include "common/http/async_client_impl.h"
#include "common/http/codes.h"
#include "common/runtime/runtime_features.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace ExtAuthz {

namespace {

// Static header map used for creating authorization requests.
const Http::HeaderMap& lengthZeroHeader() {
  static const auto headers = Http::createHeaderMap<Http::RequestHeaderMapImpl>(
      {{Http::Headers::get().ContentLength, std::to_string(0)}});
  return *headers;
}

// Static response used for creating authorization ERROR responses.
const Response& errorResponse() {
  CONSTRUCT_ON_FIRST_USE(Response,
                         Response{CheckStatus::Error, Http::HeaderVector{}, Http::HeaderVector{},
                                  Http::HeaderVector{}, EMPTY_STRING, Http::Code::Forbidden,
                                  ProtobufWkt::Struct{}});
}

// SuccessResponse used for creating either DENIED or OK authorization responses.
struct SuccessResponse {
  SuccessResponse(const Http::HeaderMap& headers, const MatcherSharedPtr& matchers,
                  const MatcherSharedPtr& append_matchers, Response&& response)
      : headers_(headers), matchers_(matchers), append_matchers_(append_matchers),
        response_(std::make_unique<Response>(response)) {
    headers_.iterate([this](const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
      // UpstreamHeaderMatcher
      if (matchers_->matches(header.key().getStringView())) {
        response_->headers_to_set.emplace_back(
            Http::LowerCaseString{std::string(header.key().getStringView())},
            std::string(header.value().getStringView()));
      }
      if (append_matchers_->matches(header.key().getStringView())) {
        // If there is an existing matching key in the current headers, the new entry will be
        // appended with the same key. For example, given {"key": "value1"} headers, if there is
        // a matching "key" from the authorization response headers {"key": "value2"}, the
        // request to upstream server will have two entries for "key": {"key": "value1", "key":
        // "value2"}.
        response_->headers_to_add.emplace_back(
            Http::LowerCaseString{std::string(header.key().getStringView())},
            std::string(header.value().getStringView()));
      }
      return Http::HeaderMap::Iterate::Continue;
    });
  }

  const Http::HeaderMap& headers_;
  const MatcherSharedPtr& matchers_;
  const MatcherSharedPtr& append_matchers_;
  ResponsePtr response_;
};

envoy::type::matcher::v3::StringMatcher
ignoreCaseStringMatcher(const envoy::type::matcher::v3::StringMatcher& matcher) {
  const auto& match_pattern_case = matcher.match_pattern_case();
  if (match_pattern_case == envoy::type::matcher::v3::StringMatcher::MatchPatternCase::kSafeRegex ||
      match_pattern_case ==
          envoy::type::matcher::v3::StringMatcher::MatchPatternCase::kHiddenEnvoyDeprecatedRegex) {
    return matcher;
  }

  envoy::type::matcher::v3::StringMatcher ignore_case;
  ignore_case.set_ignore_case(true);
  switch (matcher.match_pattern_case()) {
  case envoy::type::matcher::v3::StringMatcher::MatchPatternCase::kExact:
    ignore_case.set_exact(matcher.exact());
    break;
  case envoy::type::matcher::v3::StringMatcher::MatchPatternCase::kPrefix:
    ignore_case.set_prefix(matcher.prefix());
    break;
  case envoy::type::matcher::v3::StringMatcher::MatchPatternCase::kSuffix:
    ignore_case.set_suffix(matcher.suffix());
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  return ignore_case;
}

std::vector<Matchers::StringMatcherPtr>
createStringMatchers(const envoy::type::matcher::v3::ListStringMatcher& list,
                     const bool disable_lowercase_string_matcher) {
  std::vector<Matchers::StringMatcherPtr> matchers;
  for (const auto& matcher : list.patterns()) {
    matchers.push_back(std::make_unique<Matchers::StringMatcherImpl>(
        disable_lowercase_string_matcher ? matcher : ignoreCaseStringMatcher(matcher)));
  }
  return matchers;
}

} // namespace

// Matchers
HeaderKeyMatcher::HeaderKeyMatcher(std::vector<Matchers::StringMatcherPtr>&& list)
    : matchers_(std::move(list)) {}

bool HeaderKeyMatcher::matches(absl::string_view key) const {
  return std::any_of(matchers_.begin(), matchers_.end(),
                     [&key](auto& matcher) { return matcher->match(key); });
}

NotHeaderKeyMatcher::NotHeaderKeyMatcher(std::vector<Matchers::StringMatcherPtr>&& list)
    : matcher_(std::move(list)) {}

bool NotHeaderKeyMatcher::matches(absl::string_view key) const { return !matcher_.matches(key); }

// Config
ClientConfig::ClientConfig(const envoy::extensions::filters::http::ext_authz::v3::ExtAuthz& config,
                           uint32_t timeout, absl::string_view path_prefix)
    : enable_case_sensitive_string_matcher_(Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.ext_authz_http_service_enable_case_sensitive_string_matcher")),
      request_header_matchers_(
          toRequestMatchers(config.http_service().authorization_request().allowed_headers(),
                            enable_case_sensitive_string_matcher_)),
      client_header_matchers_(
          toClientMatchers(config.http_service().authorization_response().allowed_client_headers(),
                           enable_case_sensitive_string_matcher_)),
      upstream_header_matchers_(toUpstreamMatchers(
          config.http_service().authorization_response().allowed_upstream_headers(),
          enable_case_sensitive_string_matcher_)),
      upstream_header_to_append_matchers_(toUpstreamMatchers(
          config.http_service().authorization_response().allowed_upstream_headers_to_append(),
          enable_case_sensitive_string_matcher_)),
      cluster_name_(config.http_service().server_uri().cluster()), timeout_(timeout),
      path_prefix_(path_prefix),
      tracing_name_(fmt::format("async {} egress", config.http_service().server_uri().cluster())),
      request_headers_parser_(Router::HeaderParser::configure(
          config.http_service().authorization_request().headers_to_add(), false)) {}

MatcherSharedPtr
ClientConfig::toRequestMatchers(const envoy::type::matcher::v3::ListStringMatcher& list,
                                const bool disable_lowercase_string_matcher) {
  const std::vector<Http::LowerCaseString> keys{
      {Http::CustomHeaders::get().Authorization, Http::Headers::get().Method,
       Http::Headers::get().Path, Http::Headers::get().Host}};

  std::vector<Matchers::StringMatcherPtr> matchers(
      createStringMatchers(list, disable_lowercase_string_matcher));
  for (const auto& key : keys) {
    envoy::type::matcher::v3::StringMatcher matcher;
    matcher.set_exact(key.get());
    matchers.push_back(std::make_unique<Matchers::StringMatcherImpl>(matcher));
  }

  return std::make_shared<HeaderKeyMatcher>(std::move(matchers));
}

MatcherSharedPtr
ClientConfig::toClientMatchers(const envoy::type::matcher::v3::ListStringMatcher& list,
                               const bool disable_lowercase_string_matcher) {
  std::vector<Matchers::StringMatcherPtr> matchers(
      createStringMatchers(list, disable_lowercase_string_matcher));

  // If list is empty, all authorization response headers, except Host, should be added to
  // the client response.
  if (matchers.empty()) {
    envoy::type::matcher::v3::StringMatcher matcher;
    matcher.set_exact(Http::Headers::get().Host.get());
    matchers.push_back(std::make_unique<Matchers::StringMatcherImpl>(matcher));

    return std::make_shared<NotHeaderKeyMatcher>(std::move(matchers));
  }

  // If not empty, all user defined matchers and default matcher's list will
  // be used instead.
  std::vector<Http::LowerCaseString> keys{
      {Http::Headers::get().Status, Http::Headers::get().ContentLength,
       Http::Headers::get().WWWAuthenticate, Http::Headers::get().Location}};

  for (const auto& key : keys) {
    envoy::type::matcher::v3::StringMatcher matcher;
    matcher.set_exact(key.get());
    matchers.push_back(std::make_unique<Matchers::StringMatcherImpl>(matcher));
  }

  return std::make_shared<HeaderKeyMatcher>(std::move(matchers));
}

MatcherSharedPtr
ClientConfig::toUpstreamMatchers(const envoy::type::matcher::v3::ListStringMatcher& list,
                                 const bool disable_lowercase_string_matcher) {
  return std::make_unique<HeaderKeyMatcher>(
      createStringMatchers(list, disable_lowercase_string_matcher));
}

RawHttpClientImpl::RawHttpClientImpl(Upstream::ClusterManager& cm, ClientConfigSharedPtr config)
    : cm_(cm), config_(config) {}

RawHttpClientImpl::~RawHttpClientImpl() { ASSERT(callbacks_ == nullptr); }

void RawHttpClientImpl::cancel() {
  ASSERT(callbacks_ != nullptr);
  request_->cancel();
  callbacks_ = nullptr;
}

// Client
void RawHttpClientImpl::check(RequestCallbacks& callbacks,
                              const envoy::service::auth::v3::CheckRequest& request,
                              Tracing::Span& parent_span,
                              const StreamInfo::StreamInfo& stream_info) {
  ASSERT(callbacks_ == nullptr);
  callbacks_ = &callbacks;

  Http::RequestHeaderMapPtr headers;
  const uint64_t request_length = request.attributes().request().http().body().size();
  if (request_length > 0) {
    headers = Http::createHeaderMap<Http::RequestHeaderMapImpl>(
        {{Http::Headers::get().ContentLength, std::to_string(request_length)}});
  } else {
    headers = Http::createHeaderMap<Http::RequestHeaderMapImpl>(lengthZeroHeader());
  }

  for (const auto& header : request.attributes().request().http().headers()) {
    const Http::LowerCaseString key{header.first};
    // Skip setting content-length header since it is already configured at initialization.
    if (key == Http::Headers::get().ContentLength) {
      continue;
    }

    if (config_->requestHeaderMatchers()->matches(key.get())) {
      if (key == Http::Headers::get().Path && !config_->pathPrefix().empty()) {
        headers->addCopy(key, absl::StrCat(config_->pathPrefix(), header.second));
      } else {
        headers->addCopy(key, header.second);
      }
    }
  }

  config_->requestHeaderParser().evaluateHeaders(*headers, stream_info);

  Http::RequestMessagePtr message =
      std::make_unique<Envoy::Http::RequestMessageImpl>(std::move(headers));
  if (request_length > 0) {
    message->body() =
        std::make_unique<Buffer::OwnedImpl>(request.attributes().request().http().body());
  }

  const std::string& cluster = config_->cluster();

  // It's possible that the cluster specified in the filter configuration no longer exists due to a
  // CDS removal.
  if (cm_.get(cluster) == nullptr) {
    // TODO(dio): Add stats related to this.
    ENVOY_LOG(debug, "ext_authz cluster '{}' does not exist", cluster);
    callbacks_->onComplete(std::make_unique<Response>(errorResponse()));
    callbacks_ = nullptr;
  } else {
    auto options = Http::AsyncClient::RequestOptions()
                       .setTimeout(config_->timeout())
                       .setParentSpan(parent_span)
                       .setChildSpanName(config_->tracingName());

    request_ = cm_.httpAsyncClientForCluster(cluster).send(std::move(message), *this, options);
  }
}

void RawHttpClientImpl::onSuccess(const Http::AsyncClient::Request&,
                                  Http::ResponseMessagePtr&& message) {
  callbacks_->onComplete(toResponse(std::move(message)));
  callbacks_ = nullptr;
}

void RawHttpClientImpl::onFailure(const Http::AsyncClient::Request&,
                                  Http::AsyncClient::FailureReason reason) {
  ASSERT(reason == Http::AsyncClient::FailureReason::Reset);
  callbacks_->onComplete(std::make_unique<Response>(errorResponse()));
  callbacks_ = nullptr;
}

void RawHttpClientImpl::onBeforeFinalizeUpstreamSpan(
    Tracing::Span& span, const Http::ResponseHeaderMap* response_headers) {
  if (response_headers != nullptr) {
    const uint64_t status_code = Http::Utility::getResponseStatus(*response_headers);
    span.setTag(TracingConstants::get().HttpStatus,
                Http::CodeUtility::toString(static_cast<Http::Code>(status_code)));
    span.setTag(TracingConstants::get().TraceStatus, status_code == enumToInt(Http::Code::OK)
                                                         ? TracingConstants::get().TraceOk
                                                         : TracingConstants::get().TraceUnauthz);
  }
}

ResponsePtr RawHttpClientImpl::toResponse(Http::ResponseMessagePtr message) {
  const uint64_t status_code = Http::Utility::getResponseStatus(message->headers());

  // Set an error status if the call to the authorization server returns any of the 5xx HTTP error
  // codes. A Forbidden response is sent to the client if the filter has not been configured with
  // failure_mode_allow.
  if (Http::CodeUtility::is5xx(status_code)) {
    return std::make_unique<Response>(errorResponse());
  }

  // Create an Ok authorization response.
  if (status_code == enumToInt(Http::Code::OK)) {
    SuccessResponse ok{message->headers(), config_->upstreamHeaderMatchers(),
                       config_->upstreamHeaderToAppendMatchers(),
                       Response{CheckStatus::OK, Http::HeaderVector{}, Http::HeaderVector{},
                                Http::HeaderVector{}, EMPTY_STRING, Http::Code::OK,
                                ProtobufWkt::Struct{}}};
    return std::move(ok.response_);
  }

  // Create a Denied authorization response.
  SuccessResponse denied{message->headers(), config_->clientHeaderMatchers(),
                         config_->upstreamHeaderToAppendMatchers(),
                         Response{CheckStatus::Denied, Http::HeaderVector{}, Http::HeaderVector{},
                                  Http::HeaderVector{}, message->bodyAsString(),
                                  static_cast<Http::Code>(status_code), ProtobufWkt::Struct{}}};
  return std::move(denied.response_);
}

} // namespace ExtAuthz
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
