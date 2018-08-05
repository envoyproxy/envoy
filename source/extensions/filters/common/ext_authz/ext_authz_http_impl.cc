#include "extensions/filters/common/ext_authz/ext_authz_http_impl.h"

#include "common/common/enum_to_int.h"
#include "common/http/async_client_impl.h"

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

void RawHttpClientImpl::onSuccess(Http::MessagePtr&& response) {
  ResponsePtr authz_response = std::make_unique<Response>(Response{});

  uint64_t status_code;
  if (StringUtil::atoul(response->headers().Status()->value().c_str(), status_code)) {
    if (status_code == enumToInt(Http::Code::OK)) {
      authz_response->status = CheckStatus::OK;
      authz_response->status_code = Http::Code::OK;
    } else {
      authz_response->status = CheckStatus::Denied;
      authz_response->body = response->bodyAsString();
      authz_response->status_code = static_cast<Http::Code>(status_code);
    }
  } else {
    ENVOY_LOG(warn, "Authz_Ext failed to parse the HTTP response code.");
    authz_response->status_code = Http::Code::Forbidden;
    authz_response->status = CheckStatus::Denied;
  }

  for (const auto& allowed_header : allowed_authorization_headers_) {
    const auto* entry = response->headers().get(allowed_header);
    if (entry) {
      authz_response->headers_to_add.emplace_back(Http::LowerCaseString{entry->key().c_str()},
                                                  std::string{entry->value().c_str()});
    }
  }

  callbacks_->onComplete(std::move(authz_response));
  callbacks_ = nullptr;
}

void RawHttpClientImpl::onFailure(Http::AsyncClient::FailureReason reason) {
  ASSERT(reason == Http::AsyncClient::FailureReason::Reset);
  Response authz_response{};
  authz_response.status = CheckStatus::Error;
  callbacks_->onComplete(std::make_unique<Response>(authz_response));
  callbacks_ = nullptr;
}

} // namespace ExtAuthz
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
