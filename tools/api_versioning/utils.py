"""Utility functions for accessing the API version (X.Y.Z format).
"""
from collections import namedtuple
import pathlib

ApiVersion = namedtuple('ApiVersion', ['major', 'minor', 'patch'])


# Errors that happen during API version parsing.
class ApiVersionParsingError(Exception):
    pass


def get_api_version(input_path):
    """Returns the API version from a given API version input file.

    Args:
      input_path: the file containing the API version (API_VERSION.txt).

    Returns:
      a namedtuple containing the major, minor, patch versions.

    Raises:
      ApiVersionParsingError: If the given file has no lines or more than one line, or if
          the line doesn't include exactly two dots ('.').
    """
    lines = pathlib.Path(input_path).read_text().splitlines()
    if len(lines) != 1 or lines[0].count('.') != 2:
        raise ApiVersionParsingError(
            'Api version file must have a single line of format X.Y.Z, where X, Y, and Z are non-negative integers.'
        )

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


def is_deprecated_annotation_version(version: str):
    """Validates that a given version string is of format X.Y, where X and Y are
    integers, X>0, and Y>=0.

    Args:
      version: a minor version deprecation annotation value (see api/envoy/annotations/deprecation.proto)

    Returns:
      True iff the given string represents a valid X.Y version.
    """
    if version.count('.') != 1:
        return False
    # Validate major and minor parts.
    try:
        major, minor = [int(x) for x in version.split('.')]
        if major <= 0:
            return False
        if minor < 0:
            return False
    except ValueError:
        return False
    return True
