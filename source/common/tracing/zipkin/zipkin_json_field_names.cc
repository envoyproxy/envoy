#include "common/tracing/zipkin/zipkin_json_field_names.h"

namespace Zipkin {

const std::string ZipkinJsonFieldNames::SPAN_TRACE_ID = "traceId";
const std::string ZipkinJsonFieldNames::SPAN_PARENT_ID = "parentId";
const std::string ZipkinJsonFieldNames::SPAN_NAME = "name";
const std::string ZipkinJsonFieldNames::SPAN_ID = "id";
const std::string ZipkinJsonFieldNames::SPAN_TIMESTAMP = "timestamp";
const std::string ZipkinJsonFieldNames::SPAN_DURATION = "duration";
const std::string ZipkinJsonFieldNames::SPAN_ANNOTATIONS = "annotations";
const std::string ZipkinJsonFieldNames::SPAN_BINARY_ANNOTATIONS = "binaryAnnotations";

const std::string ZipkinJsonFieldNames::ANNOTATION_ENDPOINT = "endpoint";
const std::string ZipkinJsonFieldNames::ANNOTATION_TIMESTAMP = "timestamp";
const std::string ZipkinJsonFieldNames::ANNOTATION_VALUE = "value";

const std::string ZipkinJsonFieldNames::BINARY_ANNOTATION_ENDPOINT = "endpoint";
const std::string ZipkinJsonFieldNames::BINARY_ANNOTATION_KEY = "key";
const std::string ZipkinJsonFieldNames::BINARY_ANNOTATION_VALUE = "value";

const std::string ZipkinJsonFieldNames::ENDPOINT_SERVICE_NAME = "serviceName";
const std::string ZipkinJsonFieldNames::ENDPOINT_PORT = "port";
const std::string ZipkinJsonFieldNames::ENDPOINT_IPV4 = "ipv4";
const std::string ZipkinJsonFieldNames::ENDPOINT_IPV6 = "ipv6";
} // Zipkin
