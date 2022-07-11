"""Python protoc plugin for Envoy APIs."""

import sys
from collections import namedtuple

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
        # Supply --//tools/api_proto_plugin CLI args as a parameters dictionary
        # to visitor_factory constructor and xform function?
        'want_params',
    ])


def direct_output_descriptor(output_suffix, visitor, want_params=False):
    return OutputDescriptor(
        output_suffix, visitor, (lambda x, _: x) if want_params else lambda x: x, want_params)


# TODO(phlax): make this into a class
def plugin(output_descriptors, traverser=None):
    """Protoc plugin entry point.

    This defines protoc plugin and manages the stdin -> stdout flow. An
    api_proto_plugin is defined by the provided visitor.

    See
        http://www.expobrain.net/2015/09/13/create-a-plugin-for-google-protocol-buffer/
          for further details on protoc plugin basics.

    Args:
        output_descriptors: a list of OutputDescriptors.
    """

    traverser = traverser or traverse.traverse_file

    request = plugin_pb2.CodeGeneratorRequest()
    request.ParseFromString(sys.stdin.buffer.read())
    response = plugin_pb2.CodeGeneratorResponse()

    # We use request.file_to_generate rather than request.file_proto here since we
    # are invoked inside a Bazel aspect, each node in the DAG will be visited once
    # by the aspect and we only want to generate docs for the current node.
    for file_to_generate in request.file_to_generate:
        # Find the FileDescriptorProto for the file we actually are generating.
        file_proto = [pf for pf in request.proto_file if pf.name == file_to_generate][0]
        for od in output_descriptors:
            f = response.file.add()
            f.name = f"{file_proto.name}{od.output_suffix}"
            if request.HasField("parameter") and od.want_params:
                params = dict(param.split('=') for param in request.parameter.split(','))
                xformed_proto = od.xform(file_proto, params)
                visitor_factory = od.visitor_factory(params)
            else:
                xformed_proto = od.xform(file_proto)
                visitor_factory = od.visitor_factory()
            f.content = traverser(xformed_proto, visitor_factory)
    sys.stdout.buffer.write(response.SerializeToString())
