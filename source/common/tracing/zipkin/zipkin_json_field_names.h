#pragma once

#include <string>

#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Zipkin {

class ZipkinJsonFieldNameValues {
public:
  const std::string SPAN_TRACE_ID = "traceId";
  const std::string SPAN_PARENT_ID = "parentId";
  const std::string SPAN_NAME = "name";
  const std::string SPAN_ID = "id";
  const std::string SPAN_TIMESTAMP = "timestamp";
  const std::string SPAN_DURATION = "duration";
  const std::string SPAN_ANNOTATIONS = "annotations";
  const std::string SPAN_BINARY_ANNOTATIONS = "binaryAnnotations";

  const std::string ANNOTATION_ENDPOINT = "endpoint";
  const std::string ANNOTATION_TIMESTAMP = "timestamp";
  const std::string ANNOTATION_VALUE = "value";

  const std::string BINARY_ANNOTATION_ENDPOINT = "endpoint";
  const std::string BINARY_ANNOTATION_KEY = "key";
  const std::string BINARY_ANNOTATION_VALUE = "value";

  const std::string ENDPOINT_SERVICE_NAME = "serviceName";
  const std::string ENDPOINT_PORT = "port";
  const std::string ENDPOINT_IPV4 = "ipv4";
  const std::string ENDPOINT_IPV6 = "ipv6";
};

typedef ConstSingleton<ZipkinJsonFieldNameValues> ZipkinJsonFieldNames;

} // namespace Zipkin
} // namespace Envoy
