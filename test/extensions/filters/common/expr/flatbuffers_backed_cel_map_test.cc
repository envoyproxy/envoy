#include <string>

#include "source/extensions/filters/common/expr/flatbuffers_backed_cel_map.h"

#include "test/test_common/environment.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "flatbuffers/idl.h"
#include "flatbuffers/reflection.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {

namespace {

using google::api::expr::runtime::CelList;
using google::api::expr::runtime::CelMap;
using google::api::expr::runtime::CelValue;

constexpr char kReflectionBufferPath[] =
    "test/extensions/filters/common/expr/test_data/flatbuffers.bfbs";

constexpr absl::string_view kByteField = "f_byte";
constexpr absl::string_view kUbyteField = "f_ubyte";
constexpr absl::string_view kShortField = "f_short";
constexpr absl::string_view kUshortField = "f_ushort";
constexpr absl::string_view kIntField = "f_int";
constexpr absl::string_view kUintField = "f_uint";
constexpr absl::string_view kLongField = "f_long";
constexpr absl::string_view kUlongField = "f_ulong";
constexpr absl::string_view kFloatField = "f_float";
constexpr absl::string_view kDoubleField = "f_double";
constexpr absl::string_view kBoolField = "f_bool";
constexpr absl::string_view kStringField = "f_string";
constexpr absl::string_view kObjField = "f_obj";

constexpr absl::string_view kUnknownField = "f_unknown";

constexpr absl::string_view kBytesField = "r_byte";
constexpr absl::string_view kUbytesField = "r_ubyte";
constexpr absl::string_view kShortsField = "r_short";
constexpr absl::string_view kUshortsField = "r_ushort";
constexpr absl::string_view kIntsField = "r_int";
constexpr absl::string_view kUintsField = "r_uint";
constexpr absl::string_view kLongsField = "r_long";
constexpr absl::string_view kUlongsField = "r_ulong";
constexpr absl::string_view kFloatsField = "r_float";
constexpr absl::string_view kDoublesField = "r_double";
constexpr absl::string_view kBoolsField = "r_bool";
constexpr absl::string_view kStringsField = "r_string";
constexpr absl::string_view kObjsField = "r_obj";
constexpr absl::string_view kIndexedField = "r_indexed";

const int64_t kNumFields = 27;

class FlatBuffersBackedCelMapTest : public testing::Test {
public:
  FlatBuffersBackedCelMapTest() {
    EXPECT_TRUE(flatbuffers::LoadFile(TestEnvironment::runfilesPath(kReflectionBufferPath).c_str(),
                                      true, &schema_file_));
    flatbuffers::Verifier verifier(reinterpret_cast<const uint8_t*>(schema_file_.data()),
                                   schema_file_.size());
    EXPECT_TRUE(reflection::VerifySchemaBuffer(verifier));
    EXPECT_TRUE(parser_.Deserialize(reinterpret_cast<const uint8_t*>(schema_file_.data()),
                                    schema_file_.size()));
    schema_ = reflection::GetSchema(schema_file_.data());
  }
  const CelMap& loadJson(std::string data) {
    EXPECT_TRUE(parser_.Parse(data.data()));
    const CelMap* value =
        createFlatBuffersBackedCelMap(parser_.builder_.GetBufferPointer(), *schema_, &arena_);
    EXPECT_NE(nullptr, value);
    EXPECT_EQ(kNumFields, value->size());
    const CelList* keys = value->ListKeys().value();
    EXPECT_NE(nullptr, keys);
    EXPECT_EQ(kNumFields, keys->size());
    EXPECT_TRUE((*keys)[2].IsString());
    return *value;
  }

protected:
  std::string schema_file_;
  flatbuffers::Parser parser_;
  const reflection::Schema* schema_;
  Protobuf::Arena arena_;
};

TEST_F(FlatBuffersBackedCelMapTest, PrimitiveFields) {
  const CelMap& value = loadJson(R"({
              f_byte: -1,
              f_ubyte: 1,
              f_short: -2,
              f_ushort: 2,
              f_int: -3,
              f_uint: 3,
              f_long: -4,
              f_ulong: 4,
              f_float: 5.0,
              f_double: 6.0,
              f_bool: false,
              f_string: "test"
              })");
  // byte
  {
    auto f = value[CelValue::CreateStringView(kByteField)];
    EXPECT_TRUE(f.has_value());
    EXPECT_TRUE(f->IsInt64());
    EXPECT_EQ(-1, f->Int64OrDie());
  }
  {
    auto uf = value[CelValue::CreateStringView(kUbyteField)];
    EXPECT_TRUE(uf.has_value());
    EXPECT_TRUE(uf->IsUint64());
    EXPECT_EQ(1, uf->Uint64OrDie());
  }
  // short
  {
    auto f = value[CelValue::CreateStringView(kShortField)];
    EXPECT_TRUE(f.has_value());
    EXPECT_TRUE(f->IsInt64());
    EXPECT_EQ(-2, f->Int64OrDie());
  }
  {
    auto uf = value[CelValue::CreateStringView(kUshortField)];
    EXPECT_TRUE(uf.has_value());
    EXPECT_TRUE(uf->IsUint64());
    EXPECT_EQ(2, uf->Uint64OrDie());
  }
  // int
  {
    auto f = value[CelValue::CreateStringView(kIntField)];
    EXPECT_TRUE(f.has_value());
    EXPECT_TRUE(f->IsInt64());
    EXPECT_EQ(-3, f->Int64OrDie());
  }
  {
    auto uf = value[CelValue::CreateStringView(kUintField)];
    EXPECT_TRUE(uf.has_value());
    EXPECT_TRUE(uf->IsUint64());
    EXPECT_EQ(3, uf->Uint64OrDie());
  }
  // long
  {
    auto f = value[CelValue::CreateStringView(kLongField)];
    EXPECT_TRUE(f.has_value());
    EXPECT_TRUE(f->IsInt64());
    EXPECT_EQ(-4, f->Int64OrDie());
  }
  {
    auto uf = value[CelValue::CreateStringView(kUlongField)];
    EXPECT_TRUE(uf.has_value());
    EXPECT_TRUE(uf->IsUint64());
    EXPECT_EQ(4, uf->Uint64OrDie());
  }
  // float and double
  {
    auto f = value[CelValue::CreateStringView(kFloatField)];
    EXPECT_TRUE(f.has_value());
    EXPECT_TRUE(f->IsDouble());
    EXPECT_EQ(5.0, f->DoubleOrDie());
  }
  {
    auto f = value[CelValue::CreateStringView(kDoubleField)];
    EXPECT_TRUE(f.has_value());
    EXPECT_TRUE(f->IsDouble());
    EXPECT_EQ(6.0, f->DoubleOrDie());
  }
  // bool
  {
    auto f = value[CelValue::CreateStringView(kBoolField)];
    EXPECT_TRUE(f.has_value());
    EXPECT_TRUE(f->IsBool());
    EXPECT_EQ(false, f->BoolOrDie());
  }
  // string
  {
    auto f = value[CelValue::CreateStringView(kStringField)];
    EXPECT_TRUE(f.has_value());
    EXPECT_TRUE(f->IsString());
    EXPECT_EQ("test", f->StringOrDie().value());
  }
  // bad field type
  {
    CelValue bad_field = CelValue::CreateInt64(1);
    auto f = value[bad_field];
    EXPECT_TRUE(f.has_value());
    EXPECT_TRUE(f->IsError());
    auto presence = value.Has(bad_field);
    EXPECT_FALSE(presence.ok());
    EXPECT_EQ(presence.status().code(), absl::StatusCode::kInvalidArgument);
  }
  // missing field
  {
    auto f = value[CelValue::CreateStringView(kUnknownField)];
    EXPECT_FALSE(f.has_value());
  }
}

TEST_F(FlatBuffersBackedCelMapTest, PrimitiveFieldDefaults) {
  const CelMap& value = loadJson("{}");
  // byte
  {
    auto f = value[CelValue::CreateStringView(kByteField)];
    EXPECT_TRUE(f.has_value());
    EXPECT_TRUE(f->IsInt64());
    EXPECT_EQ(0, f->Int64OrDie());
  }
  // short
  {
    auto f = value[CelValue::CreateStringView(kShortField)];
    EXPECT_TRUE(f.has_value());
    EXPECT_TRUE(f->IsInt64());
    EXPECT_EQ(150, f->Int64OrDie());
  }
  // bool
  {
    auto f = value[CelValue::CreateStringView(kBoolField)];
    EXPECT_TRUE(f.has_value());
    EXPECT_TRUE(f->IsBool());
    EXPECT_EQ(true, f->BoolOrDie());
  }
  // string
  {
    auto f = value[CelValue::CreateStringView(kStringField)];
    EXPECT_TRUE(f.has_value());
    EXPECT_TRUE(f->IsString());
    EXPECT_EQ("", f->StringOrDie().value());
  }
}

TEST_F(FlatBuffersBackedCelMapTest, ObjectField) {
  const CelMap& value = loadJson(R"({
                                    f_obj: {
                                      f_string: "entry",
                                      f_int: 16
                                    }
                                    })");
  CelValue field = CelValue::CreateStringView(kObjField);
  auto presence = value.Has(field);
  EXPECT_OK(presence);
  EXPECT_TRUE(*presence);
  auto f = value[field];
  EXPECT_TRUE(f.has_value());
  EXPECT_TRUE(f->IsMap());
  const CelMap& m = *f->MapOrDie();
  EXPECT_EQ(2, m.size());
  {
    auto obj_field = CelValue::CreateStringView(kStringField);
    auto member_presence = m.Has(obj_field);
    EXPECT_OK(member_presence);
    EXPECT_TRUE(*member_presence);
    auto mf = m[obj_field];
    EXPECT_TRUE(mf.has_value());
    EXPECT_TRUE(mf->IsString());
    EXPECT_EQ("entry", mf->StringOrDie().value());
  }
  {
    auto obj_field = CelValue::CreateStringView(kIntField);
    auto member_presence = m.Has(obj_field);
    EXPECT_OK(member_presence);
    EXPECT_TRUE(*member_presence);
    auto mf = m[obj_field];
    EXPECT_TRUE(mf.has_value());
    EXPECT_TRUE(mf->IsInt64());
    EXPECT_EQ(16, mf->Int64OrDie());
  }
  {
    std::string undefined = "f_undefined";
    CelValue undefined_field = CelValue::CreateStringView(undefined);
    auto presence = m.Has(undefined_field);
    EXPECT_OK(presence);
    EXPECT_FALSE(*presence);
    auto v = m[undefined_field];
    EXPECT_FALSE(v.has_value());

    presence = m.Has(CelValue::CreateBool(false));
    EXPECT_FALSE(presence.ok());
    EXPECT_EQ(presence.status().code(), absl::StatusCode::kInvalidArgument);
  }
}

TEST_F(FlatBuffersBackedCelMapTest, ObjectFieldDefault) {
  const CelMap& value = loadJson("{}");
  auto f = value[CelValue::CreateStringView(kObjField)];
  EXPECT_TRUE(f.has_value());
  EXPECT_TRUE(f->IsNull());
}

TEST_F(FlatBuffersBackedCelMapTest, PrimitiveVectorFields) {
  const CelMap& value = loadJson(R"({
              r_byte: [-97],
              r_ubyte: [97, 98, 99],
              r_short: [-2],
              r_ushort: [2],
              r_int: [-3],
              r_uint: [3],
              r_long: [-4],
              r_ulong: [4],
              r_float: [5.0],
              r_double: [6.0],
              r_bool: [false],
              r_string: ["test"]
              })");
  // byte
  {
    auto f = value[CelValue::CreateStringView(kBytesField)];
    EXPECT_TRUE(f.has_value());
    EXPECT_TRUE(f->IsBytes());
    EXPECT_EQ("\x9F", f->BytesOrDie().value());
  }
  {
    auto uf = value[CelValue::CreateStringView(kUbytesField)];
    EXPECT_TRUE(uf.has_value());
    EXPECT_TRUE(uf->IsBytes());
    EXPECT_EQ("abc", uf->BytesOrDie().value());
  }
  // short
  {
    auto f = value[CelValue::CreateStringView(kShortsField)];
    EXPECT_TRUE(f.has_value());
    EXPECT_TRUE(f->IsList());
    const CelList& l = *f->ListOrDie();
    EXPECT_EQ(1, l.size());
    EXPECT_EQ(-2, l[0].Int64OrDie());
  }
  {
    auto uf = value[CelValue::CreateStringView(kUshortsField)];
    EXPECT_TRUE(uf.has_value());
    EXPECT_TRUE(uf->IsList());
    const CelList& l = *uf->ListOrDie();
    EXPECT_EQ(1, l.size());
    EXPECT_EQ(2, l[0].Uint64OrDie());
  }
  // int
  {
    auto f = value[CelValue::CreateStringView(kIntsField)];
    EXPECT_TRUE(f.has_value());
    EXPECT_TRUE(f->IsList());
    const CelList& l = *f->ListOrDie();
    EXPECT_EQ(1, l.size());
    EXPECT_EQ(-3, l[0].Int64OrDie());
  }
  {
    auto uf = value[CelValue::CreateStringView(kUintsField)];
    EXPECT_TRUE(uf.has_value());
    EXPECT_TRUE(uf->IsList());
    const CelList& l = *uf->ListOrDie();
    EXPECT_EQ(1, l.size());
    EXPECT_EQ(3, l[0].Uint64OrDie());
  }
  // long
  {
    auto f = value[CelValue::CreateStringView(kLongsField)];
    EXPECT_TRUE(f.has_value());
    EXPECT_TRUE(f->IsList());
    const CelList& l = *f->ListOrDie();
    EXPECT_EQ(1, l.size());
    EXPECT_EQ(-4, l[0].Int64OrDie());
  }
  {
    auto uf = value[CelValue::CreateStringView(kUlongsField)];
    EXPECT_TRUE(uf.has_value());
    EXPECT_TRUE(uf->IsList());
    const CelList& l = *uf->ListOrDie();
    EXPECT_EQ(1, l.size());
    EXPECT_EQ(4, l[0].Uint64OrDie());
  }
  // float and double
  {
    auto f = value[CelValue::CreateStringView(kFloatsField)];
    EXPECT_TRUE(f.has_value());
    EXPECT_TRUE(f->IsList());
    const CelList& l = *f->ListOrDie();
    EXPECT_EQ(1, l.size());
    EXPECT_EQ(5.0, l[0].DoubleOrDie());
  }
  {
    auto f = value[CelValue::CreateStringView(kDoublesField)];
    EXPECT_TRUE(f.has_value());
    EXPECT_TRUE(f->IsList());
    const CelList& l = *f->ListOrDie();
    EXPECT_EQ(1, l.size());
    EXPECT_EQ(6.0, l[0].DoubleOrDie());
  }
  // bool
  {
    auto f = value[CelValue::CreateStringView(kBoolsField)];
    EXPECT_TRUE(f.has_value());
    EXPECT_TRUE(f->IsList());
    const CelList& l = *f->ListOrDie();
    EXPECT_EQ(1, l.size());
    EXPECT_EQ(false, l[0].BoolOrDie());
  }
  // string
  {
    auto f = value[CelValue::CreateStringView(kStringsField)];
    EXPECT_TRUE(f.has_value());
    EXPECT_TRUE(f->IsList());
    const CelList& l = *f->ListOrDie();
    EXPECT_EQ(1, l.size());
    EXPECT_EQ("test", l[0].StringOrDie().value());
  }
}

TEST_F(FlatBuffersBackedCelMapTest, ObjectVectorField) {
  const CelMap& value = loadJson(R"({
                                    r_obj: [{
                                      f_string: "entry",
                                      f_int: 16
                                    },{
                                      f_int: 32
                                    }]
                                    })");
  auto f = value[CelValue::CreateStringView(kObjsField)];
  EXPECT_TRUE(f.has_value());
  EXPECT_TRUE(f->IsList());
  const CelList& l = *f->ListOrDie();
  EXPECT_EQ(2, l.size());
  {
    EXPECT_TRUE(l[0].IsMap());
    const CelMap& m = *l[0].MapOrDie();
    EXPECT_EQ(2, m.size());
    {
      CelValue field = CelValue::CreateStringView(kStringField);
      auto presence = m.Has(field);
      EXPECT_OK(presence);
      EXPECT_TRUE(*presence);
      auto mf = m[field];
      EXPECT_TRUE(mf.has_value());
      EXPECT_TRUE(mf->IsString());
      EXPECT_EQ("entry", mf->StringOrDie().value());
    }
    {
      CelValue field = CelValue::CreateStringView(kIntField);
      auto presence = m.Has(field);
      EXPECT_OK(presence);
      EXPECT_TRUE(*presence);
      auto mf = m[field];
      EXPECT_TRUE(mf.has_value());
      EXPECT_TRUE(mf->IsInt64());
      EXPECT_EQ(16, mf->Int64OrDie());
    }
  }
  {
    EXPECT_TRUE(l[1].IsMap());
    const CelMap& m = *l[1].MapOrDie();
    EXPECT_EQ(2, m.size());
    {
      CelValue field = CelValue::CreateStringView(kStringField);
      auto presence = m.Has(field);
      EXPECT_OK(presence);
      // Note, the presence checks on flat buffers seem to only apply to whether
      // the field is defined.
      EXPECT_TRUE(*presence);
      auto mf = m[field];
      EXPECT_TRUE(mf.has_value());
      EXPECT_TRUE(mf->IsString());
      EXPECT_EQ("", mf->StringOrDie().value());
    }
    {
      CelValue field = CelValue::CreateStringView(kIntField);
      auto presence = m.Has(field);
      EXPECT_OK(presence);
      EXPECT_TRUE(*presence);
      auto mf = m[field];
      EXPECT_TRUE(mf.has_value());
      EXPECT_TRUE(mf->IsInt64());
      EXPECT_EQ(32, mf->Int64OrDie());
    }
    {
      std::string undefined = "f_undefined";
      CelValue field = CelValue::CreateStringView(undefined);
      auto presence = m.Has(field);
      EXPECT_OK(presence);
      EXPECT_FALSE(*presence);
      auto mf = m[field];
      EXPECT_FALSE(mf.has_value());
    }
  }
}

TEST_F(FlatBuffersBackedCelMapTest, VectorFieldDefaults) {
  const CelMap& value = loadJson("{}");
  for (const auto field :
       std::vector<absl::string_view>{kIntsField, kBoolsField, kStringsField, kObjsField}) {
    auto f = value[CelValue::CreateStringView(field)];
    EXPECT_TRUE(f.has_value());
    EXPECT_TRUE(f->IsList());
    const CelList& l = *f->ListOrDie();
    EXPECT_EQ(0, l.size());
  }

  {
    auto f = value[CelValue::CreateStringView(kIndexedField)];
    EXPECT_TRUE(f.has_value());
    EXPECT_TRUE(f->IsMap());
    const CelMap& m = *f->MapOrDie();
    EXPECT_EQ(0, m.size());
    EXPECT_EQ(0, (*m.ListKeys())->size());
  }

  {
    auto f = value[CelValue::CreateStringView(kBytesField)];
    EXPECT_TRUE(f.has_value());
    EXPECT_TRUE(f->IsBytes());
    EXPECT_EQ("", f->BytesOrDie().value());
  }
}

TEST_F(FlatBuffersBackedCelMapTest, IndexedObjectVectorField) {
  const CelMap& value = loadJson(R"({
                                    r_indexed: [
                                    {
                                      f_string: "a",
                                      f_int: 16
                                    },
                                    {
                                      f_string: "b",
                                      f_int: 32
                                    },
                                    {
                                      f_string: "c",
                                      f_int: 64
                                    },
                                    {
                                      f_string: "d",
                                      f_int: 128
                                    }
                                    ]
                                    })");
  auto f = value[CelValue::CreateStringView(kIndexedField)];
  EXPECT_TRUE(f.has_value());
  EXPECT_TRUE(f->IsMap());
  const CelMap& m = *f->MapOrDie();
  EXPECT_EQ(4, m.size());
  const CelList& l = *m.ListKeys().value();
  EXPECT_EQ(4, l.size());
  EXPECT_TRUE(l[0].IsString());
  EXPECT_TRUE(l[1].IsString());
  EXPECT_TRUE(l[2].IsString());
  EXPECT_TRUE(l[3].IsString());
  std::string a = "a";
  std::string b = "b";
  std::string c = "c";
  std::string d = "d";
  EXPECT_EQ(a, l[0].StringOrDie().value());
  EXPECT_EQ(b, l[1].StringOrDie().value());
  EXPECT_EQ(c, l[2].StringOrDie().value());
  EXPECT_EQ(d, l[3].StringOrDie().value());

  for (const std::string& key : std::vector<std::string>{a, b, c, d}) {
    auto v = m[CelValue::CreateString(&key)];
    EXPECT_TRUE(v.has_value());
    const CelMap& vm = *v->MapOrDie();
    EXPECT_EQ(2, vm.size());
    auto vf = vm[CelValue::CreateStringView(kStringField)];
    EXPECT_TRUE(vf.has_value());
    EXPECT_TRUE(vf->IsString());
    EXPECT_EQ(key, vf->StringOrDie().value());
    auto vi = vm[CelValue::CreateStringView(kIntField)];
    EXPECT_TRUE(vi.has_value());
    EXPECT_TRUE(vi->IsInt64());
  }

  {
    std::string bb = "bb";
    std::string dd = "dd";
    EXPECT_FALSE(m[CelValue::CreateString(&bb)].has_value());
    EXPECT_FALSE(m[CelValue::CreateString(&dd)].has_value());
    EXPECT_FALSE(m[CelValue::CreateStringView(absl::string_view())].has_value());
  }
}

TEST_F(FlatBuffersBackedCelMapTest, IndexedObjectVectorFieldDefaults) {
  const CelMap& value = loadJson(R"({
                                    r_indexed: [
                                    {
                                      f_string: "",
                                      f_int: 16
                                    }
                                    ]
                                    })");
  CelValue field = CelValue::CreateStringView(kIndexedField);
  auto presence = value.Has(field);
  EXPECT_OK(presence);
  EXPECT_TRUE(*presence);
  auto f = value[field];
  EXPECT_TRUE(f.has_value());
  EXPECT_TRUE(f->IsMap());
  const CelMap& m = *f->MapOrDie();

  EXPECT_EQ(1, m.size());
  const CelList& l = *m.ListKeys().value();
  EXPECT_EQ(1, l.size());
  EXPECT_TRUE(l[0].IsString());
  EXPECT_EQ("", l[0].StringOrDie().value());

  CelValue map_field = CelValue::CreateStringView(absl::string_view());
  presence = m.Has(map_field);
  EXPECT_OK(presence);
  EXPECT_TRUE(*presence);
  auto v = m[map_field];
  EXPECT_TRUE(v.has_value());

  std::string undefined = "f_undefined";
  CelValue undefined_field = CelValue::CreateStringView(undefined);
  presence = m.Has(undefined_field);
  EXPECT_OK(presence);
  EXPECT_FALSE(*presence);
  v = m[undefined_field];
  EXPECT_FALSE(v.has_value());

  presence = m.Has(CelValue::CreateBool(false));
  EXPECT_FALSE(presence.ok());
  EXPECT_EQ(presence.status().code(), absl::StatusCode::kInvalidArgument);
}

} // namespace

} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
