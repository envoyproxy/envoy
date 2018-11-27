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
const Http::HeaderMap& getZeroContentLengthHeader() {
  static const Http::HeaderMap* header_map =
      new Http::HeaderMapImpl{{Http::Headers::get().ContentLength, std::to_string(0)}};
  return *header_map;
}

const Response& getErrorResponse() {
  static const Response* response =
      new Response{CheckStatus::Error, Http::HeaderVector{}, Http::HeaderVector{}, std::string{},
                   Http::Code::Forbidden};
  return *response;
}

const Response& getDeniedResponse() {
  static const Response* response =
      new Response{CheckStatus::Denied, Http::HeaderVector{}, Http::HeaderVector{}, std::string{}};
  return *response;
}

const Response& getOkResponse() {
  static const Response* response = new Response{
      CheckStatus::OK, Http::HeaderVector{}, Http::HeaderVector{}, std::string{}, Http::Code::OK};
  return *response;
}

struct ResponseContext {
  ResponsePtr response_;
  const std::vector<Matchers::StringMatcher>* matchers_;
};
} // namespace

RawHttpClientImpl::RawHttpClientImpl(
    Upstream::ClusterManager& cluster_manager, const std::string& cluster_name,
    const std::string& path_prefix, const absl::optional<std::chrono::milliseconds>& timeout,
    const std::vector<Matchers::StringMatcher>& allowed_request_headers,
    const std::vector<Matchers::StringMatcher>& allowed_client_headers,
    const std::vector<Matchers::StringMatcher>& allowed_upstream_headers,
    const Http::LowerCaseStrPairVector& authorization_headers_to_add)
    : cm_(cluster_manager), cluster_name_(cluster_name), path_prefix_(path_prefix),
      timeout_(timeout), allowed_request_headers_(allowed_request_headers),
      allowed_client_headers_(allowed_client_headers),
      allowed_upstream_headers_(allowed_upstream_headers),
      authorization_headers_to_add_(authorization_headers_to_add) {}

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

  Http::HeaderMapPtr headers = std::make_unique<Http::HeaderMapImpl>(getZeroContentLengthHeader());
  for (const auto& header : request.attributes().request().http().headers()) {
    const Http::LowerCaseString key{header.first};
    if (std::any_of(allowed_request_headers_.begin(), allowed_request_headers_.end(),
                    [&key](auto matcher) { return matcher.match(key.get()); })) {
      if (key == Http::Headers::get().Path && !path_prefix_.empty()) {
        std::string value;
        absl::StrAppend(&value, path_prefix_, header.second);
        headers->addCopy(key, value);
      } else {
        headers->addCopy(key, header.second);
      }
    }
  }
  for (const auto& header_to_add : authorization_headers_to_add_) {
    headers->setReference(header_to_add.first, header_to_add.second);
  }

  request_ = cm_.httpAsyncClientForCluster(cluster_name_)
                 .send(std::make_unique<Envoy::Http::RequestMessageImpl>(std::move(headers)), *this,
                       Http::AsyncClient::RequestOptions().setTimeout(timeout_));
}

void RawHttpClientImpl::onSuccess(Http::MessagePtr&& message) {
  callbacks_->onComplete(messageToResponse(std::move(message)));
  callbacks_ = nullptr;
}

void RawHttpClientImpl::onFailure(Http::AsyncClient::FailureReason reason) {
  ASSERT(reason == Http::AsyncClient::FailureReason::Reset);
  callbacks_->onComplete(std::make_unique<Response>(getErrorResponse()));
  callbacks_ = nullptr;
}

ResponsePtr RawHttpClientImpl::messageToResponse(Http::MessagePtr message) {
  // Set an error status if parsing status code fails. A Forbidden response is sent to the client
  // if the filter has not been configured with failure_mode_allow.
  uint64_t status_code{};
  if (!StringUtil::atoul(message->headers().Status()->value().c_str(), status_code)) {
    ENVOY_LOG(warn, "ext_authz HTTP client failed to parse the HTTP status code.");
    return std::make_unique<Response>(getErrorResponse());
  }

  // Set an error status if the call to the authorization server returns any of the 5xx HTTP error
  // codes. A Forbidden response is sent to the client if the filter has not been configured with
  // failure_mode_allow.
  if (Http::CodeUtility::is5xx(status_code)) {
    return std::make_unique<Response>(getErrorResponse());
  }

  // If the authorization server returned HTTP status 200 OK, set an authorized response object,
  // otherwise set a rejected response.
  ResponseContext ctx;
  if (status_code == enumToInt(Http::Code::OK)) {
    ctx.matchers_ = &allowed_upstream_headers_;
    ctx.response_ = std::make_unique<Response>(getOkResponse());
  } else {
    ctx.matchers_ = &allowed_client_headers_;
    ctx.response_ = std::make_unique<Response>(getDeniedResponse());
    ctx.response_->status_code = static_cast<Http::Code>(status_code);
    ctx.response_->body = message->bodyAsString();
  }

  message->headers().iterate(
      [](const Http::HeaderEntry& header, void* ctx) -> Http::HeaderMap::Iterate {
        auto* context = static_cast<ResponseContext*>(ctx);
        for (const auto& matcher : *(context->matchers_)) {
          if (matcher.match(header.key().c_str())) {
            context->response_->headers_to_add.emplace_back(
                Http::LowerCaseString{header.key().c_str()}, header.value().c_str());
            return Http::HeaderMap::Iterate::Continue;
          }
        }
        return Http::HeaderMap::Iterate::Continue;
      },
      &ctx);

  return std::move(ctx.response_);
}

} // namespace ExtAuthz
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
