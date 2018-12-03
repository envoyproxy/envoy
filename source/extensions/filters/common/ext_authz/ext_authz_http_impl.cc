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
  SuccessResponse(const Http::HeaderMap& headers,
                  const std::vector<Matchers::StringMatcher>& matchers, Response&& response)
      : headers_(headers), matchers_(matchers), response_(std::make_unique<Response>(response)) {
    headers_.iterate(
        [](const Http::HeaderEntry& header, void* ctx) -> Http::HeaderMap::Iterate {
          auto* context = static_cast<SuccessResponse*>(ctx);
          for (const auto& matcher : context->matchers_) {
            if (matcher.match(header.key().getStringView())) {
              context->response_->headers_to_add.emplace_back(
                  Http::LowerCaseString{header.key().c_str()}, header.value().c_str());
              return Http::HeaderMap::Iterate::Continue;
            }
          }
          return Http::HeaderMap::Iterate::Continue;
        },
        this);
  }

  const Http::HeaderMap& headers_;
  const std::vector<Matchers::StringMatcher>& matchers_;
  ResponsePtr response_;
};

} // namespace

ClientConfig::ClientConfig(const envoy::config::filter::http::ext_authz::v2alpha::ExtAuthz& config,
                           uint32_t timeout, absl::string_view path_prefix)
    : request_header_matchers_(
          toRequestMatchers(config.http_service().authorization_request().allowed_headers())),
      client_header_matchers_(toClientMatchers(
          config.http_service().authorization_response().allowed_client_headers())),
      upstream_header_matchers_(toUpstreamMatchers(
          config.http_service().authorization_response().allowed_upstream_headers())),
      authorization_headers_to_add_(
          toHeadersAdd(config.http_service().authorization_request().headers_to_add())),
      cluster_name_(config.grpc_service().envoy_grpc().cluster_name()), timeout_(timeout),
      path_prefix_(path_prefix) {}

std::vector<Matchers::StringMatcher>
ClientConfig::toRequestMatchers(const envoy::type::matcher::ListStringMatcher& list) {
  const std::vector<Http::LowerCaseString> keys{
      {Http::Headers::get().Authorization, Http::Headers::get().Method, Http::Headers::get().Path,
       Http::Headers::get().Host}};

  std::vector<Matchers::StringMatcher> matchers{list.patterns().begin(), list.patterns().end()};
  for (const auto& key : keys) {
    envoy::type::matcher::StringMatcher matcher;
    matcher.set_exact(key.get());
    matchers.push_back(matcher);
  }
  matchers.shrink_to_fit();
  return matchers;
}

std::vector<Matchers::StringMatcher>
ClientConfig::toClientMatchers(const envoy::type::matcher::ListStringMatcher& list) {
  // If list is empty, all authorization response headers, except Host, should be included to
  // the client response. If not empty, user input matchers and a default list of exact matches will
  // be used.
  std::vector<Matchers::StringMatcher> matchers{list.patterns().begin(), list.patterns().end()};
  if (matchers.empty()) {
    envoy::type::matcher::StringMatcher matcher;
    matcher.set_not_exact(Http::Headers::get().Host.get());
    matchers.push_back(matcher);
  } else {
    std::vector<Http::LowerCaseString> keys{
        {Http::Headers::get().Status, Http::Headers::get().ContentLength,
         Http::Headers::get().WWWAuthenticate, Http::Headers::get().Location}};
    for (const auto& key : keys) {
      envoy::type::matcher::StringMatcher matcher;
      matcher.set_exact(key.get());
      matchers.push_back(matcher);
    }
  }
  matchers.shrink_to_fit();
  return matchers;
}

std::vector<Matchers::StringMatcher>
ClientConfig::toUpstreamMatchers(const envoy::type::matcher::ListStringMatcher& list) {
  std::vector<Matchers::StringMatcher> matchers{list.patterns().begin(), list.patterns().end()};
  matchers.shrink_to_fit();
  return matchers;
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

void RawHttpClientImpl::check(RequestCallbacks& callbacks,
                              const envoy::service::auth::v2alpha::CheckRequest& request,
                              Tracing::Span&) {
  ASSERT(callbacks_ == nullptr);
  callbacks_ = &callbacks;

  Http::HeaderMapPtr headers = std::make_unique<Http::HeaderMapImpl>(lengthZeroHeader());
  for (const auto& header : request.attributes().request().http().headers()) {
    const Http::LowerCaseString key{header.first};
    const bool is_match = std::any_of(config_->requestHeaderMatchers().begin(),
                                      config_->requestHeaderMatchers().end(),
                                      [&key](auto matcher) { return matcher.match(key.get()); });
    if (is_match) {
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

  request_ = cm_.httpAsyncClientForCluster(config_->cluster())
                 .send(std::make_unique<Envoy::Http::RequestMessageImpl>(std::move(headers)), *this,
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
  if (!StringUtil::atoul(message->headers().Status()->value().c_str(), status_code)) {
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
