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

const Http::HeaderMap* getZeroContentLengthHeader() {
  static const Http::HeaderMap* header_map =
      new Http::HeaderMapImpl{{Http::Headers::get().ContentLength, std::to_string(0)}};
  return header_map;
}
} // namespace

RawHttpClientImpl::RawHttpClientImpl(
    const std::string& cluster_name, Upstream::ClusterManager& cluster_manager,
    const absl::optional<std::chrono::milliseconds>& timeout, const std::string& path_prefix,
    const Http::LowerCaseStrUnorderedSet& allowed_authorization_headers,
    const Http::LowerCaseStrUnorderedSet& allowed_request_headers)
    : cluster_name_(cluster_name), path_prefix_(path_prefix),
      allowed_authorization_headers_(allowed_authorization_headers),
      allowed_request_headers_(allowed_request_headers), timeout_(timeout), cm_(cluster_manager) {}

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

  Http::HeaderMapPtr headers = std::make_unique<Http::HeaderMapImpl>(*getZeroContentLengthHeader());
  for (const auto& allowed_header : allowed_request_headers_) {
    const auto& request_header =
        request.attributes().request().http().headers().find(allowed_header.get());
    if (request_header != request.attributes().request().http().headers().cend()) {
      if (allowed_header == Http::Headers::get().Path && !path_prefix_.empty()) {
        std::string value;
        absl::StrAppend(&value, path_prefix_, request_header->second);
        headers->addCopy(allowed_header, value);
      } else {
        headers->addCopy(allowed_header, request_header->second);
      }
    }
  }

  request_ = cm_.httpAsyncClientForCluster(cluster_name_)
                 .send(std::make_unique<Envoy::Http::RequestMessageImpl>(std::move(headers)), *this,
                       timeout_);
}

ResponsePtr RawHttpClientImpl::messageToResponse(Http::MessagePtr& message) {
  ResponsePtr response = std::make_unique<Response>(Response{});

  // Set an error status if parsing status code fails. A Forbidden response is sent to the client
  // if the filter has not been configured with failure_mode_allow.
  uint64_t status_code{};
  if (!StringUtil::atoul(message->headers().Status()->value().c_str(), status_code)) {
    ENVOY_LOG(warn, "ext_authz HTTP client failed to parse the HTTP status code.");
    response->status = CheckStatus::Error;
    response->status_code = Http::Code::Forbidden;
    return response;
  }

  // Set an error status if the call to the authorization server returns any of the 5xx HTTP error
  // codes. A Forbidden response is sent to the client if the filter has not been configured with
  // failure_mode_allow.
  if (Http::CodeUtility::is5xx(status_code)) {
    ENVOY_LOG(warn, "{} HTTP error code received from the authorization server.", status_code);
    response->status = CheckStatus::Error;
    response->status_code = Http::Code::Forbidden;
    return response;
  }

  // Set an accepted authorization response if status is OK.
  if (status_code == enumToInt(Http::Code::OK)) {
    response->status = CheckStatus::OK;
    response->status_code = Http::Code::OK;
    return response;
  }

  // Deny otherwise.
  response->status = CheckStatus::Denied;
  response->status_code = static_cast<Http::Code>(status_code);

  // Copy the body message that should be in the response.
  response->body = message->bodyAsString();

  // Copy all headers from the message that should be in the response.
  for (const auto& allowed_header : allowed_authorization_headers_) {
    const auto* entry = message->headers().get(allowed_header);
    if (entry) {
      response->headers_to_add.emplace_back(Http::LowerCaseString{entry->key().c_str()},
                                            std::string{entry->value().c_str()});
    }
  }

  return response;
}

void RawHttpClientImpl::onSuccess(Http::MessagePtr&& response) {
  callbacks_->onComplete(messageToResponse(response));
  callbacks_ = nullptr;
}

void RawHttpClientImpl::onFailure(Http::AsyncClient::FailureReason reason) {
  ENVOY_LOG(warn, "ext_authz HTTP client failed to call the authorization server.");
  ASSERT(reason == Http::AsyncClient::FailureReason::Reset);
  Response response{};
  response.status = CheckStatus::Error;
  response.status_code = Http::Code::Forbidden;
  callbacks_->onComplete(std::make_unique<Response>(response));
  callbacks_ = nullptr;
}

} // namespace ExtAuthz
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
