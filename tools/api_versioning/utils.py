#!/usr/bin/python
"""Utility functions for accessing the API version (X.Y.Z format).
"""
from collections import namedtuple
import pathlib
import string
import sys

ApiVersion = namedtuple('ApiVersion', ['major', 'minor', 'patch'])


def get_api_version(input_path):
    """Returns the API version from a given API version input file.

  Args:
    input_path: the file containing the API version (API_VERSION).

  Returns:
    a namedtuple containing the major, minor, patch versions.
  """
    lines = pathlib.Path(input_path).read_text().splitlines()
    assert len(lines) == 1

    # Mapping each field to int verifies it is a valid version
    return ApiVersion(*map(int, lines[0].split('.')))


def compute_oldest_api_version(current_version: ApiVersion):
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
