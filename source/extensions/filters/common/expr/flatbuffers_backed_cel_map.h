#pragma once

#include <optional>

#include "source/common/protobuf/protobuf.h"

#include "eval/public/cel_value.h"
#include "flatbuffers/reflection.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {

class FlatBuffersBackedCelMap : public google::api::expr::runtime::CelMap {
public:
  FlatBuffersBackedCelMap(const flatbuffers::Table& table, const reflection::Schema& schema,
                          const reflection::Object& object, Protobuf::Arena* arena)
      : arena_(arena), table_(table), schema_(schema) {
    keys_.fields = object.fields();
  }

  int size() const override { return keys_.fields->size(); }

  absl::StatusOr<bool> Has(const google::api::expr::runtime::CelValue& key) const override;

  std::optional<google::api::expr::runtime::CelValue>
  operator[](google::api::expr::runtime::CelValue cel_key) const override;

  // Import base class signatures to bypass GCC warning/error.
  using google::api::expr::runtime::CelMap::ListKeys;
  absl::StatusOr<const google::api::expr::runtime::CelList*> ListKeys() const override {
    return &keys_;
  }

private:
  struct FieldList : public google::api::expr::runtime::CelList {
    int size() const override { return fields->size(); }
    google::api::expr::runtime::CelValue operator[](int index) const override {
      auto name = fields->Get(index)->name();
      return google::api::expr::runtime::CelValue::CreateStringView(
          absl::string_view(name->c_str(), name->size()));
    }
    const flatbuffers::Vector<flatbuffers::Offset<reflection::Field>>* fields;
  };
  FieldList keys_;
  Protobuf::Arena* arena_;
  const flatbuffers::Table& table_;
  const reflection::Schema& schema_;
};

// Factory method to instantiate a CelMap on the arena for flatbuffer object
// from a reflection schema.
const google::api::expr::runtime::CelMap*
createFlatBuffersBackedCelMap(const uint8_t* flatbuf, const reflection::Schema& schema,
                              Protobuf::Arena* arena);

} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
