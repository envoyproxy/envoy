#pragma once

#include <sys/types.h>

#include "common/http/matching_data.h"
#include "common/matcher/matcher.h"

namespace Envoy {

class HttpRequestBody : public DataInput<Http::HttpMatchingData> {
public:
  HttpRequestBody(uint32_t limit) : limit_(limit) {}
  DataInputGetResult get(const Http::HttpMatchingData& matching_data) {
    if (!matching_data.request_buffer_copy_) {
      return {true, false, absl::nullopt};
    }

    storage_ = matching_data.request_buffer_copy_->toString();
    return {false, canGetMoreData(matching_data), storage_};
  }

private:
  bool canGetMoreData(const Http::HttpMatchingData& matching_data) {
    if (matching_data.request_end_stream_) {
      return false;
    }

    if (limit_ == 0) {
      return true;
    }

    return matching_data.request_buffer_copy_->length() < limit_;
  }
  std::string storage_;
  const uint32_t limit_;
};

class HttpResponseBody : public DataInput<Http::HttpMatchingData> {
public:
  HttpResponseBody(uint32_t limit) : limit_(limit) {}
  DataInputGetResult get(const Http::HttpMatchingData& matching_data) {
    if (!matching_data.response_buffer_copy_) {
      return {true, false, absl::nullopt};
    }

    storage_ = matching_data.response_buffer_copy_->toString();
    return {false, canGetMoreData(matching_data), storage_};
  }

private:
  bool canGetMoreData(const Http::HttpMatchingData& matching_data) {
    if (matching_data.response_end_stream_) {
      return false;
    }

    if (limit_ == 0) {
      return true;
    }

    return matching_data.response_buffer_copy_->length() < limit_;
  }
  std::string storage_;
  const uint32_t limit_;
};

class HttpRequestTrailers : public DataInput<Http::HttpMatchingData> {
public:
  explicit HttpRequestTrailers(absl::string_view header_name)
      : header_name_(std::string(header_name)) {}

  DataInputGetResult get(const Http::HttpMatchingData& matching_data) {
    if (!matching_data.request_trailers_) {
      return {true, false, absl::nullopt};
    }

    result_ =
        Http::HeaderUtility::getAllOfHeaderAsString(*matching_data.request_trailers_, header_name_);

    return {false, false, result_.result()};
  }

private:
  Http::HeaderUtility::GetAllOfHeaderAsStringResult result_;
  const Http::LowerCaseString header_name_;
};

class HttpResponseTrailers : public DataInput<Http::HttpMatchingData> {
public:
  explicit HttpResponseTrailers(absl::string_view header_name)
      : header_name_(std::string(header_name)) {}

  DataInputGetResult get(const Http::HttpMatchingData& matching_data) {
    if (!matching_data.response_trailers_) {
      std::cout << "no trailers!" << std::endl;
      return {true, false, absl::nullopt};
    }

    std::cout << *matching_data.response_trailers_ << std::endl;
    result_ = Http::HeaderUtility::getAllOfHeaderAsString(*matching_data.response_trailers_,
                                                          header_name_);

    return {false, false, result_.result()};
  }

private:
  Http::HeaderUtility::GetAllOfHeaderAsStringResult result_;
  const Http::LowerCaseString header_name_;
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