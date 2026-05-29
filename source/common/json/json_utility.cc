#include "source/common/json/json_utility.h"

namespace Envoy {
namespace Json {

namespace {

void structValueToJson(const Protobuf::Struct& struct_value, StringStreamer::Map& level);
void listValueToJson(const Protobuf::ListValue& list_value, StringStreamer::Array& level);

void valueToJson(const Protobuf::Value& value, StringStreamer::Level& level) {
  switch (value.kind_case()) {
  case Protobuf::Value::KIND_NOT_SET:
  case Protobuf::Value::kNullValue:
    level.addNull();
    break;
  case Protobuf::Value::kNumberValue:
    level.addNumber(value.number_value());
    break;
  case Protobuf::Value::kStringValue:
    level.addString(value.string_value());
    break;
  case Protobuf::Value::kBoolValue:
    level.addBool(value.bool_value());
    break;
  case Protobuf::Value::kStructValue:
    structValueToJson(value.struct_value(), *level.addMap());
    break;
  case Protobuf::Value::kListValue:
    listValueToJson(value.list_value(), *level.addArray());
    break;
  }
}

void structValueToJson(const Protobuf::Struct& struct_value, StringStreamer::Map& map) {
  using PairRefWrapper =
      std::reference_wrapper<const Protobuf::Map<std::string, Protobuf::Value>::value_type>;
  absl::InlinedVector<PairRefWrapper, 8> sorted_fields;
  sorted_fields.reserve(struct_value.fields_size());

  for (const auto& field : struct_value.fields()) {
    sorted_fields.emplace_back(field);
  }
  // Sort the keys to make the output deterministic.
  std::sort(sorted_fields.begin(), sorted_fields.end(),
            [](PairRefWrapper a, PairRefWrapper b) { return a.get().first < b.get().first; });

  for (const PairRefWrapper field : sorted_fields) {
    map.addKey(field.get().first);
    valueToJson(field.get().second, map);
  }
}

void listValueToJson(const Protobuf::ListValue& list_value, StringStreamer::Array& arr) {
  for (const Protobuf::Value& value : list_value.values()) {
    valueToJson(value, arr);
  }
}

} // namespace

void Utility::appendValueToString(const Protobuf::Value& value, std::string& dest) {
  StringStreamer streamer(dest);
  switch (value.kind_case()) {
  case Protobuf::Value::KIND_NOT_SET:
  case Protobuf::Value::kNullValue:
    streamer.addNull();
    break;
  case Protobuf::Value::kNumberValue:
    streamer.addNumber(value.number_value());
    break;
  case Protobuf::Value::kStringValue:
    streamer.addString(value.string_value());
    break;
  case Protobuf::Value::kBoolValue:
    streamer.addBool(value.bool_value());
    break;
  case Protobuf::Value::kStructValue:
    structValueToJson(value.struct_value(), *streamer.makeRootMap());
    break;
  case Protobuf::Value::kListValue:
    listValueToJson(value.list_value(), *streamer.makeRootArray());
    break;
  }
}

void Utility::appendStructToString(const Protobuf::Struct& struct_value, std::string& dest) {
  StringStreamer streamer(dest);
  structValueToJson(struct_value, *streamer.makeRootMap());
}

} // namespace Json
} // namespace Envoy
