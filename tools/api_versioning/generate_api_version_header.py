"""Parses a file containing the API version (X.Y.Z format), and outputs (to
stdout) a C++ header file with the ApiVersion value.
"""
import string
import sys

from tools.api_versioning import utils

FILE_TEMPLATE = string.Template(
    """#pragma once
#include "source/common/version/api_version_struct.h"

namespace Envoy {

constexpr ApiVersion api_version = {$major, $minor, $patch};
constexpr ApiVersion oldest_api_version = {$oldest_major, $oldest_minor, $oldest_patch};

} // namespace Envoy""")


def generate_header_file(input_path):
    """Generates a c++ header file containing the api_version variable with the
    correct value.

    Args:
        input_path: the file containing the API version (API_VERSION.txt).

    Returns:
        the header file contents.
    """
    version = utils.get_api_version(input_path)
    oldest_version = utils.compute_oldest_api_version(version)

    header_file_contents = FILE_TEMPLATE.substitute({
        'major': version.major,
        'minor': version.minor,
        'patch': version.patch,
        'oldest_major': oldest_version.major,
        'oldest_minor': oldest_version.minor,
        'oldest_patch': oldest_version.patch
    })
    return header_file_contents


if __name__ == '__main__':
    input_path = sys.argv[1]
    output = generate_header_file(input_path)
    # Print output to stdout
    print(output)
