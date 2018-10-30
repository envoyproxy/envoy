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
} // namespace

RawHttpClientImpl::RawHttpClientImpl(
    Upstream::ClusterManager& cluster_manager, const std::string& cluster_name,
    const absl::optional<std::chrono::milliseconds>& timeout, const std::string& path_prefix,
    const Http::LowerCaseStrUnorderedSet& allowed_request_headers,
    const Http::LowerCaseStrUnorderedSet& allowed_request_headers_prefix,
    const Http::LowerCaseStrPairVector& authorization_headers_to_add,
    const Http::LowerCaseStrUnorderedSet& allowed_upstream_headers,
    const Http::LowerCaseStrUnorderedSet& allowed_client_headers)
    : cluster_name_(cluster_name), path_prefix_(path_prefix),
      allowed_request_headers_(allowed_request_headers),
      allowed_request_headers_prefix_(allowed_request_headers_prefix),
      authorization_headers_to_add_(authorization_headers_to_add),
      allowed_upstream_headers_(allowed_upstream_headers),
      allowed_client_headers_(allowed_client_headers), timeout_(timeout), cm_(cluster_manager) {}

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

  for (const auto& kv : authorization_headers_to_add_) {
    headers->setReference(kv.first, kv.second);
  }

  request_ = cm_.httpAsyncClientForCluster(cluster_name_)
                 .send(std::make_unique<Envoy::Http::RequestMessageImpl>(std::move(headers)), *this,
                       timeout_);
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

  if (status_code == enumToInt(Http::Code::OK)) {
    ResponsePtr ok_response = std::make_unique<Response>(getOkResponse());
    // Copy all headers that should be forwarded to the upstream.
    for (const auto& header : allowed_upstream_headers_) {
      const auto* entry = message->headers().get(header);
      if (entry) {
        ok_response->headers_to_add.emplace_back(Http::LowerCaseString{entry->key().c_str()},
                                                 entry->value().c_str());
      }
    }
    return ok_response;
  }

  ResponsePtr denied_response = std::make_unique<Response>(getDeniedResponse());
  denied_response->status_code = static_cast<Http::Code>(status_code);
  denied_response->body = message->bodyAsString();
  if (allowed_client_headers_.empty()) {
    // Copy all headers to the client's response.
    message->headers().iterate(
        [](const Http::HeaderEntry& header, void* context) -> Http::HeaderMap::Iterate {
          static_cast<Http::HeaderVector*>(context)->emplace_back(
              Http::LowerCaseString{header.key().c_str()}, header.value().c_str());
          return Http::HeaderMap::Iterate::Continue;
        },
        &(denied_response->headers_to_add));
  } else {
    // Copy only the allowed headers to the client's response.
    for (const auto& header : allowed_client_headers_) {
      const auto* entry = message->headers().get(header);
      if (entry) {
        denied_response->headers_to_add.emplace_back(Http::LowerCaseString{entry->key().c_str()},
                                                     entry->value().c_str());
      }
    }
  }

  return denied_response;
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

} // namespace ExtAuthz
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
