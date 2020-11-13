#pragma once

#include "common/http/matching_data.h"
#include "common/matcher/matcher.h"

namespace Envoy {

class HttpRequestHeaders : public DataInput<Http::HttpMatchingData> {
public:
  explicit HttpRequestHeaders(absl::string_view header_name)
      : header_name_(std::string(header_name)) {}

  DataInputGetResult get(const Http::HttpMatchingData& matching_data) {
    if (!matching_data.request_headers_) {
      return {true, absl::nullopt};
    }

    auto header = matching_data.request_headers_->get(header_name_);
    if (header.size() > 0) {
      std::cout << header[0]->value().getStringView() << std::endl;
    }

    result_ =
        Http::HeaderUtility::getAllOfHeaderAsString(*matching_data.request_headers_, header_name_);

    return {false, result_.result()};
  }

private:
  Http::HeaderUtility::GetAllOfHeaderAsStringResult result_;
  const Http::LowerCaseString header_name_;
};

class HttpResponseHeaders : public DataInput<Http::HttpMatchingData> {
public:
  explicit HttpResponseHeaders(absl::string_view header_name)
      : header_name_(std::string(header_name)) {}

  DataInputGetResult get(const Http::HttpMatchingData& matching_data) {
    if (!matching_data.response_headers_) {
      return {true, absl::nullopt};
    }

    auto header = matching_data.request_headers_->get(header_name_);
    if (header.size() > 0) {
      std::cout << header[0]->value().getStringView() << std::endl;
    }

    result_ =
        Http::HeaderUtility::getAllOfHeaderAsString(*matching_data.response_headers_, header_name_);

    return {false, result_.result()};
  }

private:
  Http::HeaderUtility::GetAllOfHeaderAsStringResult result_;
  const Http::LowerCaseString header_name_;
};
} // namespace Envoy