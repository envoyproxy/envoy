#pragma once

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/service/auth/v3/external_auth.pb.h"
#include "envoy/type/v3/http_status.pb.h"

#include "common/http/headers.h"

#include "extensions/filters/common/ext_authz/ext_authz_grpc_impl.h"

#include "test/extensions/filters/common/ext_authz/mocks.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace ExtAuthz {

struct KeyValueOption {
  std::string key;
  std::string value;
  bool append;
};

using KeyValueOptionVector = std::vector<KeyValueOption>;
using HeaderValueOptionVector = std::vector<envoy::config::core::v3::HeaderValueOption>;
using CheckResponsePtr = std::unique_ptr<envoy::service::auth::v3::CheckResponse>;

class TestCommon {
public:
  static Http::ResponseMessagePtr makeMessageResponse(const HeaderValueOptionVector& headers,
                                                      const std::string& body = std::string{});

  static CheckResponsePtr makeCheckResponse(
      Grpc::Status::GrpcStatus response_status = Grpc::Status::WellKnownGrpcStatus::Ok,
      envoy::type::v3::StatusCode http_status_code = envoy::type::v3::OK,
      const std::string& body = std::string{},
      const HeaderValueOptionVector& headers = HeaderValueOptionVector{});

  static Response
  makeAuthzResponse(CheckStatus status, Http::Code status_code = Http::Code::OK,
                    const std::string& body = std::string{},
                    const HeaderValueOptionVector& headers = HeaderValueOptionVector{});

  static HeaderValueOptionVector makeHeaderValueOption(KeyValueOptionVector&& headers);

  static bool compareHeaderVector(const Http::HeaderVector& lhs, const Http::HeaderVector& rhs);
  static bool compareVectorOfHeaderName(const std::vector<Http::LowerCaseString>& lhs,
                                        const std::vector<Http::LowerCaseString>& rhs);
};

MATCHER_P(AuthzErrorResponse, status, "") {
  // These fields should be always empty when the status is an error.
  if (!arg->headers_to_add.empty() || !arg->headers_to_append.empty() || !arg->body.empty()) {
    return false;
  }
  // HTTP status code should be always set to Forbidden.
  if (arg->status_code != Http::Code::Forbidden) {
    return false;
  }
  return arg->status == status;
}

MATCHER(AuthzTimedoutResponse, "") {
  // These fields should be always empty when the status is a timeout error.
  if (!arg->headers_to_add.empty() || !arg->headers_to_append.empty() || !arg->body.empty()) {
    return false;
  }
  // HTTP status code should be always set to Forbidden.
  if (arg->status_code != Http::Code::Forbidden) {
    return false;
  }
  return arg->status == CheckStatus::Error && arg->error_kind == ErrorKind::Timedout;
}

MATCHER_P(AuthzResponseNoAttributes, response, "") {
  const bool equal_status = arg->status == response.status;
  const bool equal_metadata =
      TestUtility::protoEqual(arg->dynamic_metadata, response.dynamic_metadata);
  if (!equal_metadata) {
    *result_listener << "\n"
                     << "==================Expected response dynamic metadata:==================\n"
                     << response.dynamic_metadata.DebugString()
                     << "------------------is not equal to actual dynamic metadata:-------------\n"
                     << arg->dynamic_metadata.DebugString()
                     << "=======================================================================\n";
  }
  return equal_status && equal_metadata;
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
  return TestCommon::compareHeaderVector(response.headers_to_add, arg->headers_to_add);
}

MATCHER_P(AuthzOkResponse, response, "") {
  if (arg->status != response.status) {
    return false;
  }
  // Compare headers_to_append.
  if (!TestCommon::compareHeaderVector(response.headers_to_append, arg->headers_to_append)) {
    return false;
  }

  // Compare headers_to_add.
  if (!TestCommon::compareHeaderVector(response.headers_to_add, arg->headers_to_add)) {
    return false;
  }

  return TestCommon::compareVectorOfHeaderName(response.headers_to_remove, arg->headers_to_remove);
}

MATCHER_P(ContainsPairAsHeader, pair, "") {
  return arg->headers().get(pair.first)[0]->value().getStringView() == pair.second;
}

} // namespace ExtAuthz
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
