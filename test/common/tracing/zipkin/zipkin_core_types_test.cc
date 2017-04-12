#include "common/tracing/zipkin/zipkin_core_types.h"
#include "common/tracing/zipkin/zipkin_core_constants.h"

#include "gtest/gtest.h"

namespace Zipkin {

TEST(ZipkinCoreTypesEndpointTest, defaultConstructor) {
  Endpoint ep;

  EXPECT_EQ("", ep.ipv4());
  EXPECT_EQ(0, ep.port());
  EXPECT_EQ("", ep.serviceName());
  EXPECT_FALSE(ep.isSetIpv6());
  EXPECT_EQ("{\"ipv4\":\"\",\"port\":0,\"serviceName\":\"\"}", ep.toJson());

  ep.setIpv4(std::string("127.0.0.1"));
  EXPECT_EQ("127.0.0.1", ep.ipv4());

  ep.setIpv6("2001:0db8:85a3:0000:0000:8a2e:0370:7334");
  EXPECT_EQ("2001:0db8:85a3:0000:0000:8a2e:0370:7334", ep.ipv6());
  EXPECT_TRUE(ep.isSetIpv6());

  ep.setPort(3306);
  EXPECT_EQ(3306, ep.port());

  ep.setServiceName("my_service");
  EXPECT_EQ("my_service", ep.serviceName());

  EXPECT_EQ("{\"ipv4\":\"127.0.0.1\",\"port\":3306,\"serviceName\":\"my_service\",\"ipv6\":\"2001:"
            "0db8:85a3:0000:0000:8a2e:0370:7334\"}",
            ep.toJson());
}

TEST(ZipkinCoreTypesEndpointTest, customConstructor) {
  Endpoint ep(std::string("127.0.0.1"), 3306, std::string("my_service"));

  EXPECT_EQ("127.0.0.1", ep.ipv4());
  EXPECT_EQ(3306, ep.port());
  EXPECT_EQ("my_service", ep.serviceName());
  EXPECT_FALSE(ep.isSetIpv6());
  EXPECT_EQ("{\"ipv4\":\"127.0.0.1\",\"port\":3306,\"serviceName\":\"my_service\"}", ep.toJson());

  ep.setIpv6("2001:0db8:85a3:0000:0000:8a2e:0370:7334");
  EXPECT_EQ("2001:0db8:85a3:0000:0000:8a2e:0370:7334", ep.ipv6());
  EXPECT_TRUE(ep.isSetIpv6());

  EXPECT_EQ("{\"ipv4\":\"127.0.0.1\",\"port\":3306,\"serviceName\":\"my_service\",\"ipv6\":\"2001:"
            "0db8:85a3:0000:0000:8a2e:0370:7334\"}",
            ep.toJson());
}

TEST(ZipkinCoreTypesEndpointTest, copyOperator) {
  Endpoint ep1(std::string("127.0.0.1"), 3306, std::string("my_service"));
  Endpoint ep2(ep1);

  EXPECT_EQ("127.0.0.1", ep1.ipv4());
  EXPECT_EQ(3306, ep1.port());
  EXPECT_EQ("my_service", ep1.serviceName());
  EXPECT_FALSE(ep1.isSetIpv6());
  EXPECT_EQ("{\"ipv4\":\"127.0.0.1\",\"port\":3306,\"serviceName\":\"my_service\"}", ep1.toJson());

  EXPECT_EQ(ep1.ipv4(), ep2.ipv4());
  EXPECT_EQ(ep1.port(), ep2.port());
  EXPECT_EQ(ep1.serviceName(), ep2.serviceName());
  EXPECT_FALSE(ep2.isSetIpv6());
  EXPECT_EQ(ep1.toJson(), ep2.toJson());
}

TEST(ZipkinCoreTypesEndpointTest, assignmentOperator) {
  Endpoint ep1(std::string("127.0.0.1"), 3306, std::string("my_service"));
  Endpoint ep2 = ep1;

  EXPECT_EQ("127.0.0.1", ep1.ipv4());
  EXPECT_EQ(3306, ep1.port());
  EXPECT_EQ("my_service", ep1.serviceName());
  EXPECT_FALSE(ep1.isSetIpv6());
  EXPECT_EQ("{\"ipv4\":\"127.0.0.1\",\"port\":3306,\"serviceName\":\"my_service\"}", ep1.toJson());

  EXPECT_EQ(ep1.ipv4(), ep2.ipv4());
  EXPECT_EQ(ep1.port(), ep2.port());
  EXPECT_EQ(ep1.serviceName(), ep2.serviceName());
  EXPECT_FALSE(ep2.isSetIpv6());
  EXPECT_EQ(ep1.toJson(), ep2.toJson());
}

TEST(ZipkinCoreTypesAnnotationTest, defaultConstructor) {
  Annotation ann;

  EXPECT_EQ(0ULL, ann.timestamp());
  EXPECT_EQ("", ann.value());
  EXPECT_FALSE(ann.isSetEndpoint());

  uint64_t timestamp = Util::timeSinceEpochMicro();
  ann.setTimestamp(timestamp);
  EXPECT_EQ(timestamp, ann.timestamp());

  ann.setValue(ZipkinCoreConstants::CLIENT_SEND);
  EXPECT_EQ(ZipkinCoreConstants::CLIENT_SEND, ann.value());

  std::string expected_json = "{\"timestamp\":" + std::to_string(timestamp) + ",\"value\":\"" +
                              ZipkinCoreConstants::CLIENT_SEND + "\"}";
  EXPECT_EQ(expected_json, ann.toJson());

  // Test the copy-semantics flavor of setEndpoint

  Endpoint ep(std::string("127.0.0.1"), 3306, std::string("my_service"));
  ann.setEndpoint(ep);
  EXPECT_TRUE(ann.isSetEndpoint());
  EXPECT_EQ("127.0.0.1", ann.endpoint().ipv4());
  EXPECT_EQ(3306, ann.endpoint().port());
  EXPECT_EQ("my_service", ann.endpoint().serviceName());
  EXPECT_FALSE(ann.endpoint().isSetIpv6());
  EXPECT_EQ("{\"ipv4\":\"127.0.0.1\",\"port\":3306,\"serviceName\":\"my_service\"}",
            (const_cast<Endpoint&>(ann.endpoint())).toJson());

  expected_json = "{\"timestamp\":" + std::to_string(timestamp) + ",\"value\":\"" +
                  ZipkinCoreConstants::CLIENT_SEND +
                  "\",\"endpoint\":{\"ipv4\":"
                  "\"127.0.0.1\",\"port\":3306,\"serviceName\":\"my_service\"}}";
  EXPECT_EQ(expected_json, ann.toJson());

  // Test the move-semantics flavor of setEndpoint

  Endpoint ep2(std::string("192.168.1.1"), 5555, std::string("my_service_2"));
  ann.setEndpoint(std::move(ep2));
  EXPECT_TRUE(ann.isSetEndpoint());
  EXPECT_EQ("192.168.1.1", ann.endpoint().ipv4());
  EXPECT_EQ(5555, ann.endpoint().port());
  EXPECT_EQ("my_service_2", ann.endpoint().serviceName());
  EXPECT_FALSE(ann.endpoint().isSetIpv6());
  EXPECT_EQ("{\"ipv4\":\"192.168.1.1\",\"port\":5555,\"serviceName\":\"my_service_2\"}",
            (const_cast<Endpoint&>(ann.endpoint())).toJson());

  expected_json = "{\"timestamp\":" + std::to_string(timestamp) + ",\"value\":\"" +
                  ZipkinCoreConstants::CLIENT_SEND +
                  "\",\"endpoint\":{\"ipv4\":"
                  "\"192.168.1.1\",\"port\":5555,\"serviceName\":\"my_service_2\"}}";
  EXPECT_EQ(expected_json, ann.toJson());
}

TEST(ZipkinCoreTypesAnnotationTest, customConstructor) {
  Endpoint ep(std::string("127.0.0.1"), 3306, std::string("my_service"));
  uint64_t timestamp = Util::timeSinceEpochMicro();
  Annotation ann(timestamp, ZipkinCoreConstants::CLIENT_SEND, ep);

  EXPECT_EQ(timestamp, ann.timestamp());
  EXPECT_EQ(ZipkinCoreConstants::CLIENT_SEND, ann.value());
  EXPECT_TRUE(ann.isSetEndpoint());

  EXPECT_EQ("127.0.0.1", ann.endpoint().ipv4());
  EXPECT_EQ(3306, ann.endpoint().port());
  EXPECT_EQ("my_service", ann.endpoint().serviceName());
  EXPECT_FALSE(ann.endpoint().isSetIpv6());
  EXPECT_EQ("{\"ipv4\":\"127.0.0.1\",\"port\":3306,\"serviceName\":\"my_service\"}",
            (const_cast<Endpoint&>(ann.endpoint())).toJson());

  std::string expected_json = "{\"timestamp\":" + std::to_string(timestamp) + ",\"value\":\"" +
                              ZipkinCoreConstants::CLIENT_SEND +
                              "\",\"endpoint\":{\"ipv4\":"
                              "\"127.0.0.1\",\"port\":3306,\"serviceName\":\"my_service\"}}";
  EXPECT_EQ(expected_json, ann.toJson());
}

TEST(ZipkinCoreTypesAnnotationTest, copyConstructor) {
  Endpoint ep(std::string("127.0.0.1"), 3306, std::string("my_service"));
  uint64_t timestamp = Util::timeSinceEpochMicro();
  Annotation ann(timestamp, ZipkinCoreConstants::CLIENT_SEND, ep);
  Annotation ann2(ann);

  EXPECT_EQ(ann.value(), ann2.value());
  EXPECT_EQ(ann.timestamp(), ann2.timestamp());
  EXPECT_EQ(ann.isSetEndpoint(), ann2.isSetEndpoint());
  EXPECT_EQ(ann.toJson(), ann2.toJson());

  EXPECT_EQ(ann.endpoint().ipv4(), ann2.endpoint().ipv4());
  EXPECT_EQ(ann.endpoint().port(), ann2.endpoint().port());
  EXPECT_EQ(ann.endpoint().serviceName(), ann2.endpoint().serviceName());
}

TEST(ZipkinCoreTypesAnnotationTest, assignmentOperator) {
  Endpoint ep(std::string("127.0.0.1"), 3306, std::string("my_service"));
  uint64_t timestamp = Util::timeSinceEpochMicro();
  Annotation ann(timestamp, ZipkinCoreConstants::CLIENT_SEND, ep);
  Annotation ann2 = ann;

  EXPECT_EQ(ann.value(), ann2.value());
  EXPECT_EQ(ann.timestamp(), ann2.timestamp());
  EXPECT_EQ(ann.isSetEndpoint(), ann2.isSetEndpoint());
  EXPECT_EQ(ann.toJson(), ann2.toJson());

  EXPECT_EQ(ann.endpoint().ipv4(), ann2.endpoint().ipv4());
  EXPECT_EQ(ann.endpoint().port(), ann2.endpoint().port());
  EXPECT_EQ(ann.endpoint().serviceName(), ann2.endpoint().serviceName());
}

TEST(ZipkinCoreTypesBinaryAnnotationTest, defaultConstructor) {
  BinaryAnnotation ann;

  EXPECT_EQ("", ann.key());
  EXPECT_EQ("", ann.value());
  EXPECT_FALSE(ann.isSetEndpoint());
  EXPECT_EQ(AnnotationType::STRING, ann.annotationType());

  ann.setKey("key");
  EXPECT_EQ("key", ann.key());

  ann.setValue("value");
  EXPECT_EQ("value", ann.value());

  std::string expected_json = "{\"key\":\"key\",\"value\":\"value\"}";
  EXPECT_EQ(expected_json, ann.toJson());

  // Test the copy-semantics flavor of setEndpoint

  Endpoint ep(std::string("127.0.0.1"), 3306, std::string("my_service"));
  ann.setEndpoint(ep);
  EXPECT_TRUE(ann.isSetEndpoint());
  EXPECT_EQ("127.0.0.1", ann.endpoint().ipv4());
  EXPECT_EQ(3306, ann.endpoint().port());
  EXPECT_EQ("my_service", ann.endpoint().serviceName());
  EXPECT_FALSE(ann.endpoint().isSetIpv6());
  EXPECT_EQ("{\"ipv4\":\"127.0.0.1\",\"port\":3306,\"serviceName\":\"my_service\"}",
            (const_cast<Endpoint&>(ann.endpoint())).toJson());

  expected_json = "{\"key\":\"key\",\"value\":\"value\",\"endpoint\":{\"ipv4\":"
                  "\"127.0.0.1\",\"port\":3306,\"serviceName\":\"my_service\"}}";
  EXPECT_EQ(expected_json, ann.toJson());

  // Test the move-semantics flavor of setEndpoint

  Endpoint ep2(std::string("192.168.1.1"), 5555, std::string("my_service_2"));
  ann.setEndpoint(std::move(ep2));
  EXPECT_TRUE(ann.isSetEndpoint());
  EXPECT_EQ("192.168.1.1", ann.endpoint().ipv4());
  EXPECT_EQ(5555, ann.endpoint().port());
  EXPECT_EQ("my_service_2", ann.endpoint().serviceName());
  EXPECT_FALSE(ann.endpoint().isSetIpv6());
  EXPECT_EQ("{\"ipv4\":\"192.168.1.1\",\"port\":5555,\"serviceName\":\"my_service_2\"}",
            (const_cast<Endpoint&>(ann.endpoint())).toJson());

  expected_json = "{\"key\":\"key\",\"value\":\"value\",\"endpoint\":{\"ipv4\":"
                  "\"192.168.1.1\",\"port\":5555,\"serviceName\":\"my_service_2\"}}";
  EXPECT_EQ(expected_json, ann.toJson());
}

TEST(ZipkinCoreTypesBinaryAnnotationTest, customConstructor) {
  BinaryAnnotation ann("key", "value");

  EXPECT_EQ("key", ann.key());
  EXPECT_EQ("value", ann.value());
  EXPECT_FALSE(ann.isSetEndpoint());
  EXPECT_EQ(AnnotationType::STRING, ann.annotationType());
  std::string expected_json = "{\"key\":\"key\",\"value\":\"value\"}";
  EXPECT_EQ(expected_json, ann.toJson());
}

TEST(ZipkinCoreTypesBinaryAnnotationTest, copyConstructor) {
  BinaryAnnotation ann("key", "value");
  BinaryAnnotation ann2(ann);

  EXPECT_EQ(ann.value(), ann2.value());
  EXPECT_EQ(ann.key(), ann2.key());
  EXPECT_EQ(ann.isSetEndpoint(), ann2.isSetEndpoint());
  EXPECT_EQ(ann.toJson(), ann2.toJson());
  EXPECT_EQ(ann.annotationType(), ann2.annotationType());
}

TEST(ZipkinCoreTypesBinaryAnnotationTest, assignmentOperator) {
  BinaryAnnotation ann("key", "value");
  BinaryAnnotation ann2 = ann;

  EXPECT_EQ(ann.value(), ann2.value());
  EXPECT_EQ(ann.key(), ann2.key());
  EXPECT_EQ(ann.isSetEndpoint(), ann2.isSetEndpoint());
  EXPECT_EQ(ann.toJson(), ann2.toJson());
  EXPECT_EQ(ann.annotationType(), ann2.annotationType());
}

TEST(ZipkinCoreTypesSpanTest, defaultConstructor) {
  Span span;

  EXPECT_EQ(0ULL, span.id());
  EXPECT_EQ(0ULL, span.parentId());
  EXPECT_EQ(0ULL, span.traceId());
  EXPECT_EQ(0ULL, span.traceIdHigh());
  EXPECT_EQ("", span.name());
  EXPECT_EQ(0ULL, span.annotations().size());
  EXPECT_EQ(0ULL, span.binaryAnnotations().size());
  EXPECT_EQ("0000000000000000", span.idAsHexString());
  EXPECT_EQ("0000000000000000", span.parentIdAsHexString());
  EXPECT_EQ("0000000000000000", span.traceIdAsHexString());
  EXPECT_EQ(0LL, span.timestamp());
  EXPECT_EQ(0LL, span.duration());
  EXPECT_EQ(0LL, span.startTime());
  EXPECT_FALSE(span.isSet().debug);
  EXPECT_FALSE(span.isSet().duration);
  EXPECT_FALSE(span.isSet().parent_id);
  EXPECT_FALSE(span.isSet().timestamp);
  EXPECT_FALSE(span.isSet().trace_id_high);
  EXPECT_EQ("{\"traceId\":\"0000000000000000\",\"name\":\"\",\"id\":\"0000000000000000\","
            "\"annotations\":[],\"binaryAnnotations\":[]}",
            span.toJson());

  uint64_t id = Util::generateRandom64();
  std::string id_hex = Util::uint64ToBase16(id);
  span.setId(id);
  EXPECT_EQ(id, span.id());
  EXPECT_EQ(id_hex, span.idAsHexString());

  id = Util::generateRandom64();
  id_hex = Util::uint64ToBase16(id);
  span.setParentId(id);
  EXPECT_EQ(id, span.parentId());
  EXPECT_EQ(id_hex, span.parentIdAsHexString());
  EXPECT_TRUE(span.isSet().parent_id);

  id = Util::generateRandom64();
  id_hex = Util::uint64ToBase16(id);
  span.setTraceId(id);
  EXPECT_EQ(id, span.traceId());
  EXPECT_EQ(id_hex, span.traceIdAsHexString());

  id = Util::generateRandom64();
  id_hex = Util::uint64ToBase16(id);
  span.setTraceIdHigh(id);
  EXPECT_EQ(id, span.traceIdHigh());
  EXPECT_TRUE(span.isSet().trace_id_high);

  int64_t timestamp = Util::timeSinceEpochMicro();
  span.setTimestamp(timestamp);
  EXPECT_EQ(timestamp, span.timestamp());
  EXPECT_TRUE(span.isSet().timestamp);

  span.setStartTime(timestamp);
  EXPECT_EQ(timestamp, span.startTime());

  span.setDuration(3000LL);
  EXPECT_EQ(3000LL, span.duration());
  EXPECT_TRUE(span.isSet().duration);

  span.setName("span_name");
  EXPECT_EQ("span_name", span.name());

  span.setDebug();
  EXPECT_TRUE(span.isSet().debug);

  Endpoint endpoint;
  Annotation ann;
  BinaryAnnotation bann;
  std::vector<Zipkin::Annotation> annotations;
  std::vector<Zipkin::BinaryAnnotation> bAnnotations;

  endpoint.setServiceName("my_service_name");
  std::string ip = "192.168.1.2";
  endpoint.setIpv4(ip);
  endpoint.setPort(3306);

  ann.setValue(Zipkin::ZipkinCoreConstants::CLIENT_SEND);
  ann.setTimestamp(timestamp);
  ann.setEndpoint(endpoint);

  annotations.push_back(ann);
  span.setAannotations(annotations);
  EXPECT_EQ(1ULL, span.annotations().size());

  bann.setKey(Zipkin::ZipkinCoreConstants::LOCAL_COMPONENT);
  bann.setValue("my_component_name");
  bann.setEndpoint(endpoint);

  bAnnotations.push_back(bann);
  span.setBinaryAnnotations(bAnnotations);
  EXPECT_EQ(1ULL, span.binaryAnnotations().size());

  EXPECT_EQ(
      "{\"traceId\":\"" + span.traceIdAsHexString() + "\",\"name\":\"span_name\",\"id\":\"" +
          span.idAsHexString() + "\",\"parentId\":\"" + span.parentIdAsHexString() +
          "\",\"timestamp\":" + std::to_string(span.timestamp()) + ",\"duration\":3000,"
                                                                   "\"annotations\":["
                                                                   "{\"timestamp\":" +
          std::to_string(span.timestamp()) +
          ",\"value\":\"cs\",\"endpoint\":"
          "{\"ipv4\":\"192.168.1.2\",\"port\":3306,\"serviceName\":\"my_service_name\"}}],"
          "\"binaryAnnotations\":[{\"key\":\"lc\",\"value\":\"my_component_name\",\"endpoint\":"
          "{\"ipv4\":\"192.168.1.2\",\"port\":3306,\"serviceName\":\"my_service_name\"}}]}",
      span.toJson());

  // Test the copy-semantics flavor of addAnnotation and addBinaryAnnotation

  ann.setValue(Zipkin::ZipkinCoreConstants::SERVER_SEND);
  span.addAnnotation(ann);
  bann.setKey("http.return_code");
  bann.setValue("200");
  span.addBinaryAnnotation(bann);

  EXPECT_EQ(2ULL, span.annotations().size());
  EXPECT_EQ(2ULL, span.binaryAnnotations().size());

  // Test the move-semantics flavor of addAnnotation and addBinaryAnnotation

  ann.setValue(Zipkin::ZipkinCoreConstants::SERVER_RECV);
  span.addAnnotation(std::move(ann));
  bann.setKey("http.return_code");
  bann.setValue("400");
  span.addBinaryAnnotation(std::move(bann));

  EXPECT_EQ(3ULL, span.annotations().size());
  EXPECT_EQ(3ULL, span.binaryAnnotations().size());

  EXPECT_EQ("{\"traceId\":\"" + span.traceIdAsHexString() + "\",\"name\":\"span_name\",\"id\":\"" +
                span.idAsHexString() + "\",\"parentId\":\"" + span.parentIdAsHexString() +
                "\",\"timestamp\":" + std::to_string(span.timestamp()) + ",\"duration\":3000,"
                                                                         "\"annotations\":["
                                                                         "{\"timestamp\":" +
                std::to_string(timestamp) +
                ",\"value\":\"cs\",\"endpoint\":"
                "{\"ipv4\":\"192.168.1.2\",\"port\":3306,\"serviceName\":\"my_service_name\"}},"
                "{\"timestamp\":" +
                std::to_string(timestamp) + ",\"value\":\"ss\","
                                            "\"endpoint\":{\"ipv4\":\"192.168.1.2\",\"port\":3306,"
                                            "\"serviceName\":\"my_service_name\"}},"
                                            "{\"timestamp\":" +
                std::to_string(timestamp) +
                ",\"value\":\"sr\","
                "\"endpoint\":{\"ipv4\":\"192.168.1.2\",\"port\":3306,\"serviceName\":\"my_service_"
                "name\"}}],"
                "\"binaryAnnotations\":[{\"key\":\"lc\",\"value\":\"my_component_name\","
                "\"endpoint\":{\"ipv4\":\"192.168.1.2\",\"port\":3306,\"serviceName\":\"my_service_"
                "name\"}},"
                "{\"key\":\"http.return_code\",\"value\":\"200\","
                "\"endpoint\":{\"ipv4\":\"192.168.1.2\",\"port\":3306,\"serviceName\":\"my_service_"
                "name\"}},"
                "{\"key\":\"http.return_code\",\"value\":\"400\","
                "\"endpoint\":{\"ipv4\":\"192.168.1.2\",\"port\":3306,\"serviceName\":\"my_service_"
                "name\"}}]}",
            span.toJson());
}

TEST(ZipkinCoreTypesSpanTest, copyConstructor) {
  Span span;

  uint64_t id = Util::generateRandom64();
  std::string id_hex = Util::uint64ToBase16(id);
  span.setId(id);
  span.setParentId(id);
  span.setTraceId(id);
  int64_t timestamp = Util::timeSinceEpochMicro();
  span.setTimestamp(timestamp);
  span.setDuration(3000LL);
  span.setName("span_name");

  Span span2(span);

  EXPECT_EQ(span.id(), span2.id());
  EXPECT_EQ(span.parentId(), span2.parentId());
  EXPECT_EQ(span.traceId(), span2.traceId());
  EXPECT_EQ(span.traceIdHigh(), span2.traceIdHigh());
  EXPECT_EQ(span.name(), span2.name());
  EXPECT_EQ(span.annotations().size(), span2.annotations().size());
  EXPECT_EQ(span.binaryAnnotations().size(), span2.binaryAnnotations().size());
  EXPECT_EQ(span.idAsHexString(), span2.idAsHexString());
  EXPECT_EQ(span.parentIdAsHexString(), span2.parentIdAsHexString());
  EXPECT_EQ(span.traceIdAsHexString(), span2.traceIdAsHexString());
  EXPECT_EQ(span.timestamp(), span2.timestamp());
  EXPECT_EQ(span.duration(), span2.duration());
  EXPECT_EQ(span.startTime(), span2.startTime());
  EXPECT_EQ(span.isSet().debug, span2.isSet().debug);
  EXPECT_EQ(span.isSet().duration, span2.isSet().duration);
  EXPECT_EQ(span.isSet().parent_id, span2.isSet().parent_id);
  EXPECT_EQ(span.isSet().timestamp, span2.isSet().timestamp);
  EXPECT_EQ(span.isSet().trace_id_high, span2.isSet().trace_id_high);
}

TEST(ZipkinCoreTypesSpanTest, assignmentOperator) {
  Span span;

  uint64_t id = Util::generateRandom64();
  std::string id_hex = Util::uint64ToBase16(id);
  span.setId(id);
  span.setParentId(id);
  span.setTraceId(id);
  int64_t timestamp = Util::timeSinceEpochMicro();
  span.setTimestamp(timestamp);
  span.setDuration(3000LL);
  span.setName("span_name");

  Span span2 = span;

  EXPECT_EQ(span.id(), span2.id());
  EXPECT_EQ(span.parentId(), span2.parentId());
  EXPECT_EQ(span.traceId(), span2.traceId());
  EXPECT_EQ(span.traceIdHigh(), span2.traceIdHigh());
  EXPECT_EQ(span.name(), span2.name());
  EXPECT_EQ(span.annotations().size(), span2.annotations().size());
  EXPECT_EQ(span.binaryAnnotations().size(), span2.binaryAnnotations().size());
  EXPECT_EQ(span.idAsHexString(), span2.idAsHexString());
  EXPECT_EQ(span.parentIdAsHexString(), span2.parentIdAsHexString());
  EXPECT_EQ(span.traceIdAsHexString(), span2.traceIdAsHexString());
  EXPECT_EQ(span.timestamp(), span2.timestamp());
  EXPECT_EQ(span.duration(), span2.duration());
  EXPECT_EQ(span.startTime(), span2.startTime());
  EXPECT_EQ(span.isSet().debug, span2.isSet().debug);
  EXPECT_EQ(span.isSet().duration, span2.isSet().duration);
  EXPECT_EQ(span.isSet().parent_id, span2.isSet().parent_id);
  EXPECT_EQ(span.isSet().timestamp, span2.isSet().timestamp);
  EXPECT_EQ(span.isSet().trace_id_high, span2.isSet().trace_id_high);
}

TEST(ZipkinCoreTypesSpanTest, setTag) {
  Span span;

  span.setTag("key1", "value1");
  span.setTag("key2", "value2");

  EXPECT_EQ(2ULL, span.binaryAnnotations().size());

  BinaryAnnotation bann = span.binaryAnnotations()[0];
  EXPECT_EQ("key1", bann.key());
  EXPECT_EQ("value1", bann.value());

  bann = span.binaryAnnotations()[1];
  EXPECT_EQ("key2", bann.key());
  EXPECT_EQ("value2", bann.value());
}
} // Zipkin
