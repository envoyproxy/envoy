#include "extensions/filters/common/ext_authz/ext_authz_http_impl.h"

#include "common/common/enum_to_int.h"
#include "common/http/async_client_impl.h"
#include "common/http/codes.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace ExtAuthz {

namespace {

// Static header map used for creating authorization requests.
const Http::HeaderMap& lengthZeroHeader() {
  CONSTRUCT_ON_FIRST_USE(Http::HeaderMapImpl,
                         {Http::Headers::get().ContentLength, std::to_string(0)});
}

// Static response used for creating authorization ERROR responses.
const Response& errorResponse() {
  CONSTRUCT_ON_FIRST_USE(Response,
                         Response{CheckStatus::Error, Http::HeaderVector{}, Http::HeaderVector{},
                                  EMPTY_STRING, Http::Code::Forbidden});
}

// SuccessResponse used for creating either DENIED or OK authorization responses.
struct SuccessResponse {
  SuccessResponse(const Http::HeaderMap& headers, const MatcherSharedPtr& matchers,
                  Response&& response)
      : headers_(headers), matchers_(matchers), response_(std::make_unique<Response>(response)) {
    headers_.iterate(
        [](const Http::HeaderEntry& header, void* ctx) -> Http::HeaderMap::Iterate {
          auto* context = static_cast<SuccessResponse*>(ctx);
          // UpstreamHeaderMatcher
          if (context->matchers_->matches(header.key().getStringView())) {
            context->response_->headers_to_add.emplace_back(
                Http::LowerCaseString{std::string(header.key().getStringView())},
                std::string(header.value().getStringView()));
          }
          return Http::HeaderMap::Iterate::Continue;
        },
        this);
  }

  const Http::HeaderMap& headers_;
  const MatcherSharedPtr& matchers_;
  ResponsePtr response_;
};

std::vector<Matchers::LowerCaseStringMatcherPtr>
createLowerCaseMatchers(const envoy::type::matcher::ListStringMatcher& list) {
  std::vector<Matchers::LowerCaseStringMatcherPtr> matchers;
  for (const auto& matcher : list.patterns()) {
    matchers.push_back(std::make_unique<Matchers::LowerCaseStringMatcher>(matcher));
  }
  return matchers;
}

} // namespace

// Matchers
HeaderKeyMatcher::HeaderKeyMatcher(std::vector<Matchers::LowerCaseStringMatcherPtr>&& list)
    : matchers_(std::move(list)) {}

bool HeaderKeyMatcher::matches(absl::string_view key) const {
  return std::any_of(matchers_.begin(), matchers_.end(),
                     [&key](auto& matcher) { return matcher->match(key); });
}

NotHeaderKeyMatcher::NotHeaderKeyMatcher(std::vector<Matchers::LowerCaseStringMatcherPtr>&& list)
    : matcher_(std::move(list)) {}

bool NotHeaderKeyMatcher::matches(absl::string_view key) const { return !matcher_.matches(key); }

// Config
ClientConfig::ClientConfig(const envoy::config::filter::http::ext_authz::v2::ExtAuthz& config,
                           uint32_t timeout, absl::string_view path_prefix)
    : request_header_matchers_(
          toRequestMatchers(config.http_service().authorization_request().allowed_headers())),
      client_header_matchers_(toClientMatchers(
          config.http_service().authorization_response().allowed_client_headers())),
      upstream_header_matchers_(toUpstreamMatchers(
          config.http_service().authorization_response().allowed_upstream_headers())),
      authorization_headers_to_add_(
          toHeadersAdd(config.http_service().authorization_request().headers_to_add())),
      cluster_name_(config.http_service().server_uri().cluster()), timeout_(timeout),
      path_prefix_(path_prefix) {}

MatcherSharedPtr
ClientConfig::toRequestMatchers(const envoy::type::matcher::ListStringMatcher& list) {
  const std::vector<Http::LowerCaseString> keys{
      {Http::Headers::get().Authorization, Http::Headers::get().Method, Http::Headers::get().Path,
       Http::Headers::get().Host}};

  std::vector<Matchers::LowerCaseStringMatcherPtr> matchers(createLowerCaseMatchers(list));
  for (const auto& key : keys) {
    envoy::type::matcher::StringMatcher matcher;
    matcher.set_exact(key.get());
    matchers.push_back(std::make_unique<Matchers::LowerCaseStringMatcher>(matcher));
  }

  return std::make_shared<HeaderKeyMatcher>(std::move(matchers));
}

MatcherSharedPtr
ClientConfig::toClientMatchers(const envoy::type::matcher::ListStringMatcher& list) {
  std::vector<Matchers::LowerCaseStringMatcherPtr> matchers(createLowerCaseMatchers(list));

  // If list is empty, all authorization response headers, except Host, should be added to
  // the client response.
  if (matchers.empty()) {
    envoy::type::matcher::StringMatcher matcher;
    matcher.set_exact(Http::Headers::get().Host.get());
    matchers.push_back(std::make_unique<Matchers::LowerCaseStringMatcher>(matcher));

    return std::make_shared<NotHeaderKeyMatcher>(std::move(matchers));
  }

  // If not empty, all user defined matchers and default matcher's list will
  // be used instead.
  std::vector<Http::LowerCaseString> keys{
      {Http::Headers::get().Status, Http::Headers::get().ContentLength,
       Http::Headers::get().WWWAuthenticate, Http::Headers::get().Location}};

  for (const auto& key : keys) {
    envoy::type::matcher::StringMatcher matcher;
    matcher.set_exact(key.get());
    matchers.push_back(std::make_unique<Matchers::LowerCaseStringMatcher>(matcher));
  }

  return std::make_shared<HeaderKeyMatcher>(std::move(matchers));
}

MatcherSharedPtr
ClientConfig::toUpstreamMatchers(const envoy::type::matcher::ListStringMatcher& list) {
  return std::make_unique<HeaderKeyMatcher>(createLowerCaseMatchers(list));
}

Http::LowerCaseStrPairVector ClientConfig::toHeadersAdd(
    const Protobuf::RepeatedPtrField<envoy::api::v2::core::HeaderValue>& headers) {
  Http::LowerCaseStrPairVector header_vec;
  header_vec.reserve(headers.size());
  for (const auto& header : headers) {
    header_vec.emplace_back(Http::LowerCaseString(header.key()), header.value());
  }
  return header_vec;
}

RawHttpClientImpl::RawHttpClientImpl(Upstream::ClusterManager& cm, ClientConfigSharedPtr config)
    : cm_(cm), config_(config) {}

RawHttpClientImpl::~RawHttpClientImpl() { ASSERT(!callbacks_); }

void RawHttpClientImpl::cancel() {
  ASSERT(callbacks_ != nullptr);
  request_->cancel();
  callbacks_ = nullptr;
}

// Client
void RawHttpClientImpl::check(RequestCallbacks& callbacks,
                              const envoy::service::auth::v2::CheckRequest& request,
                              Tracing::Span&) {
  ASSERT(callbacks_ == nullptr);
  callbacks_ = &callbacks;

  Http::HeaderMapPtr headers;
  const uint64_t request_length = request.attributes().request().http().body().size();
  if (request_length > 0) {
    headers =
        std::make_unique<Http::HeaderMapImpl,
                         std::initializer_list<std::pair<Http::LowerCaseString, std::string>>>(
            {{Http::Headers::get().ContentLength, std::to_string(request_length)}});
  } else {
    headers = std::make_unique<Http::HeaderMapImpl>(lengthZeroHeader());
  }

  for (const auto& header : request.attributes().request().http().headers()) {
    const Http::LowerCaseString key{header.first};
    if (config_->requestHeaderMatchers()->matches(key.get())) {
      if (key == Http::Headers::get().Path && !config_->pathPrefix().empty()) {
        std::string value;
        absl::StrAppend(&value, config_->pathPrefix(), header.second);
        headers->addCopy(key, value);
      } else {
        headers->addCopy(key, header.second);
      }
    }
  }

  for (const auto& header_to_add : config_->headersToAdd()) {
    headers->setReference(header_to_add.first, header_to_add.second);
  }

  Http::MessagePtr message = std::make_unique<Envoy::Http::RequestMessageImpl>(std::move(headers));
  if (request_length > 0) {
    message->body() =
        std::make_unique<Buffer::OwnedImpl>(request.attributes().request().http().body());
  }

  request_ = cm_.httpAsyncClientForCluster(config_->cluster())
                 .send(std::move(message), *this,
                       Http::AsyncClient::RequestOptions().setTimeout(config_->timeout()));
}

void RawHttpClientImpl::onSuccess(Http::MessagePtr&& message) {
  callbacks_->onComplete(toResponse(std::move(message)));
  callbacks_ = nullptr;
}

void RawHttpClientImpl::onFailure(Http::AsyncClient::FailureReason reason) {
  ASSERT(reason == Http::AsyncClient::FailureReason::Reset);
  callbacks_->onComplete(std::make_unique<Response>(errorResponse()));
  callbacks_ = nullptr;
}

ResponsePtr RawHttpClientImpl::toResponse(Http::MessagePtr message) {
  // Set an error status if parsing status code fails. A Forbidden response is sent to the client
  // if the filter has not been configured with failure_mode_allow.
  uint64_t status_code{};
  if (!absl::SimpleAtoi(message->headers().Status()->value().getStringView(), &status_code)) {
    ENVOY_LOG(warn, "ext_authz HTTP client failed to parse the HTTP status code.");
    return std::make_unique<Response>(errorResponse());
  }

  // Set an error status if the call to the authorization server returns any of the 5xx HTTP error
  // codes. A Forbidden response is sent to the client if the filter has not been configured with
  // failure_mode_allow.
  if (Http::CodeUtility::is5xx(status_code)) {
    return std::make_unique<Response>(errorResponse());
  }

  // Create an Ok authorization response.
  if (status_code == enumToInt(Http::Code::OK)) {
    SuccessResponse ok{message->headers(), config_->upstreamHeaderMatchers(),
                       Response{CheckStatus::OK, Http::HeaderVector{}, Http::HeaderVector{},
                                EMPTY_STRING, Http::Code::OK}};
    return std::move(ok.response_);
  }

  // Create a Denied authorization response.
  SuccessResponse denied{message->headers(), config_->clientHeaderMatchers(),
                         Response{CheckStatus::Denied, Http::HeaderVector{}, Http::HeaderVector{},
                                  message->bodyAsString(), static_cast<Http::Code>(status_code)}};
  return std::move(denied.response_);
}

} // namespace ExtAuthz
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
