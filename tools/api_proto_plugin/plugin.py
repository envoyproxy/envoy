"""Python protoc plugin for Envoy APIs."""

import cProfile
import io
import os
import pstats
import sys

from tools.api_proto_plugin import traverse

from google.protobuf.compiler import plugin_pb2


def Plugin(output_suffix, visitor):
  """Protoc plugin entry point.

  This defines protoc plugin and manages the stdin -> stdout flow. An
  api_proto_plugin is defined by the provided visitor.

  See
  http://www.expobrain.net/2015/09/13/create-a-plugin-for-google-protocol-buffer/
  for further details on protoc plugin basics.

  Args:
    output_suffix: output files are generated alongside their corresponding
      input .proto, with this filename suffix.
    visitor: visitor.Visitor defining the business logic of the plugin.
  """
  request = plugin_pb2.CodeGeneratorRequest()
  request.ParseFromString(sys.stdin.buffer.read())
  response = plugin_pb2.CodeGeneratorResponse()
  cprofile_enabled = os.getenv('CPROFILE_ENABLED')

  # We use request.file_to_generate rather than request.file_proto here since we
  # are invoked inside a Bazel aspect, each node in the DAG will be visited once
  # by the aspect and we only want to generate docs for the current node.
  for file_to_generate in request.file_to_generate:
    # Find the FileDescriptorProto for the file we actually are generating.
    file_proto = [pf for pf in request.proto_file if pf.name == file_to_generate][0]
    f = response.file.add()
    f.name = file_proto.name + output_suffix
    if cprofile_enabled:
      pr = cProfile.Profile()
      pr.enable()
    # We don't actually generate any RST right now, we just string dump the
    # input proto file descriptor into the output file.
    f.content = traverse.TraverseFile(file_proto, visitor)
    if cprofile_enabled:
      pr.disable()
      stats_stream = io.StringIO()
      ps = pstats.Stats(pr,
                        stream=stats_stream).sort_stats(os.getenv('CPROFILE_SORTBY', 'cumulative'))
      stats_file = response.file.add()
      stats_file.name = file_proto.name + output_suffix + '.profile'
      ps.print_stats()
      stats_file.content = stats_stream.getvalue()
  sys.stdout.buffer.write(response.SerializeToString())
