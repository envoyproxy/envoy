"""Python protoc plugin for Envoy APIs."""

import cProfile
from collections import namedtuple
import io
import os
import pstats
import sys

from tools.api_proto_plugin import traverse

from google.protobuf.compiler import plugin_pb2

OutputDescriptor = namedtuple(
    'OutputDescriptor',
    [
        # Output files are generated alongside their corresponding input .proto,
        # with the output_suffix appended.
        'output_suffix',
        # The visitor factory is a function to create a visitor.Visitor defining
        # the business logic of the plugin for the specific output descriptor.
        'visitor_factory',
        # FileDescriptorProto transformer; this is applied to the input
        # before any output generation.
        'xform',
    ])


def DirectOutputDescriptor(output_suffix, visitor):
  return OutputDescriptor(output_suffix, visitor, lambda x: x)


def Plugin(output_descriptors, parameter_callback=None):
  """Protoc plugin entry point.

  This defines protoc plugin and manages the stdin -> stdout flow. An
  api_proto_plugin is defined by the provided visitor.

  See
  http://www.expobrain.net/2015/09/13/create-a-plugin-for-google-protocol-buffer/
  for further details on protoc plugin basics.

  Args:
    output_descriptors: a list of OutputDescriptors.
  """
  request = plugin_pb2.CodeGeneratorRequest()
  request.ParseFromString(sys.stdin.buffer.read())
  response = plugin_pb2.CodeGeneratorResponse()
  cprofile_enabled = os.getenv('CPROFILE_ENABLED')

  if request.HasField("parameter") and parameter_callback:
    parameter_callback(request.parameter)

  # We use request.file_to_generate rather than request.file_proto here since we
  # are invoked inside a Bazel aspect, each node in the DAG will be visited once
  # by the aspect and we only want to generate docs for the current node.
  for file_to_generate in request.file_to_generate:
    # Find the FileDescriptorProto for the file we actually are generating.
    file_proto = [pf for pf in request.proto_file if pf.name == file_to_generate][0]
    if cprofile_enabled:
      pr = cProfile.Profile()
      pr.enable()
    for od in output_descriptors:
      f = response.file.add()
      f.name = file_proto.name + od.output_suffix
      xformed_proto = od.xform(file_proto)
      f.content = traverse.TraverseFile(xformed_proto,
                                        od.visitor_factory()) if xformed_proto else ''
    if cprofile_enabled:
      pr.disable()
      stats_stream = io.StringIO()
      ps = pstats.Stats(pr,
                        stream=stats_stream).sort_stats(os.getenv('CPROFILE_SORTBY', 'cumulative'))
      stats_file = response.file.add()
      stats_file.name = file_proto.name + '.profile'
      ps.print_stats()
      stats_file.content = stats_stream.getvalue()
    # Also include the original FileDescriptorProto as text proto, this is
    # useful when debugging.
    descriptor_file = response.file.add()
    descriptor_file.name = file_proto.name + ".descriptor.proto"
    descriptor_file.content = str(file_proto)
  sys.stdout.buffer.write(response.SerializeToString())
