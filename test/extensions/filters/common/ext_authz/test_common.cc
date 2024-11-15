#include "test/extensions/filters/common/ext_authz/test_common.h"

#include <memory>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/service/auth/v3/external_auth.pb.h"
#include "envoy/type/v3/http_status.pb.h"

#include "test/test_common/utility.h"

using ::testing::PrintToString;

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace ExtAuthz {

// NOLINTNEXTLINE(readability-identifier-naming)
void PrintTo(const ResponsePtr& ptr, std::ostream* os) {
  if (ptr != nullptr) {
    PrintTo(*ptr, os);
  } else {
    (*os) << "null";
  }
}

// NOLINTNEXTLINE(readability-identifier-naming)
void PrintTo(const Response& response, std::ostream* os) {
  (*os) << "\n{\n  check_status: " << int(response.status)
        << "\n  headers_to_append: " << PrintToString(response.headers_to_append)
        << "\n  headers_to_set: " << PrintToString(response.headers_to_set)
        << "\n  headers_to_add: " << PrintToString(response.headers_to_add)
        << "\n  response_headers_to_add: " << PrintToString(response.response_headers_to_add)
        << "\n  response_headers_to_set: " << PrintToString(response.response_headers_to_set)
        << "\n  headers_to_remove: " << PrintToString(response.headers_to_remove)
        << "\n  query_parameters_to_set: " << PrintToString(response.query_parameters_to_set)
        << "\n  query_parameters_to_remove: " << PrintToString(response.query_parameters_to_remove)
        << "\n  body: " << response.body << "\n  status_code: " << int(response.status_code)
        << "\n  dynamic_metadata: " << response.dynamic_metadata.DebugString() << "\n}\n";
}

CheckResponsePtr TestCommon::makeCheckResponse(Grpc::Status::GrpcStatus response_status,
                                               envoy::type::v3::StatusCode http_status_code,
                                               const std::string& body,
                                               const HeaderValueOptionVector& headers,
                                               const HeaderValueOptionVector& downstream_headers) {
  auto response = std::make_unique<envoy::service::auth::v3::CheckResponse>();
  auto status = response->mutable_status();
  status->set_code(response_status);

  if (response_status != Grpc::Status::WellKnownGrpcStatus::Ok) {
    const auto denied_response = response->mutable_denied_response();
    if (!body.empty()) {
      denied_response->set_body(body);
    }

    auto status_code = denied_response->mutable_status();
    status_code->set_code(http_status_code);

    auto denied_response_headers = denied_response->mutable_headers();
    if (!headers.empty()) {
      for (const auto& header : headers) {
        auto* item = denied_response_headers->Add();
        item->CopyFrom(header);
      }
    }
  } else {
    if (!headers.empty()) {
      const auto ok_response_headers = response->mutable_ok_response()->mutable_headers();
      for (const auto& header : headers) {
        auto* item = ok_response_headers->Add();
        item->CopyFrom(header);
      }
    }
    if (!downstream_headers.empty()) {
      const auto ok_response_headers_to_add =
          response->mutable_ok_response()->mutable_response_headers_to_add();
      for (const auto& header : downstream_headers) {
        auto* item = ok_response_headers_to_add->Add();
        item->CopyFrom(header);
      }
    }
  }
  return response;
}

Response TestCommon::makeAuthzResponse(CheckStatus status, Http::Code status_code,
                                       const std::string& body,
                                       const HeaderValueOptionVector& headers,
                                       const HeaderValueOptionVector& downstream_headers) {
  auto authz_response = Response{};
  authz_response.status = status;
  authz_response.status_code = status_code;
  if (!body.empty()) {
    authz_response.body = body;
  }
  if (!headers.empty()) {
    for (auto& header : headers) {
      if (header.append().value()) {
        authz_response.headers_to_append.emplace_back(header.header().key(),
                                                      header.header().value());
      } else {
        authz_response.headers_to_set.emplace_back(header.header().key(), header.header().value());
      }
    }
  }
  if (!downstream_headers.empty()) {
    for (auto& header : downstream_headers) {
      if (header.append().value()) {
        authz_response.response_headers_to_add.emplace_back(header.header().key(),
                                                            header.header().value());
      } else {
        authz_response.response_headers_to_set.emplace_back(header.header().key(),
                                                            header.header().value());
      }
    }
  }

  return authz_response;
}

HeaderValueOptionVector TestCommon::makeHeaderValueOption(KeyValueOptionVector&& headers) {
  HeaderValueOptionVector header_option_vector{};
  for (const auto& header : headers) {
    envoy::config::core::v3::HeaderValueOption header_value_option;
    auto* mutable_header = header_value_option.mutable_header();
    mutable_header->set_key(header.key);
    mutable_header->set_value(header.value);
    header_value_option.mutable_append()->set_value(header.append);
    header_option_vector.push_back(header_value_option);
  }
  return header_option_vector;
}

Http::ResponseMessagePtr TestCommon::makeMessageResponse(const HeaderValueOptionVector& headers,
                                                         const std::string& body) {
  Http::ResponseMessagePtr response(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{}}));
  for (auto& header : headers) {
    response->headers().addCopy(Http::LowerCaseString(header.header().key()),
                                header.header().value());
  }
  response->body().add(body);
  return response;
};

bool TestCommon::compareHeaderVector(const UnsafeHeaderVector& lhs, const UnsafeHeaderVector& rhs) {
  return std::set<UnsafeHeader>(lhs.begin(), lhs.end()) ==
         std::set<UnsafeHeader>(rhs.begin(), rhs.end());
}

bool TestCommon::compareVectorOfHeaderName(const std::vector<std::string>& lhs,
                                           const std::vector<std::string>& rhs) {
  return std::set<std::string>(lhs.begin(), lhs.end()) ==
         std::set<std::string>(rhs.begin(), rhs.end());
}

bool TestCommon::compareVectorOfUnorderedStrings(const std::vector<std::string>& lhs,
                                                 const std::vector<std::string>& rhs) {
  return std::set<std::string>(lhs.begin(), lhs.end()) ==
         std::set<std::string>(rhs.begin(), rhs.end());
}

// TODO(esmet): This belongs in a QueryParams class
bool TestCommon::compareQueryParamsVector(const Http::Utility::QueryParamsVector& lhs,
                                          const Http::Utility::QueryParamsVector& rhs) {
  return std::set<std::pair<std::string, std::string>>(lhs.begin(), lhs.end()) ==
         std::set<std::pair<std::string, std::string>>(rhs.begin(), rhs.end());
}

} // namespace ExtAuthz
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
