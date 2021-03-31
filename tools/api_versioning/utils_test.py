"""Tests the api version header file generation.
"""
import os
import pathlib
import tempfile
import unittest

import utils


class UtilsTest(unittest.TestCase):

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
    def successful_test_template(self, output_string, expected_version: utils.ApiVersion):
        pathlib.Path(self._temp_fname).write_text(output_string)

        # Read the string from the file, and parse the version.
        version = utils.get_api_version(self._temp_fname)
        self.assertEqual(expected_version, version)

    # General failure pattern when invalid file contents is detected.
    def failed_test_template(self, output_string, assertion_error_type):
        pathlib.Path(self._temp_fname).write_text(output_string)

        # Read the string from the file, and expect version parsing to fail.
        with self.assertRaises(assertion_error_type,
                               msg='The call to get_api_version should have thrown an exception'):
            utils.get_api_version(self._temp_fname)

    def test_valid_version(self):
        self.successful_test_template('1.2.3', utils.ApiVersion(1, 2, 3))

    def test_valid_version_newline(self):
        self.successful_test_template('3.2.1\n', utils.ApiVersion(3, 2, 1))

    def test_invalid_version_string(self):
        self.failed_test_template('1.2.abc3', ValueError)

    def test_invalid_version_partial(self):
        self.failed_test_template('1.2.', ValueError)

    def test_invalid_version_partial2(self):
        self.failed_test_template('1.2', utils.ApiVersionParsingError)

    def test_empty_file(self):
        # Not writing anything to the file
        self.failed_test_template('', utils.ApiVersionParsingError)

    def test_invalid_multiple_lines(self):
        self.failed_test_template('1.2.3\n1.2.3', utils.ApiVersionParsingError)

    def test_valid_oldest_api_version(self):
        expected_latest_oldest_pairs = [(utils.ApiVersion(3, 2, 2), utils.ApiVersion(3, 1, 0)),
                                        (utils.ApiVersion(4, 5, 30), utils.ApiVersion(4, 4, 0)),
                                        (utils.ApiVersion(1, 1, 5), utils.ApiVersion(1, 0, 0)),
                                        (utils.ApiVersion(2, 0, 3), utils.ApiVersion(2, 0, 0))]

        for latest_version, expected_oldest_version in expected_latest_oldest_pairs:
            self.assertEqual(
                expected_oldest_version, utils.compute_oldest_api_version(latest_version))

    def test_valid_deprecated_version_annotation(self):
        self.assertTrue(utils.is_deprecated_annotation_version('1.2'))

    def test_zero_major_deprecated_version_annotation(self):
        self.assertFalse(utils.is_deprecated_annotation_version('0.2'))

    def test_char_deprecated_version_annotation(self):
        self.assertFalse(utils.is_deprecated_annotation_version('1.2a'))

    def test_patch_deprecated_version_annotation(self):
        self.assertFalse(utils.is_deprecated_annotation_version('1.2.3'))

    def test_negative_minor_deprecated_version_annotation(self):
        self.assertFalse(utils.is_deprecated_annotation_version('1.-2'))

    def test_missing_major_deprecated_version_annotation(self):
        self.assertFalse(utils.is_deprecated_annotation_version('.2'))

    def test_single_number_deprecated_version_annotation(self):
        self.assertFalse(utils.is_deprecated_annotation_version('1'))

    def test_empty_number_deprecated_version_annotation(self):
        self.assertFalse(utils.is_deprecated_annotation_version(''))


if __name__ == '__main__':
    unittest.main()
