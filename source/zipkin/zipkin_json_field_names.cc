#include "zipkin/zipkin_json_field_names.h"

const std::string Zipkin::ZipkinJsonFieldNames::SPAN_TRACE_ID = "traceId";
const std::string Zipkin::ZipkinJsonFieldNames::SPAN_TRACE_ID_HIGH = "traceIdHigh";
const std::string Zipkin::ZipkinJsonFieldNames::SPAN_PARENT_ID = "parentId";
const std::string Zipkin::ZipkinJsonFieldNames::SPAN_NAME = "name";
const std::string Zipkin::ZipkinJsonFieldNames::SPAN_ID = "id";
const std::string Zipkin::ZipkinJsonFieldNames::SPAN_TIMESTAMP = "timestamp";
const std::string Zipkin::ZipkinJsonFieldNames::SPAN_DURATION = "duration";
const std::string Zipkin::ZipkinJsonFieldNames::SPAN_DEBUG = "debug";
const std::string Zipkin::ZipkinJsonFieldNames::SPAN_ANNOTATIONS = "annotations";
const std::string Zipkin::ZipkinJsonFieldNames::SPAN_BINARY_ANNOTATIONS = "binaryAnnotations";

const std::string Zipkin::ZipkinJsonFieldNames::ANNOTATION_ENDPOINT = "endpoint";
const std::string Zipkin::ZipkinJsonFieldNames::ANNOTATION_TIMESTAMP = "timestamp";
const std::string Zipkin::ZipkinJsonFieldNames::ANNOTATION_VALUE = "value";

const std::string Zipkin::ZipkinJsonFieldNames::BINARY_ANNOTATION_ENDPOINT = "endpoint";
const std::string Zipkin::ZipkinJsonFieldNames::BINARY_ANNOTATION_KEY = "key";
const std::string Zipkin::ZipkinJsonFieldNames::BINARY_ANNOTATION_VALUE = "value";

const std::string Zipkin::ZipkinJsonFieldNames::ENDPOINT_SERVICE_NAME = "serviceName";
const std::string Zipkin::ZipkinJsonFieldNames::ENDPOINT_PORT = "port";
const std::string Zipkin::ZipkinJsonFieldNames::ENDPOINT_IPV4 = "ipv4";
const std::string Zipkin::ZipkinJsonFieldNames::ENDPOINT_IPV6 = "ipv6";
