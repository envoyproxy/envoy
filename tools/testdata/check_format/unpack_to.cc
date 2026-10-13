namespace Envoy {

int foo() {
  ProtobufWky::Any bar;
  Protobuf::Message baz;
  bar.UnpackTo(baz);
}

} // namespace Envoy
