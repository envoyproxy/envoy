#pragma once

#include "envoy/api/v2/core/base.pb.h"

#include "common/http/headers.h"

#include "extensions/filters/common/ext_authz/ext_authz_grpc_impl.h"

#include "test/extensions/filters/common/ext_authz/mocks.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace ExtAuthz {

MATCHER_P(AuthzErrorResponse, status, "") { return arg->status == status; }

MATCHER_P(AuthzResponseNoAttributes, response, "") {
  if (arg->status != response.status) {
    return false;
  }
  return true;
}

MATCHER_P(AuthzDeniedResponse, response, "") {
  if (arg->status != response.status) {
    return false;
  }
  if (arg->status_code != response.status_code) {
    return false;
  }
  if (arg->body.compare(response.body)) {
    return false;
  }
  // Compare headers_to_add.
  if (!arg->headers_to_add.empty() && response.headers_to_add.empty()) {
    return false;
  }
  if (!std::equal(arg->headers_to_add.begin(), arg->headers_to_add.end(),
                  response.headers_to_add.begin())) {
    return false;
  }

  return true;
}

MATCHER_P(AuthzOkResponse, response, "") {
  if (arg->status != response.status) {
    return false;
  }
  // Compare headers_to_apppend.
  if (!arg->headers_to_append.empty() && response.headers_to_append.empty()) {
    return false;
  }
  if (!std::equal(arg->headers_to_append.begin(), arg->headers_to_append.end(),
                  response.headers_to_append.begin())) {
    return false;
  }
  // Compare headers_to_add.
  if (!arg->headers_to_add.empty() && response.headers_to_add.empty()) {
    return false;
  }
  if (!std::equal(arg->headers_to_add.begin(), arg->headers_to_add.end(),
                  response.headers_to_add.begin())) {
    return false;
  }

  return true;
}

struct KeyValueOption {
  std::string key;
  std::string value;
  bool append;
};

typedef std::vector<KeyValueOption> KeyValueOptionVector;
typedef std::vector<envoy::api::v2::core::HeaderValueOption> HeaderValueOptionVector;
typedef std::unique_ptr<envoy::service::auth::v2alpha::CheckResponse> CheckResponsePtr;

class TestCommon {
public:
  static Http::MessagePtr makeMessageResponse(const HeaderValueOptionVector& headers,
                                              const std::string& body = std::string{});

  static CheckResponsePtr
  makeCheckResponse(Grpc::Status::GrpcStatus response_status = Grpc::Status::GrpcStatus::Ok,
                    envoy::type::StatusCode http_status_code = envoy::type::StatusCode::OK,
                    const std::string& body = std::string{},
                    const HeaderValueOptionVector& headers = HeaderValueOptionVector{});

  static Response
  makeAuthzResponse(CheckStatus status, Http::Code status_code = Http::Code::OK,
                    const std::string& body = std::string{},
                    const HeaderValueOptionVector& headers = HeaderValueOptionVector{});

  static HeaderValueOptionVector makeHeaderValueOption(KeyValueOptionVector&& headers);
};

} // namespace ExtAuthz
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
