#include <cstdio>

#include "bazel/cc_proto_descriptor_library/file_descriptor_generator.h"
#include "google/protobuf/compiler/plugin.h"

// NOLINT(namespace-envoy)
int main(int argc, char* argv[]) {
  cc_proto_descriptor_library::ProtoDescriptorGenerator generator;
  return google::protobuf::compiler::PluginMain(argc, argv, &generator);
}
