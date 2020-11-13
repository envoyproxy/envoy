#pragma once

#include "common/http/matching_data.h"
#include "common/matcher/matcher.h"
#include <sys/types.h>

namespace Envoy {

class HttpRequestBody : public DataInput<Http::HttpMatchingData> {
public:
  HttpRequestBody(uint32_t limit) : limit_(limit) {}
  DataInputGetResult get(const Http::HttpMatchingData& matching_data) {
    if (!matching_data.request_buffer_copy_) {
      return {true, false, absl::nullopt};
    }

    return {false, matching_data.request_buffer_copy_->length() < limit_,
            matching_data.request_buffer_copy_->toString()};
  }

private:
  const uint32_t limit_;
};

class HttpResponseBody : public DataInput<Http::HttpMatchingData> {
public:
  HttpResponseBody(uint32_t limit) : limit_(limit) {}
  DataInputGetResult get(const Http::HttpMatchingData& matching_data) {
    if (!matching_data.response_buffer_copy_) {
      return {true, false, absl::nullopt};
    }

    return {false, matching_data.response_buffer_copy_->length() < limit_,
            matching_data.response_buffer_copy_->toString()};
  }

private:
  const uint32_t limit_;
};

class HttpRequestHeaders : public DataInput<Http::HttpMatchingData> {
public:
  explicit HttpRequestHeaders(absl::string_view header_name)
      : header_name_(std::string(header_name)) {}

  DataInputGetResult get(const Http::HttpMatchingData& matching_data) {
    if (!matching_data.request_headers_) {
      return {true, false, absl::nullopt};
    }

    result_ =
        Http::HeaderUtility::getAllOfHeaderAsString(*matching_data.request_headers_, header_name_);

    return {false, false, result_.result()};
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
      return {true, false, absl::nullopt};
    }

    result_ =
        Http::HeaderUtility::getAllOfHeaderAsString(*matching_data.response_headers_, header_name_);

    return {false, false, result_.result()};
  }

private:
  Http::HeaderUtility::GetAllOfHeaderAsStringResult result_;
  const Http::LowerCaseString header_name_;
};
} // namespace Envoy