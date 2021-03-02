"""Tests the api version header file generation.
"""
import generate_api_version_header
import os
import pathlib
import string
import tempfile
import unittest


class GenerateApiVersionHeaderTest(unittest.TestCase):
  EXPECTED_TEMPLATE = string.Template("""#pragma once
#include "common/version/api_version_struct.h"

namespace Envoy {

constexpr ApiVersion api_version = {$major, $minor, $patch};

} // namespace Envoy""")

  def setUp(self):
    # Using mkstemp instead of NamedTemporaryFile because in windows NT or later
    # the created NamedTemporaryFile cannot be reopened again (see comment in:
    # https://docs.python.org/3.9/library/tempfile.html#tempfile.NamedTemporaryFile)
    self._temp_fd, self._temp_fname = tempfile.mkstemp(text=True)

  def tearDown(self):
    # Close and delete the temp file.
    os.close(self._temp_fd)
    pathlib.Path(self._temp_fname).unlink()

  # General success pattern when valid file contents is detected.
  def SuccessfulTestTemplate(self, output_string, major, minor, patch):
    pathlib.Path(self._temp_fname).write_text(output_string)

    # Read the string from the file, and parse the version.
    output = generate_api_version_header.GenerateHeaderFile(self._temp_fname)
    expected_output = GenerateApiVersionHeaderTest.EXPECTED_TEMPLATE.substitute({
        'major': major,
        'minor': minor,
        'patch': patch
    })
    self.assertEqual(expected_output, output)

  # General failure pattern when invalid file contents is detected.
  def FailedTestTemplate(self, output_string, assertion_error_type):
    pathlib.Path(self._temp_fname).write_text(output_string)

    # Read the string from the file, and expect version parsing to fail.
    with self.assertRaises(assertion_error_type,
                           msg='The call to GenerateHeaderFile should have thrown an exception'):
      generate_api_version_header.GenerateHeaderFile(self._temp_fname)

  def test_valid_version(self):
    self.SuccessfulTestTemplate('1.2.3', 1, 2, 3)

  def test_valid_version_newline(self):
    self.SuccessfulTestTemplate('3.2.1\n', 3, 2, 1)

  def test_invalid_version_string(self):
    self.FailedTestTemplate('1.2.abc3', ValueError)

  def test_invalid_version_partial(self):
    self.FailedTestTemplate('1.2.', ValueError)

  def test_empty_file(self):
    # Not writing anything to the file
    self.FailedTestTemplate('', AssertionError)

  def test_invalid_multiple_lines(self):
    self.FailedTestTemplate('1.2.3\n1.2.3', AssertionError)


if __name__ == '__main__':
  unittest.main()
