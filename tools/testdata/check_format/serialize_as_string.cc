namespace Envoy {

void use_serialize_as_string() {
  Protobuf::FieldMask mask;
  const std::string key = mask.SerializeAsString();
}

} // namespace Envoy
