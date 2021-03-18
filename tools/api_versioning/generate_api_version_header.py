#!/usr/bin/python
"""Parses a file containing the API version (X.Y.Z format), and outputs (to
stdout) a C++ header file with the ApiVersion value.
"""
from collections import namedtuple
import pathlib
import string
import sys
import utils
from utils import ApiVersion

FILE_TEMPLATE = string.Template("""#pragma once
#include "common/version/api_version_struct.h"

namespace Envoy {

constexpr ApiVersion api_version = {$major, $minor, $patch};
constexpr ApiVersion oldest_api_version = {$oldest_major, $oldest_minor, $oldest_patch};

} // namespace Envoy""")


def generate_header_file(input_path):
    """Generates a c++ header file containing the api_version variable with the
  correct value.

  Args:
    input_path: the file containing the API version (API_VERSION).

  Returns:
    the header file contents.
  """
    lines = pathlib.Path(input_path).read_text().splitlines()
    assert (len(lines) == 1)

    # Mapping each field to int verifies it is a valid version
    version = ApiVersion(*map(int, lines[0].split('.')))
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
