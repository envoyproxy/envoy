"""Tests the api version header file generation.
"""
import generate_api_version_header
from generate_api_version_header import GenerateHeaderFile
import tempfile
import unittest


class GenerateApiVersionHeaderTest(unittest.TestCase):
  EXPECTED_TEMPLATE = """#pragma once
namespace Envoy {

struct ApiVersion {
  uint32_t major;
  uint32_t minor;
  uint32_t patch;
};

constexpr ApiVersion api_version = {%u, %u, %u};

} // namespace Envoy"""

  def test_valid_version(self):
    with tempfile.NamedTemporaryFile() as f:
      f.write('1.2.3')
      f.flush()

      output = generate_api_version_header.GenerateHeaderFile(f.name)
      expected_output = GenerateApiVersionHeaderTest.EXPECTED_TEMPLATE % (1, 2, 3)
      self.assertEqual(expected_output, output)

  def test_invalid_version_string(self):
    with tempfile.NamedTemporaryFile() as f:
      f.write('1.2.abc3')
      f.flush()

      try:
        generate_api_version_header.GenerateHeaderFile(f.name)
        assert False, 'The call to GenerateHeaderFile should have thrown an exception'
      except:
        pass

  def test_invalid_version_partial(self):
    with tempfile.NamedTemporaryFile() as f:
      f.write('1.2.')
      f.flush()

      try:
        generate_api_version_header.GenerateHeaderFile(f.name)
        assert False, 'The call to GenerateHeaderFile should have thrown an exception'
      except:
        pass

  def test_empty_file(self):
    with tempfile.NamedTemporaryFile() as f:
      # Not writing anything to the file
      try:
        generate_api_version_header.GenerateHeaderFile(f.name)
        assert False, 'The call to GenerateHeaderFile should have thrown an exception'
      except:
        pass

  def test_invalid_multiple_lines(self):
    with tempfile.NamedTemporaryFile() as f:
      f.write('1.2.3\n')
      f.flush()

      try:
        generate_api_version_header.GenerateHeaderFile(f.name)
        assert False, 'The call to GenerateHeaderFile should have thrown an exception'
      except:
        pass


if __name__ == '__main__':
  unittest.main()
