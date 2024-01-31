#pragma once

#include <string>

#include "library/cc/headers_builder.h"
#include "library/cc/request_headers.h"
#include "library/cc/request_method.h"
#include "retry_policy.h"

namespace Envoy {
namespace Platform {

class RequestHeaders;
struct RetryPolicy;

class RequestHeadersBuilder : public HeadersBuilder {
public:
  RequestHeadersBuilder(RequestMethod request_method, std::string scheme, std::string authority,
                        std::string path);
  RequestHeadersBuilder(RequestMethod request_method, absl::string_view url);

  RequestHeadersBuilder& addRetryPolicy(const RetryPolicy& retry_policy);

  RequestHeaders build() const;

private:
  void initialize(RequestMethod request_method, std::string scheme, std::string authority,
                  std::string path);
};

using RequestHeadersBuilderSharedPtr = std::shared_ptr<RequestHeadersBuilder>;

} // namespace Platform
} // namespace Envoy
