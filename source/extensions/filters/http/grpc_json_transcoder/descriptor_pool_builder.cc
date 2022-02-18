#include "source/extensions/filters/http/grpc_json_transcoder/descriptor_pool_builder.h"

using Envoy::Protobuf::FileDescriptorSet;
using std::string;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcJsonTranscoder {

void DescriptorPoolBuilder::requestDescriptorPool(
    std::function<void(std::unique_ptr<Protobuf::DescriptorPool>&&)> descriptor_pool_available) {
  descriptor_pool_available_ = descriptor_pool_available;
  FileDescriptorSet descriptor_set;

  switch (proto_config_.descriptor_set_case()) {
  case envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder::
      DescriptorSetCase::kProtoDescriptor:
    if (!descriptor_set.ParseFromString(
            api_->fileSystem().fileReadToEnd(proto_config_.proto_descriptor()))) {
      throw EnvoyException("transcoding_filter: Unable to parse proto descriptor");
    }
    loadFileDescriptorProtos(descriptor_set);
    break;
  case envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder::
      DescriptorSetCase::kProtoDescriptorBin:
    if (!descriptor_set.ParseFromString(proto_config_.proto_descriptor_bin())) {
      throw EnvoyException("transcoding_filter: Unable to parse proto descriptor");
    }
    loadFileDescriptorProtos(descriptor_set);
    break;
  case envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder::
      DescriptorSetCase::kReflectionClusterConfig:
    // The constructor of this object is responsible for ensuring the async_reflection_fetcher_ is
    // not null.
    async_reflection_fetcher_->requestFileDescriptors(
        [this](Envoy::Protobuf::FileDescriptorSet file_descriptor_set) {
          this->loadFileDescriptorProtos(file_descriptor_set);
        });
    break;
  case envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder::
      DescriptorSetCase::DESCRIPTOR_SET_NOT_SET:
    throw EnvoyException("transcoding_filter: descriptor not set");
  }
}

void DescriptorPoolBuilder::loadFileDescriptorProtos(const FileDescriptorSet& file_descriptor_set) {
  // We dedup the file_descriptor_set because multiple gRPC services can be
  // defined in a single proto file, and different gRPC services can share
  // protos. When gRPC refleciton is used, we make a request for the file
  // descriptors associated with each gRPC service so these can overlap.
  //
  // After dedupping, we perform a topological sort on the file_descriptor_set
  // so that dependencies are always built before the files that depend on
  // them. This is needed because gRPC server reflection is not guaranteed to
  // return transitive dependencies in a sorted order.
  //
  // Note that the decision to put this logic in this class rather than
  // AsyncReflectionFetcher makes this class more general-purpose (all sources
  // of file descriptors no longer need to be pre-processed) at the cost of
  // additional cycles when the input is already deduped and topologically
  // sorted. If performance becomes an issue, this decision may need to be
  // revisited.
  absl::flat_hash_map<std::string, Envoy::Protobuf::FileDescriptorProto> filename_to_descriptor;

  for (const auto& file : file_descriptor_set.file()) {
    filename_to_descriptor[file.name()] = file;
  }

  // Build the dependency graph using file names since they are lighter weight
  // than protos.
  absl::flat_hash_map<string, absl::flat_hash_set<string>> incoming_edges;
  absl::flat_hash_map<string, absl::flat_hash_set<string>> outgoing_edges;
  absl::flat_hash_set<string> no_incoming_edges;

  for (auto& it : filename_to_descriptor) {
    const std::string& filename = it.first;
    const Envoy::Protobuf::FileDescriptorProto& proto = it.second;
    if (proto.dependency_size() > 0) {
      if (incoming_edges.find(filename) == incoming_edges.end()) {
        incoming_edges[filename] = absl::flat_hash_set<string>();
      }
      for (auto& dependency : proto.dependency()) {
        incoming_edges[filename].insert(dependency);
        if (outgoing_edges.find(dependency) == outgoing_edges.end()) {
          outgoing_edges[dependency] = absl::flat_hash_set<string>();
        }
        outgoing_edges[dependency].insert(filename);
      }
    } else {
      no_incoming_edges.insert(filename);
    }
  }

  std::vector<string> top_sorted_filenames;
  while (!no_incoming_edges.empty()) {
    std::string node = *no_incoming_edges.begin();
    top_sorted_filenames.push_back(node);
    no_incoming_edges.erase(node);

    for (auto dependent_node : outgoing_edges[node]) {
      incoming_edges[dependent_node].erase(node);
      if (incoming_edges[dependent_node].size() == 0) {
        no_incoming_edges.insert(dependent_node);
      }
    }
  }

  // Verify that all files are in among the sorted filenames. If any are
  // missing, that implies a missing dependency because the topological sort
  // will only pick up files whose dependencies are available.
  if (top_sorted_filenames.size() != filename_to_descriptor.size()) {
    ENVOY_LOG(error,
              "transcoding_filter: Missing dependencies when building proto descriptor pool");
    return;
  }

  std::unique_ptr<Protobuf::DescriptorPool> descriptor_pool =
      std::make_unique<Protobuf::DescriptorPool>();
  for (auto filename : top_sorted_filenames) {
    if (!descriptor_pool->BuildFile(filename_to_descriptor[filename])) {
      ENVOY_LOG(error, "transcoding_filter: Unable to build proto descriptor pool");
      return;
    }
  }

  bool convert_grpc_status = proto_config_.convert_grpc_status();
  if (convert_grpc_status) {
    // Add built-in symbols
    for (auto& symbol_name : {"google.protobuf.Any", "google.rpc.Status"}) {
      if (descriptor_pool->FindFileContainingSymbol(symbol_name) != nullptr) {
        continue;
      }

      auto* builtin_pool = Protobuf::DescriptorPool::generated_pool();
      if (!builtin_pool) {
        continue;
      }

      Protobuf::DescriptorPoolDatabase pool_database(*builtin_pool);
      Protobuf::FileDescriptorProto file_proto;
      pool_database.FindFileContainingSymbol(symbol_name, &file_proto);
      if (!descriptor_pool->BuildFile(file_proto)) {
        ENVOY_LOG(error, "transcoding_filter: Unable to build proto descriptor pool");
        return;
      }
    }
  }

  descriptor_pool_available_(std::move(descriptor_pool));
}

} // namespace GrpcJsonTranscoder
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
