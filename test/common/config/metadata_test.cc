#include "envoy/common/exception.h"
#include "envoy/config/core/v3/base.pb.h"

#include "common/config/metadata.h"
#include "common/config/well_known_names.h"
#include "common/protobuf/utility.h"

#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Config {
namespace {

TEST(MetadataTest, MetadataValue) {
  envoy::config::core::v3::Metadata metadata;
  Metadata::mutableMetadataValue(metadata, MetadataFilters::get().ENVOY_LB,
                                 MetadataEnvoyLbKeys::get().CANARY)
      .set_bool_value(true);
  EXPECT_TRUE(Metadata::metadataValue(&metadata, MetadataFilters::get().ENVOY_LB,
                                      MetadataEnvoyLbKeys::get().CANARY)
                  .bool_value());
  EXPECT_FALSE(Metadata::metadataValue(&metadata, "foo", "bar").bool_value());
  EXPECT_FALSE(
      Metadata::metadataValue(&metadata, MetadataFilters::get().ENVOY_LB, "bar").bool_value());
}

TEST(MetadataTest, MetadataValuePath) {
  const std::string filter = "com.test";
  envoy::config::core::v3::Metadata metadata;
  std::vector<std::string> path{"test_obj", "inner_key"};
  // not found case
  EXPECT_EQ(Metadata::metadataValue(&metadata, filter, path).kind_case(),
            ProtobufWkt::Value::KindCase::KIND_NOT_SET);
  ProtobufWkt::Struct& filter_struct = (*metadata.mutable_filter_metadata())[filter];
  auto obj = MessageUtil::keyValueStruct("inner_key", "inner_value");
  ProtobufWkt::Value val;
  *val.mutable_struct_value() = obj;
  (*filter_struct.mutable_fields())["test_obj"] = val;
  EXPECT_EQ(Metadata::metadataValue(&metadata, filter, path).string_value(), "inner_value");
  // not found with longer path
  path.push_back("bad_key");
  EXPECT_EQ(Metadata::metadataValue(&metadata, filter, path).kind_case(),
            ProtobufWkt::Value::KindCase::KIND_NOT_SET);
  // empty path returns not found
  EXPECT_EQ(Metadata::metadataValue(&metadata, filter, std::vector<std::string>{}).kind_case(),
            ProtobufWkt::Value::KindCase::KIND_NOT_SET);
}

// Test case for typed metadata: original Any data in metadata field
TEST(MetadataTest, OriginalTypedMetadataValue) {
  // create an any data and set its type_url and value
  ProtobufWkt::Any any;
  any.set_type_url("type.googleapis.com/foo");
  any.set_value("bar");

  // Put the data into metadata typed_filter_metadata field with the key : baz.
  envoy::config::core::v3::Metadata metadata;
  (*(metadata.mutable_typed_filter_metadata()))["baz"] = any;

  // Verify the data is setup correctly in the metadata
  EXPECT_EQ(metadata.typed_filter_metadata().find("test"), metadata.typed_filter_metadata().end());
  EXPECT_NE(metadata.typed_filter_metadata().find("baz"), metadata.typed_filter_metadata().end());
  EXPECT_EQ((*(metadata.mutable_typed_filter_metadata()))["baz"].type_url(),
            "type.googleapis.com/foo");
  EXPECT_EQ((*(metadata.mutable_typed_filter_metadata()))["baz"].value(), "bar");
}

// Test case for typed metadata: Pack the Any data in metadata field
TEST(MetadataTest, PackedTypedMetadataValue) {
  // create an any_source data and set its type_url and value
  ProtobufWkt::Any any_source;
  any_source.set_type_url("type.googleapis.com/foo");
  any_source.set_value("bar");

  // Pack this any_source data into metadata typed_filter_metadata
  // field with the key : baz.
  envoy::config::core::v3::Metadata metadata;
  (*(metadata.mutable_typed_filter_metadata()))["baz"].PackFrom(any_source);

  // Verify the data is setup correctly in the metadata
  EXPECT_EQ(metadata.typed_filter_metadata().find("test"), metadata.typed_filter_metadata().end());
  EXPECT_NE(metadata.typed_filter_metadata().find("baz"), metadata.typed_filter_metadata().end());
  EXPECT_EQ((*(metadata.mutable_typed_filter_metadata()))["baz"].ShortDebugString(),
            "[type.googleapis.com/google.protobuf.Any] { type_url: \"type.googleapis.com/foo\" "
            "value: \"bar\" }");
  EXPECT_EQ((*(metadata.mutable_typed_filter_metadata()))["baz"].type_url(),
            "type.googleapis.com/google.protobuf.Any");
  EXPECT_EQ((*(metadata.mutable_typed_filter_metadata()))["baz"].value(),
            "\n\x17type.googleapis.com/foo\x12\x3"
            "bar");

  // Unpack the metadata type_filter_metadata field into any_destination
  // and verity the type_url and value are expected.
  ProtobufWkt::Any any_destination;
  (*(metadata.mutable_typed_filter_metadata()))["baz"].UnpackTo(&any_destination);
  EXPECT_EQ(any_destination.type_url(), "type.googleapis.com/foo");
  EXPECT_EQ(any_destination.value(), "bar");
}

class TypedMetadataTest : public testing::Test {
public:
  TypedMetadataTest() : registered_factory_(foo_factory_) {}

  struct Foo : public TypedMetadata::Object {
    Foo(std::string name) : name_(name) {}
    std::string name_;
  };

  struct Bar : public TypedMetadata::Object {};

  class FooFactory : public TypedMetadataFactory {
  public:
    std::string name() const override { return "foo"; }
    // Throws EnvoyException (conversion failure) if d is empty.
    std::unique_ptr<const TypedMetadata::Object>
    parse(const ProtobufWkt::Struct& d) const override {
      if (d.fields().find("name") != d.fields().end()) {
        return std::make_unique<Foo>(d.fields().at("name").string_value());
      }
      throw EnvoyException("Cannot create a Foo when metadata is empty.");
    }
  };

protected:
  FooFactory foo_factory_;
  Registry::InjectFactory<TypedMetadataFactory> registered_factory_;
};

TEST_F(TypedMetadataTest, OkTest) {
  envoy::config::core::v3::Metadata metadata;
  (*metadata.mutable_filter_metadata())[foo_factory_.name()] =
      MessageUtil::keyValueStruct("name", "foo");
  TypedMetadataImpl<TypedMetadataFactory> typed(metadata);
  EXPECT_NE(nullptr, typed.get<Foo>(foo_factory_.name()));
  EXPECT_EQ("foo", typed.get<Foo>(foo_factory_.name())->name_);
  // A duck is a duck.
  EXPECT_EQ(nullptr, typed.get<Bar>(foo_factory_.name()));
}

TEST_F(TypedMetadataTest, NoMetadataTest) {
  envoy::config::core::v3::Metadata metadata;
  TypedMetadataImpl<TypedMetadataFactory> typed(metadata);
  EXPECT_EQ(nullptr, typed.get<Foo>(foo_factory_.name()));
}

TEST_F(TypedMetadataTest, MetadataRefreshTest) {
  envoy::config::core::v3::Metadata metadata;
  (*metadata.mutable_filter_metadata())[foo_factory_.name()] =
      MessageUtil::keyValueStruct("name", "foo");
  TypedMetadataImpl<TypedMetadataFactory> typed(metadata);
  EXPECT_NE(nullptr, typed.get<Foo>(foo_factory_.name()));
  EXPECT_EQ("foo", typed.get<Foo>(foo_factory_.name())->name_);

  // Updated.
  (*metadata.mutable_filter_metadata())[foo_factory_.name()] =
      MessageUtil::keyValueStruct("name", "bar");
  TypedMetadataImpl<TypedMetadataFactory> typed2(metadata);
  EXPECT_NE(nullptr, typed2.get<Foo>(foo_factory_.name()));
  EXPECT_EQ("bar", typed2.get<Foo>(foo_factory_.name())->name_);

  // Deleted.
  (*metadata.mutable_filter_metadata()).erase(foo_factory_.name());
  TypedMetadataImpl<TypedMetadataFactory> typed3(metadata);
  EXPECT_EQ(nullptr, typed3.get<Foo>(foo_factory_.name()));
}

TEST_F(TypedMetadataTest, InvalidMetadataTest) {
  envoy::config::core::v3::Metadata metadata;
  (*metadata.mutable_filter_metadata())[foo_factory_.name()] = ProtobufWkt::Struct();
  EXPECT_THROW_WITH_MESSAGE(TypedMetadataImpl<TypedMetadataFactory> typed(metadata),
                            Envoy::EnvoyException, "Cannot create a Foo when metadata is empty.");
}

} // namespace
} // namespace Config
} // namespace Envoy
