#pragma once

#include <string>
#include <vector>

#include "envoy/access_log/access_log.h"
#include "envoy/api/v2/core/base.pb.h"
#include "envoy/http/header_map.h"

#include "common/protobuf/protobuf.h"
#include "common/router/header_formatter.h"

namespace Envoy {
namespace Router {

class HeaderParser;
typedef std::unique_ptr<HeaderParser> HeaderParserPtr;

/**
 * HeaderParser manipulates Http::HeaderMap instances. Headers to be added are pre-parsed to select
 * between a constant value implementation and a dynamic value implementation based on
 * RequestInfo::RequestInfo fields.
 */
class HeaderParser {
public:
  /*
   * @param headers_to_add defines the headers to add during calls to evaluateHeaders
   * @return HeaderParserPtr a configured HeaderParserPtr
   */
  static HeaderParserPtr configure(
      const Protobuf::RepeatedPtrField<envoy::api::v2::core::HeaderValueOption>& headers_to_add);

  /*
   * @param headers_to_add defines headers to add during calls to evaluateHeaders
   * @param headers_to_remove defines headers to remove during calls to evaluateHeaders
   * @return HeaderParserPtr a configured HeaderParserPtr
   */
  static HeaderParserPtr configure(
      const Protobuf::RepeatedPtrField<envoy::api::v2::core::HeaderValueOption>& headers_to_add,
      const Protobuf::RepeatedPtrField<ProtobufTypes::String>& headers_to_remove);

  void evaluateHeaders(Http::HeaderMap& headers,
                       const RequestInfo::RequestInfo& request_info) const;

protected:
  HeaderParser() {}

private:
  std::vector<std::pair<Http::LowerCaseString, HeaderFormatterPtr>> headers_to_add_;
  std::vector<Http::LowerCaseString> headers_to_remove_;
};

} // namespace Router
} // namespace Envoy
