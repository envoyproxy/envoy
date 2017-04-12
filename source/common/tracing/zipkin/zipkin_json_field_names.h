#pragma once

namespace Zipkin {

class ZipkinJsonFieldNames {
public:
  static const std::string SPAN_TRACE_ID;
  static const std::string SPAN_PARENT_ID;
  static const std::string SPAN_NAME;
  static const std::string SPAN_ID;
  static const std::string SPAN_TIMESTAMP;
  static const std::string SPAN_DURATION;
  static const std::string SPAN_ANNOTATIONS;
  static const std::string SPAN_BINARY_ANNOTATIONS;

  static const std::string ANNOTATION_ENDPOINT;
  static const std::string ANNOTATION_TIMESTAMP;
  static const std::string ANNOTATION_VALUE;

  static const std::string BINARY_ANNOTATION_ENDPOINT;
  static const std::string BINARY_ANNOTATION_KEY;
  static const std::string BINARY_ANNOTATION_VALUE;

  static const std::string ENDPOINT_SERVICE_NAME;
  static const std::string ENDPOINT_PORT;
  static const std::string ENDPOINT_IPV4;
  static const std::string ENDPOINT_IPV6;
};
} // Zipkin
