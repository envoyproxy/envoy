#!/usr/bin/python
"""Parses a file containing the API version (X.Y.Z format), and outputs (toxi
stdout) a C++ header file with the ApiVersion struct, and the value.
"""
import sys

FILE_TEMPLATE = """#pragma once
namespace Envoy {

struct ApiVersion {
  uint32_t major;
  uint32_t minor;
  uint32_t patch;
};

constexpr ApiVersion api_version = {%u, %u, %u};

} // namespace Envoy"""


def GenerateHeaderFile(input_path):
  """Generates a c++ header file containing the ApiVersion struct and the
  api_version variable with the correct value.

  Args:
    input_path: the file containing the API version (API_VERSION).

  Returns:
    the header file contents.
  """
  with open(input_path, 'rt') as f:
    lines = f.readlines()
  assert (len(lines) == 1)

  # Mapping each field to int verifies it is a valid version
  major, minor, patch = map(int, lines[0].split('.'))

  header_file_contents = FILE_TEMPLATE % (major, minor, patch)
  return header_file_contents


if __name__ == '__main__':
  input_path = sys.argv[1]
  output = GenerateHeaderFile(input_path)
  # Print output to stdout
  print(output)
