#include "extensions/filters/http/cache/hazelcast_http_cache/hazelcast_cache_entry.h"

#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "hazelcast/client/HazelcastAll.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace HazelcastHttpCache {

using hazelcast::client::serialization::ObjectDataInput;
using hazelcast::client::serialization::ObjectDataOutput;
using hazelcast::client::serialization::pimpl::DataInput;
using hazelcast::client::serialization::pimpl::DataOutput;
using hazelcast::client::serialization::pimpl::SerializationService;

class SerializationTest : public testing::Test {
protected:
  template <typename T> std::vector<hazelcast::byte> serialize(const T& deserialized) {
    DataOutput data_output;
    ObjectDataOutput object_data_output(data_output, nullptr);
    deserialized.writeData(object_data_output);
    return *object_data_output.toByteArray();
  }

  template <typename T> T deserialize(const std::vector<hazelcast::byte>& serialized) {
    T object;
    SerializationConfig serializationConfig;
    serialization::pimpl::SerializationService serializationService(serializationConfig);
    DataInput data_input(serialized);
    ObjectDataInput objectDataInput(data_input, serializationService.getSerializerHolder());
    object.readData(objectDataInput);
    return object;
  }

  HazelcastHeaderEntry createTestHeader() {
    auto headers = Http::ResponseHeaderMapPtr{
        new Http::TestResponseHeaderMapImpl{{"cache-control", "public, max-age=3600"}}};
    Key key;
    key.set_cluster_name("some_cluster");
    key.set_host("some_host");
    key.set_path("some_path");
    key.set_query("some_query");
    key.set_clear_http(true);
    return HazelcastHeaderEntry(std::move(headers), std::move(key), 2008, 2020);
  }

  HazelcastBodyEntry createTestBody() {
    return HazelcastBodyEntry(
        std::vector<hazelcast::byte>({'h', 'a', 'z', 'e', 'l', 'c', 'a', 's', 't'}), 2008);
  }

  HazelcastResponseEntry createTestResponse() {
    return HazelcastResponseEntry(createTestHeader(), createTestBody());
  }

};

TEST_F(SerializationTest, HeaderEntry) {
  HazelcastHeaderEntry original_header = createTestHeader();
  auto serialized = serialize<HazelcastHeaderEntry>(original_header);
  HazelcastHeaderEntry new_header = deserialize<HazelcastHeaderEntry>(serialized);
  EXPECT_EQ(original_header, new_header);
}

TEST_F(SerializationTest, BodyEntry) {
  HazelcastBodyEntry original_body = createTestBody();
  auto serialized = serialize<HazelcastBodyEntry>(original_body);
  HazelcastBodyEntry new_body = deserialize<HazelcastBodyEntry>(serialized);
  EXPECT_EQ(original_body, new_body);
}

TEST_F(SerializationTest, ResponseEntry) {
  HazelcastResponseEntry original_response = createTestResponse();
  auto serialized = serialize<HazelcastResponseEntry>(original_response);
  HazelcastResponseEntry new_response = deserialize<HazelcastResponseEntry>(serialized);
  EXPECT_EQ(original_response, new_response);
}

TEST_F(SerializationTest, SerializerId) {
  HazelcastHeaderEntry header;
  HazelcastBodyEntry body;
  HazelcastResponseEntry response;

  EXPECT_EQ(header.getClassId(), HAZELCAST_HEADER_TYPE_ID);
  EXPECT_EQ(body.getClassId(), HAZELCAST_BODY_TYPE_ID);
  EXPECT_EQ(response.getClassId(), HAZELCAST_RESPONSE_TYPE_ID);

  EXPECT_EQ(header.getFactoryId(), HAZELCAST_ENTRY_SERIALIZER_FACTORY_ID);
  EXPECT_EQ(body.getFactoryId(), HAZELCAST_ENTRY_SERIALIZER_FACTORY_ID);
  EXPECT_EQ(response.getFactoryId(), HAZELCAST_ENTRY_SERIALIZER_FACTORY_ID);
}

} // namespace HazelcastHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
