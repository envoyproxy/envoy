#include "source/extensions/filters/common/ext_authz/ext_authz_grpc_impl.h"

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/service/auth/v3/external_auth.pb.h"

#include "source/common/common/assert.h"
#include "source/common/grpc/async_client_impl.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/network/utility.h"
#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace ExtAuthz {

namespace {

// Converts proto append_action to our internal HeaderMutationAction enum.
// Returns the action and sets saw_invalid if the action is unknown.
HeaderMutationAction
toHeaderMutationAction(const envoy::config::core::v3::HeaderValueOption& header,
                       bool& saw_invalid_append_actions, bool is_response_header) {
  if (header.has_append()) {
    // Handle deprecated append boolean for backward compatibility.
    // For response headers including the local replies, append=true meant adding a new header.
    // For request headers, append=true meant appending to existing value.
    if (is_response_header && header.append().value()) {
      return HeaderMutationAction::Add;
    }
    return header.append().value() ? HeaderMutationAction::Append : HeaderMutationAction::Set;
  }

  switch (header.append_action()) {
  case Router::HeaderValueOption::APPEND_IF_EXISTS_OR_ADD:
    return HeaderMutationAction::Add;
  case Router::HeaderValueOption::ADD_IF_ABSENT:
    return HeaderMutationAction::AddIfAbsent;
  case Router::HeaderValueOption::OVERWRITE_IF_EXISTS:
    return HeaderMutationAction::OverwriteIfExists;
  case Router::HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD:
    return HeaderMutationAction::Set;
  default:
    saw_invalid_append_actions = true;
    // Default to Set for unknown actions.
    return HeaderMutationAction::Set;
  }
}

// Processes header mutations from proto, preserving order.
void processHeaderMutations(
    const Protobuf::RepeatedPtrField<envoy::config::core::v3::HeaderValueOption>& headers,
    HeaderMutationVector& mutations, bool& saw_invalid_append_actions,
    bool is_response_header = false) {
  for (const auto& header : headers) {
    HeaderMutationAction action =
        toHeaderMutationAction(header, saw_invalid_append_actions, is_response_header);
    mutations.push_back({header.header().key(), header.header().value(), action});
  }
}

} // namespace

void copyOkResponseMutations(ResponsePtr& response,
                             const envoy::service::auth::v3::OkHttpResponse& ok_response) {
  // Process upstream request header mutations.
  processHeaderMutations(ok_response.headers(), response->request_header_mutations,
                         response->saw_invalid_append_actions, /*is_response_header=*/false);

  // Process downstream response header mutations.
  processHeaderMutations(ok_response.response_headers_to_add(), response->response_header_mutations,
                         response->saw_invalid_append_actions, /*is_response_header=*/true);

  response->headers_to_remove = std::vector<std::string>{ok_response.headers_to_remove().begin(),
                                                         ok_response.headers_to_remove().end()};

  for (const auto& query_parameter : ok_response.query_parameters_to_set()) {
    response->query_parameters_to_set.emplace_back(query_parameter.key(), query_parameter.value());
  }

  response->query_parameters_to_remove =
      std::vector<std::string>{ok_response.query_parameters_to_remove().begin(),
                               ok_response.query_parameters_to_remove().end()};
}

GrpcClientImpl::GrpcClientImpl(const Grpc::RawAsyncClientSharedPtr& async_client,
                               const absl::optional<std::chrono::milliseconds>& timeout)
    : async_client_(async_client), timeout_(timeout),
      service_method_(*Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
          "envoy.service.auth.v3.Authorization.Check")) {}

GrpcClientImpl::~GrpcClientImpl() { ASSERT(!callbacks_); }

void GrpcClientImpl::cancel() {
  ASSERT(callbacks_ != nullptr);
  request_->cancel();
  callbacks_ = nullptr;
}

void GrpcClientImpl::check(RequestCallbacks& callbacks,
                           const envoy::service::auth::v3::CheckRequest& request,
                           Tracing::Span& parent_span, const StreamInfo::StreamInfo& stream_info) {
  ASSERT(callbacks_ == nullptr);
  callbacks_ = &callbacks;
  Http::AsyncClient::RequestOptions options;
  options.setTimeout(timeout_);
  options.setParentContext(Http::AsyncClient::ParentContext{&stream_info});

  ENVOY_LOG(trace, "Sending CheckRequest: {}", request.DebugString());
  request_ = async_client_->send(service_method_, request, *this, parent_span, options);
}

void GrpcClientImpl::onSuccess(std::unique_ptr<envoy::service::auth::v3::CheckResponse>&& response,
                               Tracing::Span& span) {
  ENVOY_LOG(trace, "Received CheckResponse: {}", response->DebugString());
  ResponsePtr authz_response = std::make_unique<Response>(Response{});
  authz_response->grpc_status = response->status().code();
  if (response->status().code() == Grpc::Status::WellKnownGrpcStatus::Ok) {
    span.setTag(TracingConstants::get().TraceStatus, TracingConstants::get().TraceOk);
    authz_response->status = CheckStatus::OK;
    if (response->has_ok_response()) {
      copyOkResponseMutations(authz_response, response->ok_response());
    }
  } else if (response->has_error_response()) {
    // If error_response is present, treat it as an error and not denial.
    span.setTag(TracingConstants::get().TraceStatus, TracingConstants::get().TraceError);
    authz_response->status = CheckStatus::Error;

    // For error responses, don't set a default status_code.
    // Let the filter use status_on_error configuration.
    const auto& error_response = response->error_response();
    processHeaderMutations(error_response.headers(), authz_response->request_header_mutations,
                           authz_response->saw_invalid_append_actions, /*is_response_header=*/true);

    const uint32_t status_code = error_response.status().code();
    if (status_code > 0) {
      authz_response->status_code = static_cast<Http::Code>(status_code);
    }
    authz_response->body = error_response.body();
  } else {
    span.setTag(TracingConstants::get().TraceStatus, TracingConstants::get().TraceUnauthz);
    authz_response->status = CheckStatus::Denied;

    // The default HTTP status code for denied response is 403 Forbidden.
    authz_response->status_code = Http::Code::Forbidden;
    if (response->has_denied_response()) {
      processHeaderMutations(
          response->denied_response().headers(), authz_response->request_header_mutations,
          authz_response->saw_invalid_append_actions, /*is_response_header=*/true);

      const uint32_t status_code = response->denied_response().status().code();
      if (status_code > 0) {
        authz_response->status_code = static_cast<Http::Code>(status_code);
      }
      authz_response->body = response->denied_response().body();
    }
  }

  // OkHttpResponse.dynamic_metadata is deprecated. Until OkHttpResponse.dynamic_metadata is
  // removed, it overrides dynamic_metadata field of the outer check response.
  if (response->has_ok_response() && response->ok_response().has_dynamic_metadata()) {
    authz_response->dynamic_metadata = response->ok_response().dynamic_metadata();
  } else {
    authz_response->dynamic_metadata = response->dynamic_metadata();
  }
  callbacks_->onComplete(std::move(authz_response));
  callbacks_ = nullptr;
}

void GrpcClientImpl::onFailure(Grpc::Status::GrpcStatus status, const std::string&,
                               Tracing::Span&) {
  ENVOY_LOG(trace, "ExtAuthz upstream failure: {}", status);
  ResponsePtr authz_response = std::make_unique<Response>(Response{});
  authz_response->status = CheckStatus::Error;
  authz_response->grpc_status = status;
  callbacks_->onComplete(std::move(authz_response));
  callbacks_ = nullptr;
}

} // namespace ExtAuthz
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
