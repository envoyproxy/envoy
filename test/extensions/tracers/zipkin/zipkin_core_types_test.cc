#include "common/common/utility.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"

#include "extensions/tracers/zipkin/zipkin_core_constants.h"
#include "extensions/tracers/zipkin/zipkin_core_types.h"

#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Zipkin {
namespace {

TEST(ZipkinCoreTypesEndpointTest, defaultConstructor) {
  Endpoint ep;
  Util::Replacements replacements;

  EXPECT_EQ("", ep.serviceName());
  EXPECT_TRUE(
      TestUtility::protoEqual(TestUtility::jsonToStruct(R"({"ipv4":"","port":0,"serviceName":""})"),
                              ep.toStruct(replacements)));

  Network::Address::InstanceConstSharedPtr addr =
      Network::Utility::parseInternetAddress("127.0.0.1");
  ep.setAddress(addr);
  EXPECT_TRUE(TestUtility::protoEqual(
      TestUtility::jsonToStruct(R"({"ipv4":"127.0.0.1","port":0,"serviceName":""})"),
      ep.toStruct(replacements)));

  addr = Network::Utility::parseInternetAddressAndPort(
      "[2001:0db8:85a3:0000:0000:8a2e:0370:4444]:7334");
  ep.setAddress(addr);
  EXPECT_TRUE(TestUtility::protoEqual(
      TestUtility::jsonToStruct(
          R"({"ipv6":"2001:db8:85a3::8a2e:370:4444","port":7334,"serviceName":""})"),
      ep.toStruct(replacements)));
  EXPECT_TRUE(replacements.empty());

  ep.setServiceName("my_service");
  EXPECT_EQ("my_service", ep.serviceName());

  EXPECT_TRUE(TestUtility::protoEqual(
      TestUtility::jsonToStruct(
          R"({"ipv6":"2001:db8:85a3::8a2e:370:4444","port":7334,"serviceName":"my_service"})"),
      ep.toStruct(replacements)));
  EXPECT_TRUE(replacements.empty());
}

TEST(ZipkinCoreTypesEndpointTest, customConstructor) {
  Network::Address::InstanceConstSharedPtr addr =
      Network::Utility::parseInternetAddressAndPort("127.0.0.1:3306");
  Endpoint ep(std::string("my_service"), addr);
  Util::Replacements replacements;

  EXPECT_EQ("my_service", ep.serviceName());
  EXPECT_TRUE(TestUtility::protoEqual(
      TestUtility::jsonToStruct(R"({"ipv4":"127.0.0.1","port":3306,"serviceName":"my_service"})"),
      ep.toStruct(replacements)));
  EXPECT_TRUE(replacements.empty());

  addr = Network::Utility::parseInternetAddressAndPort(
      "[2001:0db8:85a3:0000:0000:8a2e:0370:4444]:7334");
  ep.setAddress(addr);

  EXPECT_TRUE(TestUtility::protoEqual(
      TestUtility::jsonToStruct(
          R"({"ipv6":"2001:db8:85a3::8a2e:370:4444","port":7334,"serviceName":"my_service"})"),
      ep.toStruct(replacements)));
  EXPECT_TRUE(replacements.empty());
}

TEST(ZipkinCoreTypesEndpointTest, copyOperator) {
  Network::Address::InstanceConstSharedPtr addr =
      Network::Utility::parseInternetAddressAndPort("127.0.0.1:3306");
  Endpoint ep1(std::string("my_service"), addr);
  Endpoint& ep2(ep1);
  Util::Replacements replacements;

  EXPECT_EQ("my_service", ep1.serviceName());
  EXPECT_TRUE(TestUtility::protoEqual(
      TestUtility::jsonToStruct(R"({"ipv4":"127.0.0.1","port":3306,"serviceName":"my_service"})"),
      ep1.toStruct(replacements)));

  EXPECT_EQ(ep1.serviceName(), ep2.serviceName());
  EXPECT_TRUE(TestUtility::protoEqual(ep1.toStruct(replacements), ep2.toStruct(replacements)));
  EXPECT_TRUE(replacements.empty());
}

TEST(ZipkinCoreTypesEndpointTest, assignmentOperator) {
  Network::Address::InstanceConstSharedPtr addr =
      Network::Utility::parseInternetAddressAndPort("127.0.0.1:3306");
  Endpoint ep1(std::string("my_service"), addr);
  Endpoint& ep2 = ep1;
  Util::Replacements replacements;

  EXPECT_EQ("my_service", ep1.serviceName());
  EXPECT_TRUE(TestUtility::protoEqual(
      TestUtility::jsonToStruct(R"({"ipv4":"127.0.0.1","port":3306,"serviceName":"my_service"})"),
      ep1.toStruct(replacements)));

  EXPECT_EQ(ep1.serviceName(), ep2.serviceName());
  EXPECT_TRUE(TestUtility::protoEqual(ep1.toStruct(replacements), ep2.toStruct(replacements)));
  EXPECT_TRUE(replacements.empty());
}

TEST(ZipkinCoreTypesAnnotationTest, defaultConstructor) {
  Annotation ann;
  Util::Replacements replacements;

  EXPECT_EQ(0ULL, ann.timestamp());
  EXPECT_EQ("", ann.value());
  EXPECT_FALSE(ann.isSetEndpoint());

  Event::SimulatedTimeSystem test_time;
  uint64_t timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                           test_time.timeSystem().systemTime().time_since_epoch())
                           .count();
  ann.setTimestamp(timestamp);
  EXPECT_EQ(timestamp, ann.timestamp());

  ann.setValue(CLIENT_SEND);
  EXPECT_EQ(CLIENT_SEND, ann.value());

  std::string expected_json = R"({"timestamp":")" + std::to_string(timestamp) + R"(")" +
                              R"(,"value":")" + CLIENT_SEND + R"("})";
  EXPECT_TRUE(TestUtility::protoEqual(TestUtility::jsonToStruct(expected_json),
                                      ann.toStruct(replacements)));
  EXPECT_EQ(1, replacements.size());

  replacements.clear();
  // Test the copy-semantics flavor of setEndpoint
  Network::Address::InstanceConstSharedPtr addr =
      Network::Utility::parseInternetAddressAndPort("127.0.0.1:3306");
  Endpoint ep(std::string("my_service"), addr);
  ann.setEndpoint(ep);
  EXPECT_TRUE(ann.isSetEndpoint());
  EXPECT_EQ("my_service", ann.endpoint().serviceName());
  EXPECT_TRUE(TestUtility::protoEqual(
      TestUtility::jsonToStruct(R"({"ipv4":"127.0.0.1","port":3306,"serviceName":"my_service"})"),
      (const_cast<Endpoint&>(ann.endpoint())).toStruct(replacements)));

  expected_json = R"({"timestamp":")" + std::to_string(timestamp) + R"(")" + R"(,"value":")" +
                  CLIENT_SEND +
                  R"(","endpoint":{"ipv4":)"
                  R"("127.0.0.1","port":3306,"serviceName":"my_service"}})";
  EXPECT_TRUE(TestUtility::protoEqual(TestUtility::jsonToStruct(expected_json),
                                      ann.toStruct(replacements)));

  EXPECT_EQ(1, replacements.size());

  replacements.clear();
  // Test the move-semantics flavor of setEndpoint
  addr = Network::Utility::parseInternetAddressAndPort("192.168.1.1:5555");
  Endpoint ep2(std::string("my_service_2"), addr);
  ann.setEndpoint(std::move(ep2));
  EXPECT_TRUE(ann.isSetEndpoint());
  EXPECT_EQ("my_service_2", ann.endpoint().serviceName());
  EXPECT_TRUE(TestUtility::protoEqual(
      TestUtility::jsonToStruct(
          R"({"ipv4":"192.168.1.1","port":5555,"serviceName":"my_service_2"})"),
      (const_cast<Endpoint&>(ann.endpoint())).toStruct(replacements)));
  EXPECT_TRUE(replacements.empty());

  replacements.clear();
  expected_json = R"({"timestamp":")" + std::to_string(timestamp) + R"(")" + R"(,"value":")" +
                  CLIENT_SEND +
                  R"(","endpoint":{"ipv4":"192.168.1.1",)"
                  R"("port":5555,"serviceName":"my_service_2"}})";
  EXPECT_TRUE(TestUtility::protoEqual(TestUtility::jsonToStruct(expected_json),
                                      ann.toStruct(replacements)));
  EXPECT_EQ(1, replacements.size());

  replacements.clear();
  // Test change endpoint service name.
  ann.changeEndpointServiceName("NEW_SERVICE_NAME");
  EXPECT_EQ("NEW_SERVICE_NAME", ann.endpoint().serviceName());
  expected_json = R"({"timestamp":")" + std::to_string(timestamp) + R"(")" + R"(,"value":")" +
                  CLIENT_SEND +
                  R"(","endpoint":{"ipv4":"192.168.1.1",)"
                  R"("port":5555,"serviceName":"NEW_SERVICE_NAME"}})";
  EXPECT_TRUE(TestUtility::protoEqual(TestUtility::jsonToStruct(expected_json),
                                      ann.toStruct(replacements)));
  EXPECT_EQ(1, replacements.size());
}

TEST(ZipkinCoreTypesAnnotationTest, customConstructor) {
  Network::Address::InstanceConstSharedPtr addr =
      Network::Utility::parseInternetAddressAndPort("127.0.0.1:3306");
  Endpoint ep(std::string("my_service"), addr);
  Event::SimulatedTimeSystem test_time;
  uint64_t timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                           test_time.timeSystem().systemTime().time_since_epoch())
                           .count();
  Annotation ann(timestamp, CLIENT_SEND, ep);
  Util::Replacements replacements;

  EXPECT_EQ(timestamp, ann.timestamp());
  EXPECT_EQ(CLIENT_SEND, ann.value());
  EXPECT_TRUE(ann.isSetEndpoint());

  EXPECT_EQ("my_service", ann.endpoint().serviceName());
  EXPECT_TRUE(TestUtility::protoEqual(
      TestUtility::jsonToStruct(R"({"ipv4":"127.0.0.1","port":3306,"serviceName":"my_service"})"),
      (const_cast<Endpoint&>(ann.endpoint())).toStruct(replacements)));
  EXPECT_TRUE(replacements.empty());

  std::string expected_json = R"({"timestamp":")" + std::to_string(timestamp) + R"(")" +
                              R"(,"value":")" + CLIENT_SEND +
                              R"(","endpoint":{"ipv4":"127.0.0.1",)"
                              R"("port":3306,"serviceName":"my_service"}})";
  EXPECT_TRUE(TestUtility::protoEqual(TestUtility::jsonToStruct(expected_json),
                                      ann.toStruct(replacements)));
  EXPECT_EQ(1, replacements.size());
}

TEST(ZipkinCoreTypesAnnotationTest, copyConstructor) {
  Network::Address::InstanceConstSharedPtr addr =
      Network::Utility::parseInternetAddressAndPort("127.0.0.1:3306");
  Endpoint ep(std::string("my_service"), addr);
  Event::SimulatedTimeSystem test_time;
  uint64_t timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                           test_time.timeSystem().systemTime().time_since_epoch())
                           .count();
  Annotation ann(timestamp, CLIENT_SEND, ep);
  Annotation& ann2(ann);
  Util::Replacements replacements;

  EXPECT_EQ(ann.value(), ann2.value());
  EXPECT_EQ(ann.timestamp(), ann2.timestamp());
  EXPECT_EQ(ann.isSetEndpoint(), ann2.isSetEndpoint());
  EXPECT_TRUE(TestUtility::protoEqual(ann.toStruct(replacements), ann2.toStruct(replacements)));
  EXPECT_EQ(ann.endpoint().serviceName(), ann2.endpoint().serviceName());
}

TEST(ZipkinCoreTypesAnnotationTest, assignmentOperator) {
  Network::Address::InstanceConstSharedPtr addr =
      Network::Utility::parseInternetAddressAndPort("127.0.0.1:3306");
  Endpoint ep(std::string("my_service"), addr);
  Event::SimulatedTimeSystem test_time;
  uint64_t timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                           test_time.timeSystem().systemTime().time_since_epoch())
                           .count();
  Annotation ann(timestamp, CLIENT_SEND, ep);
  Annotation& ann2 = ann;
  Util::Replacements replacements;

  EXPECT_EQ(ann.value(), ann2.value());
  EXPECT_EQ(ann.timestamp(), ann2.timestamp());
  EXPECT_EQ(ann.isSetEndpoint(), ann2.isSetEndpoint());
  EXPECT_TRUE(TestUtility::protoEqual(ann.toStruct(replacements), ann2.toStruct(replacements)));
  EXPECT_EQ(ann.endpoint().serviceName(), ann2.endpoint().serviceName());
}

TEST(ZipkinCoreTypesBinaryAnnotationTest, defaultConstructor) {
  BinaryAnnotation ann;
  Util::Replacements replacements;

  EXPECT_EQ("", ann.key());
  EXPECT_EQ("", ann.value());
  EXPECT_FALSE(ann.isSetEndpoint());
  EXPECT_EQ(AnnotationType::STRING, ann.annotationType());

  ann.setKey("key");
  EXPECT_EQ("key", ann.key());

  ann.setValue("value");
  EXPECT_EQ("value", ann.value());

  std::string expected_json = R"({"key":"key","value":"value"})";
  EXPECT_TRUE(TestUtility::protoEqual(TestUtility::jsonToStruct(expected_json),
                                      ann.toStruct(replacements)));

  // Test the copy-semantics flavor of setEndpoint

  Network::Address::InstanceConstSharedPtr addr =
      Network::Utility::parseInternetAddressAndPort("127.0.0.1:3306");
  Endpoint ep(std::string("my_service"), addr);
  ann.setEndpoint(ep);
  EXPECT_TRUE(ann.isSetEndpoint());
  EXPECT_EQ("my_service", ann.endpoint().serviceName());
  EXPECT_TRUE(TestUtility::protoEqual(
      TestUtility::jsonToStruct(R"({"ipv4":"127.0.0.1","port":3306,"serviceName":"my_service"})"),
      (const_cast<Endpoint&>(ann.endpoint())).toStruct(replacements)));

  expected_json = "{"
                  R"("key":"key","value":"value",)"
                  R"("endpoint":)"
                  R"({"ipv4":"127.0.0.1","port":3306,"serviceName":"my_service"})"
                  "}";
  EXPECT_TRUE(TestUtility::protoEqual(TestUtility::jsonToStruct(expected_json),
                                      ann.toStruct(replacements)));

  // Test the move-semantics flavor of setEndpoint
  addr = Network::Utility::parseInternetAddressAndPort("192.168.1.1:5555");
  Endpoint ep2(std::string("my_service_2"), addr);
  ann.setEndpoint(ep2);
  EXPECT_TRUE(ann.isSetEndpoint());
  EXPECT_EQ("my_service_2", ann.endpoint().serviceName());
  EXPECT_TRUE(TestUtility::protoEqual(
      TestUtility::jsonToStruct(
          R"({"ipv4":"192.168.1.1","port":5555,"serviceName":"my_service_2"})"),
      (const_cast<Endpoint&>(ann.endpoint())).toStruct(replacements)));
  expected_json = "{"
                  R"("key":"key","value":"value",)"
                  R"("endpoint":)"
                  R"({"ipv4":"192.168.1.1","port":5555,"serviceName":"my_service_2"})"
                  "}";
  EXPECT_TRUE(TestUtility::protoEqual(TestUtility::jsonToStruct(expected_json),
                                      ann.toStruct(replacements)));
}

TEST(ZipkinCoreTypesBinaryAnnotationTest, customConstructor) {
  BinaryAnnotation ann("key", "value");
  Util::Replacements replacements;

  EXPECT_EQ("key", ann.key());
  EXPECT_EQ("value", ann.value());
  EXPECT_FALSE(ann.isSetEndpoint());
  EXPECT_EQ(AnnotationType::STRING, ann.annotationType());
  std::string expected_json = R"({"key":"key","value":"value"})";
  EXPECT_TRUE(TestUtility::protoEqual(TestUtility::jsonToStruct(expected_json),
                                      ann.toStruct(replacements)));
}

TEST(ZipkinCoreTypesBinaryAnnotationTest, copyConstructor) {
  BinaryAnnotation ann("key", "value");
  BinaryAnnotation& ann2(ann);
  Util::Replacements replacements;

  EXPECT_EQ(ann.value(), ann2.value());
  EXPECT_EQ(ann.key(), ann2.key());
  EXPECT_EQ(ann.isSetEndpoint(), ann2.isSetEndpoint());
  EXPECT_TRUE(TestUtility::protoEqual(ann.toStruct(replacements), ann2.toStruct(replacements)));
  EXPECT_EQ(ann.annotationType(), ann2.annotationType());
}

TEST(ZipkinCoreTypesBinaryAnnotationTest, assignmentOperator) {
  BinaryAnnotation ann("key", "value");
  BinaryAnnotation& ann2 = ann;
  Util::Replacements replacements;

  EXPECT_EQ(ann.value(), ann2.value());
  EXPECT_EQ(ann.key(), ann2.key());
  EXPECT_EQ(ann.isSetEndpoint(), ann2.isSetEndpoint());
  EXPECT_TRUE(TestUtility::protoEqual(ann.toStruct(replacements), ann2.toStruct(replacements)));
  EXPECT_EQ(ann.annotationType(), ann2.annotationType());
}

TEST(ZipkinCoreTypesSpanTest, defaultConstructor) {
  Event::SimulatedTimeSystem test_time;
  Span span(test_time.timeSystem());
  Util::Replacements replacements;

  EXPECT_EQ(0ULL, span.id());
  EXPECT_EQ(0ULL, span.traceId());
  EXPECT_EQ("", span.name());
  EXPECT_EQ(0ULL, span.annotations().size());
  EXPECT_EQ(0ULL, span.binaryAnnotations().size());
  EXPECT_EQ("0000000000000000", span.idAsHexString());
  EXPECT_EQ("0000000000000000", span.parentIdAsHexString());
  EXPECT_EQ("0000000000000000", span.traceIdAsHexString());
  EXPECT_EQ(0LL, span.startTime());
  EXPECT_FALSE(span.debug());
  EXPECT_FALSE(span.isSetDuration());
  EXPECT_FALSE(span.isSetParentId());
  EXPECT_FALSE(span.isSetTimestamp());
  EXPECT_FALSE(span.isSetTraceIdHigh());
  EXPECT_TRUE(TestUtility::protoEqual(
      TestUtility::jsonToStruct(
          R"({"traceId":"0000000000000000","name":"","id":"0000000000000000"})"),
      span.toStruct(replacements)));

  uint64_t id = Util::generateRandom64(test_time.timeSystem());
  std::string id_hex = Hex::uint64ToHex(id);
  span.setId(id);
  EXPECT_EQ(id, span.id());
  EXPECT_EQ(id_hex, span.idAsHexString());

  id = Util::generateRandom64(test_time.timeSystem());
  id_hex = Hex::uint64ToHex(id);
  span.setParentId(id);
  EXPECT_EQ(id, span.parentId());
  EXPECT_EQ(id_hex, span.parentIdAsHexString());
  EXPECT_TRUE(span.isSetParentId());

  id = Util::generateRandom64(test_time.timeSystem());
  id_hex = Hex::uint64ToHex(id);
  span.setTraceId(id);
  EXPECT_EQ(id, span.traceId());
  EXPECT_EQ(id_hex, span.traceIdAsHexString());

  id = Util::generateRandom64(test_time.timeSystem());
  id_hex = Hex::uint64ToHex(id);
  span.setTraceIdHigh(id);
  EXPECT_EQ(id, span.traceIdHigh());
  EXPECT_TRUE(span.isSetTraceIdHigh());

  int64_t timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                          test_time.timeSystem().systemTime().time_since_epoch())
                          .count();
  span.setTimestamp(timestamp);
  EXPECT_EQ(timestamp, span.timestamp());
  EXPECT_TRUE(span.isSetTimestamp());

  int64_t start_time = std::chrono::duration_cast<std::chrono::microseconds>(
                           test_time.timeSystem().monotonicTime().time_since_epoch())
                           .count();
  span.setStartTime(start_time);
  EXPECT_EQ(start_time, span.startTime());

  span.setDuration(3000LL);
  EXPECT_EQ(3000LL, span.duration());
  EXPECT_TRUE(span.isSetDuration());

  span.setName("span_name");
  EXPECT_EQ("span_name", span.name());

  span.setDebug();
  EXPECT_TRUE(span.debug());

  Endpoint endpoint;
  Annotation ann;
  BinaryAnnotation bann;
  std::vector<Annotation> annotations;
  std::vector<BinaryAnnotation> binary_annotations;

  endpoint.setServiceName("my_service_name");
  Network::Address::InstanceConstSharedPtr addr =
      Network::Utility::parseInternetAddressAndPort("192.168.1.2:3306");
  endpoint.setAddress(addr);

  ann.setValue(CLIENT_SEND);
  ann.setTimestamp(timestamp);
  ann.setEndpoint(endpoint);

  annotations.push_back(ann);
  span.setAnnotations(annotations);
  EXPECT_EQ(1ULL, span.annotations().size());

  bann.setKey(LOCAL_COMPONENT);
  bann.setValue("my_component_name");
  bann.setEndpoint(endpoint);

  binary_annotations.push_back(bann);
  span.setBinaryAnnotations(binary_annotations);
  EXPECT_EQ(1ULL, span.binaryAnnotations().size());

  EXPECT_TRUE(TestUtility::protoEqual(
      TestUtility::jsonToStruct(
          R"({"traceId":")" + span.traceIdAsHexString() + R"(","name":"span_name","id":")" +
          span.idAsHexString() + R"(","parentId":")" + span.parentIdAsHexString() +
          R"(","timestamp":")" + std::to_string(span.timestamp()) +
          R"(")"
          R"(,"duration":"3000",)"
          R"("annotations":[)"
          R"({"timestamp":")" +
          std::to_string(span.timestamp()) +
          R"(")"
          R"(,"value":"cs","endpoint":)"
          R"({"ipv4":"192.168.1.2","port":3306,"serviceName":"my_service_name"}}],)"
          R"("binaryAnnotations":[{"key":"lc","value":"my_component_name","endpoint":)"
          R"({"ipv4":"192.168.1.2","port":3306,"serviceName":"my_service_name"}}]})"),
      span.toStruct(replacements)));
  EXPECT_EQ(3, replacements.size());

  // Test the copy-semantics flavor of addAnnotation and addBinaryAnnotation

  ann.setValue(SERVER_SEND);
  span.addAnnotation(ann);
  bann.setKey("http.return_code");
  bann.setValue("200");
  span.addBinaryAnnotation(bann);

  EXPECT_EQ(2ULL, span.annotations().size());
  EXPECT_EQ(2ULL, span.binaryAnnotations().size());

  // Test the move-semantics flavor of addAnnotation and addBinaryAnnotation

  ann.setValue(SERVER_RECV);
  Annotation ann_copy(ann);
  span.addAnnotation(std::move(ann));
  bann.setKey("http.return_code");
  bann.setValue("400");
  span.addBinaryAnnotation(std::move(bann));

  EXPECT_EQ(3ULL, span.annotations().size());
  EXPECT_EQ(3ULL, span.binaryAnnotations().size());

  replacements.clear();
  EXPECT_TRUE(TestUtility::protoEqual(
      TestUtility::jsonToStruct(
          R"({"traceId":")" + span.traceIdAsHexString() + R"(","name":"span_name","id":")" +
          span.idAsHexString() + R"(","parentId":")" + span.parentIdAsHexString() +
          R"(","timestamp":")" + std::to_string(span.timestamp()) +
          R"(")"
          R"(,"duration":"3000",)"
          R"("annotations":[)"
          R"({"timestamp":")" +
          std::to_string(timestamp) +
          R"(")"
          R"(,"value":"cs","endpoint":)"
          R"({"ipv4":"192.168.1.2","port":3306,"serviceName":"my_service_name"}},)"
          R"({"timestamp":")" +
          std::to_string(timestamp) +
          R"(")"
          R"(,"value":"ss",)"
          R"("endpoint":{"ipv4":"192.168.1.2","port":3306,)"
          R"("serviceName":"my_service_name"}},)"
          R"({"timestamp":")" +
          std::to_string(timestamp) +
          R"(")"
          R"(,"value":"sr","endpoint":{"ipv4":"192.168.1.2","port":3306,)"
          R"("serviceName":"my_service_name"}}],)"
          R"("binaryAnnotations":[{"key":"lc","value":"my_component_name",)"
          R"("endpoint":{"ipv4":"192.168.1.2","port":3306,)"
          R"("serviceName":"my_service_name"}},)"
          R"({"key":"http.return_code","value":"200",)"
          R"("endpoint":{"ipv4":"192.168.1.2","port":3306,)"
          R"("serviceName":"my_service_name"}},)"
          R"({"key":"http.return_code","value":"400",)"
          R"("endpoint":{"ipv4":"192.168.1.2","port":3306,)"
          R"("serviceName":"my_service_name"}}]})"),
      span.toStruct(replacements)));
  EXPECT_EQ(5, replacements.size());

  // Test setSourceServiceName and setDestinationServiceName

  ann_copy.setValue(CLIENT_RECV);
  span.addAnnotation(ann_copy);
  span.setServiceName("NEW_SERVICE_NAME");
  replacements.clear();
  EXPECT_TRUE(TestUtility::protoEqual(
      TestUtility::jsonToStruct(
          R"({"traceId":")" + span.traceIdAsHexString() + R"(","name":"span_name","id":")" +
          span.idAsHexString() + R"(","parentId":")" + span.parentIdAsHexString() +
          R"(","timestamp":")" + std::to_string(span.timestamp()) +
          R"(")"
          R"(,"duration":"3000",)"
          R"("annotations":[)"
          R"({"timestamp":")" +
          std::to_string(timestamp) +
          R"(")"
          R"(,"value":"cs","endpoint":)"
          R"({"ipv4":"192.168.1.2","port":3306,"serviceName":"NEW_SERVICE_NAME"}},)"
          R"({"timestamp":")" +
          std::to_string(timestamp) +
          R"(")"
          R"(,"value":"ss",)"
          R"("endpoint":{"ipv4":"192.168.1.2","port":3306,)"
          R"("serviceName":"NEW_SERVICE_NAME"}},)"
          R"({"timestamp":")" +
          std::to_string(timestamp) +
          R"(")"
          R"(,"value":"sr","endpoint":{"ipv4":"192.168.1.2","port":3306,)"
          R"("serviceName":"NEW_SERVICE_NAME"}},)"
          R"({"timestamp":")" +
          std::to_string(timestamp) +
          R"(")"
          R"(,"value":"cr","endpoint":)"
          R"({"ipv4":"192.168.1.2","port":3306,"serviceName":"NEW_SERVICE_NAME"}}],)"
          R"("binaryAnnotations":[{"key":"lc","value":"my_component_name",)"
          R"("endpoint":{"ipv4":"192.168.1.2","port":3306,)"
          R"("serviceName":"my_service_name"}},)"
          R"({"key":"http.return_code","value":"200",)"
          R"("endpoint":{"ipv4":"192.168.1.2","port":3306,)"
          R"("serviceName":"my_service_name"}},)"
          R"({"key":"http.return_code","value":"400",)"
          R"("endpoint":{"ipv4":"192.168.1.2","port":3306,)"
          R"("serviceName":"my_service_name"}}]})"),
      span.toStruct(replacements)));
  EXPECT_EQ(6, replacements.size());
}

TEST(ZipkinCoreTypesSpanTest, copyConstructor) {
  Event::SimulatedTimeSystem test_time;
  Span span(test_time.timeSystem());
  Util::Replacements replacements;

  uint64_t id = Util::generateRandom64(test_time.timeSystem());
  std::string id_hex = Hex::uint64ToHex(id);
  span.setId(id);
  span.setParentId(id);
  span.setTraceId(id);
  int64_t timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                          test_time.timeSystem().systemTime().time_since_epoch())
                          .count();
  span.setTimestamp(timestamp);
  span.setDuration(3000LL);
  span.setName("span_name");

  Span span2(span);

  EXPECT_EQ(span.id(), span2.id());
  EXPECT_EQ(span.parentId(), span2.parentId());
  EXPECT_EQ(span.traceId(), span2.traceId());
  EXPECT_EQ(span.name(), span2.name());
  EXPECT_EQ(span.annotations().size(), span2.annotations().size());
  EXPECT_EQ(span.binaryAnnotations().size(), span2.binaryAnnotations().size());
  EXPECT_EQ(span.idAsHexString(), span2.idAsHexString());
  EXPECT_EQ(span.parentIdAsHexString(), span2.parentIdAsHexString());
  EXPECT_EQ(span.traceIdAsHexString(), span2.traceIdAsHexString());
  EXPECT_EQ(span.timestamp(), span2.timestamp());
  EXPECT_EQ(span.duration(), span2.duration());
  EXPECT_EQ(span.startTime(), span2.startTime());
  EXPECT_EQ(span.debug(), span2.debug());
  EXPECT_EQ(span.isSetDuration(), span2.isSetDuration());
  EXPECT_EQ(span.isSetParentId(), span2.isSetParentId());
  EXPECT_EQ(span.isSetTimestamp(), span2.isSetTimestamp());
  EXPECT_EQ(span.isSetTraceIdHigh(), span2.isSetTraceIdHigh());
}

TEST(ZipkinCoreTypesSpanTest, assignmentOperator) {
  Event::SimulatedTimeSystem test_time;
  Span span(test_time.timeSystem());
  Util::Replacements replacements;

  uint64_t id = Util::generateRandom64(test_time.timeSystem());
  std::string id_hex = Hex::uint64ToHex(id);
  span.setId(id);
  span.setParentId(id);
  span.setTraceId(id);
  int64_t timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                          test_time.timeSystem().systemTime().time_since_epoch())
                          .count();
  span.setTimestamp(timestamp);
  span.setDuration(3000LL);
  span.setName("span_name");

  Span span2 = span;

  EXPECT_EQ(span.id(), span2.id());
  EXPECT_EQ(span.parentId(), span2.parentId());
  EXPECT_EQ(span.traceId(), span2.traceId());
  EXPECT_EQ(span.name(), span2.name());
  EXPECT_EQ(span.annotations().size(), span2.annotations().size());
  EXPECT_EQ(span.binaryAnnotations().size(), span2.binaryAnnotations().size());
  EXPECT_EQ(span.idAsHexString(), span2.idAsHexString());
  EXPECT_EQ(span.parentIdAsHexString(), span2.parentIdAsHexString());
  EXPECT_EQ(span.traceIdAsHexString(), span2.traceIdAsHexString());
  EXPECT_EQ(span.timestamp(), span2.timestamp());
  EXPECT_EQ(span.duration(), span2.duration());
  EXPECT_EQ(span.startTime(), span2.startTime());
  EXPECT_EQ(span.debug(), span2.debug());
  EXPECT_EQ(span.isSetDuration(), span2.isSetDuration());
  EXPECT_EQ(span.isSetParentId(), span2.isSetParentId());
  EXPECT_EQ(span.isSetTimestamp(), span2.isSetTimestamp());
  EXPECT_EQ(span.isSetTraceIdHigh(), span2.isSetTraceIdHigh());
}

TEST(ZipkinCoreTypesSpanTest, setTag) {
  Event::SimulatedTimeSystem test_time;
  Span span(test_time.timeSystem());

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

} // namespace
} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
