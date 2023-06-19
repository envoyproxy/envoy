#include "envoy/common/exception.h"
#include "envoy/config/core/v3/base.pb.h"

#include "source/common/config/metadata.h"
#include "source/common/config/well_known_names.h"
#include "source/common/protobuf/utility.h"

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

class TypedMetadataTest : public testing::Test {
public:
  TypedMetadataTest()
      : registered_factory_foo_(foo_factory_), registered_factory_bar_(bar_factory_),
        registered_factory_baz_(baz_factory_) {}

  struct Foo : public TypedMetadata::Object {
    Foo(std::string name) : name_(name) {}
    std::string name_;
  };

  struct Qux : public TypedMetadata::Object {};

  class FoobarFactory : public TypedMetadataFactory {
  public:
    // Throws EnvoyException (conversion failure) if d is empty.
    std::unique_ptr<const TypedMetadata::Object>
    parse(const ProtobufWkt::Struct& d) const override {
      if (d.fields().find("name") != d.fields().end()) {
        return std::make_unique<Foo>(d.fields().at("name").string_value());
      }
      throw EnvoyException("Cannot create a Foo when Struct metadata is empty.");
    }

    std::unique_ptr<const TypedMetadata::Object> parse(const ProtobufWkt::Any& d) const override {
      if (!(d.type_url().empty())) {
        return std::make_unique<Foo>(std::string(d.value()));
      }
      throw EnvoyException("Cannot create a Foo when Any metadata is empty.");
    }
  };

  // Three testing factories with different names
  class FooFactory : public FoobarFactory {
  public:
    std::string name() const override { return "foo"; }
  };

  class BarFactory : public FoobarFactory {
  public:
    std::string name() const override { return "bar"; }
  };

  class BazFactory : public FoobarFactory {
  public:
    std::string name() const override { return "baz"; }
    using FoobarFactory::parse;
    // Override Any parse() to just return nullptr.
    std::unique_ptr<const TypedMetadata::Object> parse(const ProtobufWkt::Any&) const override {
      return nullptr;
    }
  };

protected:
  FooFactory foo_factory_;
  BarFactory bar_factory_;
  BazFactory baz_factory_;
  Registry::InjectFactory<TypedMetadataFactory> registered_factory_foo_;
  Registry::InjectFactory<TypedMetadataFactory> registered_factory_bar_;
  Registry::InjectFactory<TypedMetadataFactory> registered_factory_baz_;
};

// Tests data parsing and retrieving when only Struct field present in the metadata.
TEST_F(TypedMetadataTest, OkTestStruct) {
  envoy::config::core::v3::Metadata metadata;
  (*metadata.mutable_filter_metadata())[foo_factory_.name()] =
      MessageUtil::keyValueStruct("name", "garply");
  TypedMetadataImpl<TypedMetadataFactory> typed(metadata);
  EXPECT_NE(nullptr, typed.get<Foo>(foo_factory_.name()));
  EXPECT_EQ("garply", typed.get<Foo>(foo_factory_.name())->name_);
  // A duck is a duck.
  EXPECT_EQ(nullptr, typed.get<Qux>(foo_factory_.name()));
}

// Tests data parsing and retrieving when only Any field present in the metadata.
TEST_F(TypedMetadataTest, OkTestAny) {
  envoy::config::core::v3::Metadata metadata;
  ProtobufWkt::Any any;
  any.set_type_url("type.googleapis.com/waldo");
  any.set_value("fred");
  (*metadata.mutable_typed_filter_metadata())[bar_factory_.name()] = any;
  TypedMetadataImpl<TypedMetadataFactory> typed(metadata);
  EXPECT_NE(nullptr, typed.get<Foo>(bar_factory_.name()));
  EXPECT_EQ("fred", typed.get<Foo>(bar_factory_.name())->name_);
}

// Tests data parsing and retrieving when only Any field present in the metadata,
// also Any data parsing method just return nullptr.
TEST_F(TypedMetadataTest, OkTestAnyParseReturnNullptr) {
  envoy::config::core::v3::Metadata metadata;
  ProtobufWkt::Any any;
  any.set_type_url("type.googleapis.com/waldo");
  any.set_value("fred");
  (*metadata.mutable_typed_filter_metadata())[baz_factory_.name()] = any;
  TypedMetadataImpl<TypedMetadataFactory> typed(metadata);
  EXPECT_EQ(nullptr, typed.get<Foo>(baz_factory_.name()));
}

// Tests data parsing and retrieving when both Struct and Any field
// present in the metadata, and in the same factory.
TEST_F(TypedMetadataTest, OkTestBothSameFactory) {
  envoy::config::core::v3::Metadata metadata;
  (*metadata.mutable_filter_metadata())[foo_factory_.name()] =
      MessageUtil::keyValueStruct("name", "garply");
  ProtobufWkt::Any any;
  any.set_type_url("type.googleapis.com/waldo");
  any.set_value("fred");
  (*metadata.mutable_typed_filter_metadata())[foo_factory_.name()] = any;
  TypedMetadataImpl<TypedMetadataFactory> typed(metadata);
  EXPECT_NE(nullptr, typed.get<Foo>(foo_factory_.name()));
  // If metadata has both Struct and Any field in same factory,
  // only Any field is populated.
  EXPECT_EQ("fred", typed.get<Foo>(foo_factory_.name())->name_);
}

// Tests data parsing and retrieving when both Struct and Any field
// present in the metadata, but in different factory.
TEST_F(TypedMetadataTest, OkTestBothDifferentFactory) {
  envoy::config::core::v3::Metadata metadata;
  (*metadata.mutable_filter_metadata())[foo_factory_.name()] =
      MessageUtil::keyValueStruct("name", "garply");
  ProtobufWkt::Any any;
  any.set_type_url("type.googleapis.com/waldo");
  any.set_value("fred");
  (*metadata.mutable_typed_filter_metadata())[bar_factory_.name()] = any;

  TypedMetadataImpl<TypedMetadataFactory> typed(metadata);
  // If metadata has both Struct and Any field in different factory,
  // both fields are populated.
  EXPECT_NE(nullptr, typed.get<Foo>(foo_factory_.name()));
  EXPECT_EQ("garply", typed.get<Foo>(foo_factory_.name())->name_);

  EXPECT_NE(nullptr, typed.get<Foo>(bar_factory_.name()));
  EXPECT_EQ("fred", typed.get<Foo>(bar_factory_.name())->name_);
}

// Tests data parsing and retrieving when both Struct and Any field
// present in the metadata, and in the same factory, but with the case
// that Any field parse() method returns nullptr.
TEST_F(TypedMetadataTest, OkTestBothSameFactoryAnyParseReturnNullptr) {
  envoy::config::core::v3::Metadata metadata;
  (*metadata.mutable_filter_metadata())[baz_factory_.name()] =
      MessageUtil::keyValueStruct("name", "garply");
  ProtobufWkt::Any any;
  any.set_type_url("type.googleapis.com/waldo");
  any.set_value("fred");
  (*metadata.mutable_typed_filter_metadata())[baz_factory_.name()] = any;

  // Since Any Parse() method in BazFactory just return nullptr,
  // Struct data is populated.
  TypedMetadataImpl<TypedMetadataFactory> typed(metadata);
  EXPECT_NE(nullptr, typed.get<Foo>(baz_factory_.name()));
  EXPECT_EQ("garply", typed.get<Foo>(baz_factory_.name())->name_);
}

// Tests data parsing and retrieving when no data present in the metadata.
TEST_F(TypedMetadataTest, NoMetadataTest) {
  envoy::config::core::v3::Metadata metadata;
  TypedMetadataImpl<TypedMetadataFactory> typed(metadata);
  // metadata is empty, nothing is populated.
  EXPECT_EQ(nullptr, typed.get<Foo>(foo_factory_.name()));
}

// Tests data parsing and retrieving when Struct metadata updates.
TEST_F(TypedMetadataTest, StructMetadataRefreshTest) {
  envoy::config::core::v3::Metadata metadata;
  (*metadata.mutable_filter_metadata())[foo_factory_.name()] =
      MessageUtil::keyValueStruct("name", "garply");
  TypedMetadataImpl<TypedMetadataFactory> typed(metadata);
  EXPECT_NE(nullptr, typed.get<Foo>(foo_factory_.name()));
  EXPECT_EQ("garply", typed.get<Foo>(foo_factory_.name())->name_);

  // Updated.
  (*metadata.mutable_filter_metadata())[foo_factory_.name()] =
      MessageUtil::keyValueStruct("name", "plugh");
  TypedMetadataImpl<TypedMetadataFactory> typed2(metadata);
  EXPECT_NE(nullptr, typed2.get<Foo>(foo_factory_.name()));
  EXPECT_EQ("plugh", typed2.get<Foo>(foo_factory_.name())->name_);

  // Deleted.
  (*metadata.mutable_filter_metadata()).erase(foo_factory_.name());
  TypedMetadataImpl<TypedMetadataFactory> typed3(metadata);
  EXPECT_EQ(nullptr, typed3.get<Foo>(foo_factory_.name()));
}

// Tests data parsing and retrieving when Any metadata updates.
TEST_F(TypedMetadataTest, AnyMetadataRefreshTest) {
  envoy::config::core::v3::Metadata metadata;
  ProtobufWkt::Any any;
  any.set_type_url("type.googleapis.com/waldo");
  any.set_value("fred");
  (*metadata.mutable_typed_filter_metadata())[bar_factory_.name()] = any;
  TypedMetadataImpl<TypedMetadataFactory> typed(metadata);
  EXPECT_NE(nullptr, typed.get<Foo>(bar_factory_.name()));
  EXPECT_EQ("fred", typed.get<Foo>(bar_factory_.name())->name_);

  // Updated.
  any.set_type_url("type.googleapis.com/xyzzy");
  any.set_value("thud");
  (*metadata.mutable_typed_filter_metadata())[bar_factory_.name()] = any;
  TypedMetadataImpl<TypedMetadataFactory> typed2(metadata);
  EXPECT_NE(nullptr, typed2.get<Foo>(bar_factory_.name()));
  EXPECT_EQ("thud", typed2.get<Foo>(bar_factory_.name())->name_);

  // Deleted.
  (*metadata.mutable_typed_filter_metadata()).erase(bar_factory_.name());
  TypedMetadataImpl<TypedMetadataFactory> typed3(metadata);
  EXPECT_EQ(nullptr, typed3.get<Foo>(bar_factory_.name()));
}

// Tests empty Struct metadata parsing case.
TEST_F(TypedMetadataTest, InvalidStructMetadataTest) {
  envoy::config::core::v3::Metadata metadata;
  (*metadata.mutable_filter_metadata())[foo_factory_.name()] = ProtobufWkt::Struct();
  EXPECT_THROW_WITH_MESSAGE(TypedMetadataImpl<TypedMetadataFactory> typed(metadata),
                            Envoy::EnvoyException,
                            "Cannot create a Foo when Struct metadata is empty.");
}

// Tests empty Any metadata parsing case.
TEST_F(TypedMetadataTest, InvalidAnyMetadataTest) {
  envoy::config::core::v3::Metadata metadata;
  (*metadata.mutable_typed_filter_metadata())[bar_factory_.name()] = ProtobufWkt::Any();
  EXPECT_THROW_WITH_MESSAGE(TypedMetadataImpl<TypedMetadataFactory> typed(metadata),
                            Envoy::EnvoyException,
                            "Cannot create a Foo when Any metadata is empty.");
}

} // namespace
} // namespace Config
} // namespace Envoy
