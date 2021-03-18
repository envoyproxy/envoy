#!/usr/bin/python
"""Parses a file containing the API version (X.Y.Z format), and outputs (to
stdout) a C++ header file with the ApiVersion value.
"""
from collections import namedtuple
import pathlib
import string
import sys

ApiVersion = namedtuple('ApiVersion', ['major', 'minor', 'patch'])

FILE_TEMPLATE = string.Template("""#pragma once
#include "common/version/api_version_struct.h"

namespace Envoy {

constexpr ApiVersion api_version = {$major, $minor, $patch};
constexpr ApiVersion oldest_api_version = {$oldest_major, $oldest_minor, $oldest_patch};

} // namespace Envoy""")


def GenerateHeaderFile(input_path):
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
    oldest_version = ComputeOldestApiVersion(version)

    header_file_contents = FILE_TEMPLATE.substitute({
        'major': version.major,
        'minor': version.minor,
        'patch': version.patch,
        'oldest_major': oldest_version.major,
        'oldest_minor': oldest_version.minor,
        'oldest_patch': oldest_version.patch
    })
    return header_file_contents


def ComputeOldestApiVersion(current_version: ApiVersion):
    """Computest the oldest API version the client supports. According to the
  specification (see: api/API_VERSIONING.md), Envoy supports up to 2 most
  recent minor versions. Therefore if the latest API version "X.Y.Z", Envoy's
  oldest API version is "X.Y-1.0". Note that the major number is always the
  same as the latest version, and the patch number is always 0. In addition,
  the minor number is at least 0, and the oldest api version cannot be set
  to a previous major number.

  Args:
    current_version: the current API version.

  Returns:
    the oldest supported API version.
  """
    return ApiVersion(current_version.major, max(current_version.minor - 1, 0), 0)


if __name__ == '__main__':
    input_path = sys.argv[1]
    output = GenerateHeaderFile(input_path)
    # Print output to stdout
    print(output)
