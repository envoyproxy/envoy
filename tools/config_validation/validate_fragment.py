# Validate a YAML fragment against an Envoy API proto3 type.
#
# Example usage:
#
# bazel run //tools/config_validation:validate_fragment -- \
#   envoy.config.bootstrap.v3.Bootstrap $PWD/configs/envoyproxy_io_proxy.yaml

import argparse
import pathlib

from envoy.base.utils import ProtobufValidator

# These functions are maintained for backward compatibility, and to provide a CLI validator
# Do not use these functions as library code - use `envoy.base.utils.ProtobufValidator` directly.


def validate_fragment(type_name, fragment, descriptor_path):
    """Validate a dictionary representing a JSON/YAML fragment against an Envoy API proto3 type.

    Throws Protobuf errors on parsing exceptions, successful validations produce
    no result.

    Args:
        type_name: a string providing the type name, e.g.
          envoy.config.bootstrap.v3.Bootstrap.
        fragment: a dictionary representing the parsed JSON/YAML configuration
          fragment.
    """
    ProtobufValidator(descriptor_path).validate_fragment(fragment, type_name)


def validate_yaml(type_name, content, descriptor_path):
    ProtobufValidator(descriptor_path).validate_yaml(content, message_type)


def parse_args():
    parser = argparse.ArgumentParser(
        description='Validate a YAML fragment against an Envoy API proto3 type.')
    parser.add_argument(
        'message_type',
        help='a string providing the type name, e.g. envoy.config.bootstrap.v3.Bootstrap.')
    parser.add_argument('fragment_path', nargs='?', help='Path to a YAML configuration fragment.')
    parser.add_argument('-s', required=False, help='YAML configuration fragment.')
    parser.add_argument('--descriptor_path', nargs='?', help='Path to a protobuf descriptor file.')
    return parser.parse_args()


if __name__ == '__main__':
    parsed_args = parse_args()
    message_type = parsed_args.message_type
    content = parsed_args.s if (parsed_args.fragment_path is None) else pathlib.Path(
        parsed_args.fragment_path).read_text()
    ProtobufValidator(parsed_args.descriptor_path).validate_yaml(content, message_type)
