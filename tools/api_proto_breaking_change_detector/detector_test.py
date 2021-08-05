""" Proto Breaking Change Detector Test Suite

This script evaluates breaking change detectors (e.g. buf) against
different protobuf file changes to ensure proper and consistent behavior
in `allowed`and `breaking` circumstances. Although the dependency likely
already tests for these circumstances, these specify Envoy's requirements
and ensure that tool behavior is consistent across dependency updates.
"""

from pathlib import Path
import unittest

from detector import ProtoBreakingChangeDetector, BufWrapper
from detector_errors import ChangeDetectorInitializeError

import tempfile
from rules_python.python.runfiles import runfiles
from tools.run_command import run_command
from shutil import copyfile
import os
from buf_utils import make_lock, pull_buf_deps
from typing import Tuple


class BreakingChangeDetectorTests(object):

    def initialize_test(self, testname, current_file, changed_file, additional_args=None):
        pass

    def create_detector(
            self,
            lock_location,
            changed_directory,
            additional_args=None) -> ProtoBreakingChangeDetector:
        pass

    def run_detector_test(self, testname, is_breaking, expects_changes, additional_args=None):
        """Runs a test case for an arbitrary breaking change detector type"""
        tests_path = Path(
            Path(__file__).absolute().parent.parent, "testdata",
            "api_proto_breaking_change_detector", "breaking" if is_breaking else "allowed")

        current = Path(tests_path, f"{testname}_current")
        changed = Path(tests_path, f"{testname}_next")

        # buf requires protobuf files to be in a subdirectory of the yaml file
        with tempfile.TemporaryDirectory(prefix=str(Path(".").absolute()) + os.sep) as temp_dir:
            lock_location, changed_dir = self.initialize_test(
                testname, temp_dir, current, changed, additional_args)

            detector_obj = self.create_detector(lock_location, changed_dir, additional_args)
            detector_obj.run_detector()
            detector_obj.update_lock_file()

        breaking_response = detector_obj.is_breaking()
        self.assertEqual(breaking_response, is_breaking)

        lock_file_changed_response = detector_obj.lock_file_changed()
        self.assertEqual(lock_file_changed_response, expects_changes)


class TestBreakingChanges(BreakingChangeDetectorTests):

    def run_breaking_test(self, testname):
        self.run_detector_test(testname, is_breaking=True, expects_changes=False)

    def test_change_field_id(self):
        self.run_breaking_test(self.test_change_field_id.__name__)

    def test_change_field_type(self):
        self.run_breaking_test(self.test_change_field_type.__name__)

    def test_change_field_plurality(self):
        self.run_breaking_test(self.test_change_field_plurality.__name__)

    def test_change_field_name(self):
        self.run_breaking_test(self.test_change_field_name.__name__)

    def test_change_package_name(self):
        self.run_breaking_test(self.test_change_package_name.__name__)

    def test_change_field_from_oneof(self):
        self.run_breaking_test(self.test_change_field_from_oneof.__name__)

    def test_change_field_to_oneof(self):
        self.run_breaking_test(self.test_change_field_to_oneof.__name__)

    def test_change_pgv_field(self):
        self.run_breaking_test(self.test_change_pgv_field.__name__)

    def test_change_pgv_message(self):
        self.run_breaking_test(self.test_change_pgv_message.__name__)

    def test_change_pgv_oneof(self):
        self.run_breaking_test(self.test_change_pgv_oneof.__name__)


class TestAllowedChanges(BreakingChangeDetectorTests):

    def run_allowed_test(self, testname):
        self.run_detector_test(testname, is_breaking=False, expects_changes=True)

    def test_add_comment(self):
        self.run_allowed_test(self.test_add_comment.__name__)

    def test_add_field(self):
        self.run_allowed_test(self.test_add_field.__name__)

    def test_add_option(self):
        self.run_allowed_test(self.test_add_option.__name__)

    def test_add_enum_value(self):
        self.run_allowed_test(self.test_add_enum_value.__name__)

    def test_remove_and_reserve_field(self):
        self.run_allowed_test(self.test_remove_and_reserve_field.__name__)


class BufTests(TestAllowedChanges, TestBreakingChanges, unittest.TestCase):
    _config_file_loc = Path(".", "buf.yaml")
    _buf_path = runfiles.Create().Rlocation("com_github_bufbuild_buf/bin/buf")
    _buf_state_file = "tmp.json"

    def initialize_test(
            self, testname, target_path, current_file, changed_file, additional_args=None):
        target = Path(target_path, f"{testname}.proto")
        copyfile(current_file, target)
        lock_location = Path(target_path, self._buf_state_file)

        bazel_buf_config_loc = Path(".", "external", "envoy_api_canonical", "buf.yaml")
        copyfile(bazel_buf_config_loc, self._config_file_loc)

        pull_buf_deps(
            self._buf_path,
            target_path,
            config_file_loc=self._config_file_loc,
            additional_args=additional_args)

        make_lock(
            self._buf_path,
            target_path,
            lock_location,
            config_file_loc=self._config_file_loc,
            additional_args=additional_args)

        copyfile(changed_file, target)

        return lock_location, target_path

    def create_detector(self, lock_location, changed_directory, additional_args=None) -> BufWrapper:
        return BufWrapper(
            changed_directory,
            additional_args=additional_args,
            buf_path=self._buf_path,
            config_file_loc=self._config_file_loc,
            path_to_lock_file=lock_location)

    @unittest.skip("PGV field support not yet added to buf")
    def test_change_pgv_field(self):
        pass

    @unittest.skip("PGV message option support not yet added to buf")
    def test_change_pgv_message(self):
        pass

    @unittest.skip("PGV oneof option support not yet added to buf")
    def test_change_pgv_oneof(self):
        pass


if __name__ == '__main__':
    unittest.main()
