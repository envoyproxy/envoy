#!/usr/bin/python
"""Parses a file containing the API version (X.Y.Z format), and outputs (to
stdout) a C++ header file with the ApiVersion value.
"""
import pathlib
import string
import sys

FILE_TEMPLATE = string.Template("""#pragma once
#include "common/version/api_version_struct.h"

namespace Envoy {

constexpr ApiVersion api_version = {$major, $minor, $patch};

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
  major, minor, patch = map(int, lines[0].split('.'))

  header_file_contents = FILE_TEMPLATE.substitute({'major': major, 'minor': minor, 'patch': patch})
  return header_file_contents


if __name__ == '__main__':
  input_path = sys.argv[1]
  output = GenerateHeaderFile(input_path)
  # Print output to stdout
  print(output)
