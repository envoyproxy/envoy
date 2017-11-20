#pragma once

#include <list>
#include <string>

#include "envoy/http/header_map.h"

#include "common/protobuf/protobuf.h"

#include "api/base.pb.h"

namespace Envoy {
namespace Router {

class ResponseHeaderParser;
typedef std::unique_ptr<ResponseHeaderParser> ResponseHeaderParserPtr;

/**
 * This class holds the logic required to apply response headers additions and removals.
 */
class ResponseHeaderParser {
public:
  virtual ~ResponseHeaderParser() {}

  static ResponseHeaderParserPtr
  parse(const Protobuf::RepeatedPtrField<envoy::api::v2::HeaderValueOption>& headers_to_add,
        const Protobuf::RepeatedPtrField<ProtobufTypes::String>& headers_to_remove);

  void evaluateResponseHeaders(Http::HeaderMap& headers) const;

private:
  struct HeaderAddition {
    const Http::LowerCaseString header_;
    const std::string value_;
    const bool append_;
  };

  std::list<HeaderAddition> headers_to_add_;
  std::list<Http::LowerCaseString> headers_to_remove_;
};

} // namespace Router
} // namespace Envoy
