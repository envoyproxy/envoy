#include "test/extensions/filters/http/ext_proc/utils.h"

#include <string>
#include <utility>

#include "envoy/http/header_map.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/common/macros.h"
#include "source/common/protobuf/protobuf.h"

#include "test/test_common/utility.h"

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

const absl::flat_hash_set<std::string> ExtProcTestUtility::ignoredHeaders() {
  CONSTRUCT_ON_FIRST_USE(absl::flat_hash_set<std::string>, "x-request-id",
                         "x-envoy-upstream-service-time", "x-envoy-expected-rq-timeout-ms");
}

bool ExtProcTestUtility::headerProtosEqualIgnoreOrder(
    const Http::HeaderMap& expected, const envoy::config::core::v3::HeaderMap& actual) {
  // Comparing header maps is hard because they have duplicates in them.
  // So we're going to turn them into a HeaderMap and let Envoy do the work.
  Http::TestRequestHeaderMapImpl actual_headers;
  for (const auto& header : actual.headers()) {
    if (!ignoredHeaders().contains(header.key())) {
      actual_headers.addCopy(header.key(), header.raw_value());
    }
  }
  return TestUtility::headerMapEqualIgnoreOrder(expected, actual_headers);
}

envoy::config::core::v3::HeaderValue makeHeaderValue(const std::string& key,
                                                     const std::string& value) {
  envoy::config::core::v3::HeaderValue v;
  v.set_key(key);
  v.set_value(value);
  return v;
}

void TestOnReceiveMessageDecorator::onReceiveMessage(
    const envoy::service::ext_proc::v3::ProcessingResponse& response, absl::Status,
    Envoy::StreamInfo::StreamInfo& stream_info) {
  if (response.has_request_headers()) {
    if (response.request_headers().has_response() &&
        response.request_headers().response().has_header_mutation()) {
      Envoy::ProtobufWkt::Struct struct_metadata;
      for (auto& header : response.request_headers().response().header_mutation().set_headers()) {
        Envoy::ProtobufWkt::Value value;
        value.mutable_string_value()->assign(header.header().raw_value());
        struct_metadata.mutable_fields()->insert(std::make_pair(header.header().key(), value));
      }
      for (auto& header :
           response.request_headers().response().header_mutation().remove_headers()) {
        Envoy::ProtobufWkt::Value value;
        value.mutable_string_value()->assign("remove");
        struct_metadata.mutable_fields()->insert(std::make_pair(header, value));
      }
      stream_info.setDynamicMetadata("envoy.test.ext_proc.request_headers_response",
                                     struct_metadata);
    }
  }
}

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
