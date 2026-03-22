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

// Result of parsing a HeaderValueOption's append action.
struct AppendActionResult {
  HeaderAppendAction action;
  bool from_deprecated_append;
};

// Converts proto HeaderValueOption to HeaderAppendAction.
// Handles the deprecated append boolean for backward compatibility.
// Returns the action and whether it came from the deprecated field.
// Sets saw_invalid if the action is unknown.
AppendActionResult getAppendAction(envoy::config::core::v3::HeaderValueOption& header,
                                   bool& saw_invalid_append_actions) {
  if (header.has_append()) {
    // Handle deprecated append boolean for backward compatibility.
    // When from_deprecated_append is true and action is APPEND_IF_EXISTS_OR_ADD,
    // the filter will use appendCopy() instead of addCopy().
    return {header.append().value() ? HeaderValueOption::APPEND_IF_EXISTS_OR_ADD
                                    : HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD,
            true};
  }

  if (!envoy::config::core::v3::HeaderValueOption::HeaderAppendAction_IsValid(
          header.append_action())) {
    saw_invalid_append_actions = true;
    return {HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD, false};
  }

  return {header.append_action(), false};
}

// Moves header mutations from proto to the response vector, preserving order.
// This is more efficient than copying strings since the proto data is not reused.
void moveHeaderMutations(
    Protobuf::RepeatedPtrField<envoy::config::core::v3::HeaderValueOption>& headers,
    HeaderMutationVector& mutations, bool& saw_invalid_append_actions) {
  mutations.reserve(mutations.size() + headers.size());
  for (auto& header : headers) {
    AppendActionResult result = getAppendAction(header, saw_invalid_append_actions);
    mutations.push_back({std::move(*header.mutable_header()->mutable_key()),
                         std::move(*header.mutable_header()->mutable_value()), result.action,
                         result.from_deprecated_append});
  }
}

// Moves strings from proto repeated field to vector without copying.
std::vector<std::string>
moveStringsFromProto(Protobuf::RepeatedPtrField<std::string>& proto_strings) {
  std::vector<std::string> result;
  result.reserve(proto_strings.size());
  for (auto& str : proto_strings) {
    result.push_back(std::move(str));
  }
  return result;
}

} // namespace

void copyOkResponseMutations(ResponsePtr& response,
                             envoy::service::auth::v3::OkHttpResponse* ok_response) {
  // Move upstream request header mutations.
  moveHeaderMutations(*ok_response->mutable_headers(), response->request_header_mutations,
                      response->saw_invalid_append_actions);

  // Move downstream response header mutations.
  moveHeaderMutations(*ok_response->mutable_response_headers_to_add(),
                      response->response_header_mutations, response->saw_invalid_append_actions);

  // Move headers_to_remove to avoid copying strings.
  response->headers_to_remove = moveStringsFromProto(*ok_response->mutable_headers_to_remove());

  for (const auto& query_parameter : ok_response->query_parameters_to_set()) {
    response->query_parameters_to_set.emplace_back(query_parameter.key(), query_parameter.value());
  }

  // Move query_parameters_to_remove to avoid copying strings.
  response->query_parameters_to_remove =
      moveStringsFromProto(*ok_response->mutable_query_parameters_to_remove());
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
      copyOkResponseMutations(authz_response, response->mutable_ok_response());
    }
  } else if (response->has_error_response()) {
    // If error_response is present, treat it as an error and not denial.
    span.setTag(TracingConstants::get().TraceStatus, TracingConstants::get().TraceError);
    authz_response->status = CheckStatus::Error;

    // For error responses, don't set a default status_code.
    // Let the filter use status_on_error configuration.
    auto* error_response = response->mutable_error_response();
    // Move headers to local_response_header_mutations for local reply.
    moveHeaderMutations(*error_response->mutable_headers(),
                        authz_response->local_response_header_mutations,
                        authz_response->saw_invalid_append_actions);

    const uint32_t status_code = error_response->status().code();
    if (status_code > 0) {
      authz_response->status_code = static_cast<Http::Code>(status_code);
    }
    authz_response->body = error_response->body();
  } else {
    span.setTag(TracingConstants::get().TraceStatus, TracingConstants::get().TraceUnauthz);
    authz_response->status = CheckStatus::Denied;

    // The default HTTP status code for denied response is 403 Forbidden.
    authz_response->status_code = Http::Code::Forbidden;
    if (response->has_denied_response()) {
      auto* denied_response = response->mutable_denied_response();
      // Move headers to local_response_header_mutations for local reply.
      moveHeaderMutations(*denied_response->mutable_headers(),
                          authz_response->local_response_header_mutations,
                          authz_response->saw_invalid_append_actions);

      const uint32_t status_code = denied_response->status().code();
      if (status_code > 0) {
        authz_response->status_code = static_cast<Http::Code>(status_code);
      }
      authz_response->body = denied_response->body();
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
