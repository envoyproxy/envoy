#include "test/extensions/filters/common/ext_authz/test_common.h"

#include "test/mocks/upstream/mocks.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace ExtAuthz {

CheckResponsePtr TestCommon::makeCheckResponse(Grpc::Status::GrpcStatus response_status,
                                               envoy::type::StatusCode http_status_code,
                                               const std::string& body,
                                               const HeaderValueOptionVector& headers) {
  auto response = std::make_unique<envoy::service::auth::v2alpha::CheckResponse>();
  auto status = response->mutable_status();
  status->set_code(response_status);

  if (response_status != Grpc::Status::GrpcStatus::Ok) {
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
  }
  return response;
}

Response TestCommon::makeAuthzResponse(CheckStatus status, Http::Code status_code,
                                       const std::string& body,
                                       const HeaderValueOptionVector& headers) {
  auto authz_response = Response{};
  authz_response.status = status;
  authz_response.status_code = status_code;
  if (!body.empty()) {
    authz_response.body = body;
  }
  if (!headers.empty()) {
    for (auto& header : headers) {
      if (header.append().value()) {
        authz_response.headers_to_append.emplace_back(Http::LowerCaseString(header.header().key()),
                                                      header.header().value());
      } else {
        authz_response.headers_to_add.emplace_back(Http::LowerCaseString(header.header().key()),
                                                   header.header().value());
      }
    }
  }
  return authz_response;
}

HeaderValueOptionVector TestCommon::makeHeaderValueOption(KeyValueOptionVector&& headers) {
  HeaderValueOptionVector header_option_vector{};
  for (auto header : headers) {
    envoy::api::v2::core::HeaderValueOption header_value_option;
    auto* mutable_header = header_value_option.mutable_header();
    mutable_header->set_key(header.key);
    mutable_header->set_value(header.value);
    header_value_option.mutable_append()->set_value(header.append);
    header_option_vector.push_back(header_value_option);
  }
  return header_option_vector;
}

Http::MessagePtr TestCommon::makeMessageResponse(const HeaderValueOptionVector& headers,
                                                 const std::string& body) {
  Http::MessagePtr response(
      new Http::ResponseMessageImpl(Http::HeaderMapPtr{new Http::TestHeaderMapImpl{}}));
  for (auto& header : headers) {
    response->headers().addCopy(Http::LowerCaseString(header.header().key()),
                                header.header().value());
  }
  response->body().reset(new Buffer::OwnedImpl(body));
  return response;
};

} // namespace ExtAuthz
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
