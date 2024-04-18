""" Proto Breaking Change Detector Test Suite

This script evaluates breaking change detectors (e.g. buf) against
different protobuf file changes to ensure proper and consistent behavior
in `allowed`and `breaking` circumstances. Although the dependency likely
already tests for these circumstances, these specify Envoy's requirements
and ensure that tool behavior is consistent across dependency updates.
"""

import subprocess
import tempfile
import unittest
from pathlib import Path
from shutil import copyfile, copytree

from rules_python.python.runfiles import runfiles

from tools.api_proto_breaking_change_detector.buf_utils import pull_buf_deps
from tools.api_proto_breaking_change_detector.detector import BufWrapper
from envoy.base.utils import cd_and_return


class BreakingChangeDetectorTests(object):

    def run_detector_test(self, testname, is_breaking, expects_changes, additional_args=None):
        """Runs a test case for an arbitrary breaking change detector type"""
        pass


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
    _buf_path = runfiles.Create().Rlocation("com_github_bufbuild_buf/bin/buf")

    @classmethod
    def _run_command_print_error(cls, cmd):
        response = subprocess.run([cmd], shell=True, capture_output=True, encoding="utf-8")
        code, out, err = response.returncode, response.stdout, response.stderr
        if code != 0:
            raise Exception(
                f"Error running command {cmd}\nExit code: {code} | stdout: {out} | stderr: {err}")

    @classmethod
    def setUpClass(cls):
        try:
            # make temp dir
            # buf requires protobuf files to be in a subdirectory of the directory containing the yaml file
            cls._temp_dir = tempfile.TemporaryDirectory(dir=Path.cwd())
            cls._config_file_loc = Path(cls._temp_dir.name, "buf.yaml")

            # copy in test data
            testdata_path = Path(
                Path.cwd(), "tools", "testdata", "api_proto_breaking_change_detector")
            copytree(testdata_path, cls._temp_dir.name, dirs_exist_ok=True)

            # copy in buf config
            bazel_buf_config_loc = Path.cwd().joinpath("external", "envoy_api", "buf.yaml")
            copyfile(bazel_buf_config_loc, cls._config_file_loc)

            # pull buf dependencies and initialize git repo with test data files
            with cd_and_return(cls._temp_dir.name):
                pull_buf_deps(
                    cls._buf_path, cls._temp_dir.name, config_file_loc=cls._config_file_loc)
                cls._run_command_print_error('git init')
                cls._run_command_print_error('git add .')
                cls._run_command_print_error("git config user.name 'Bazel Test'")
                cls._run_command_print_error("git config user.email '<>'")
                cls._run_command_print_error('git commit -m "Initial commit"')
        except:
            cls.tearDownClass()
            raise

    @classmethod
    def tearDownClass(cls):
        cls._temp_dir.cleanup()

    def tearDown(self):
        # undo changes to proto file that were applied in test
        with cd_and_return(self._temp_dir.name):
            self._run_command_print_error('git reset --hard')

    def run_detector_test(self, testname, is_breaking, expects_changes, additional_args=None):
        """Runs a test case for an arbitrary breaking change detector type"""
        tests_path = Path(self._temp_dir.name, "breaking" if is_breaking else "allowed")

        target = Path(tests_path, f"{testname}.proto")
        changed = Path(tests_path, f"{testname}_changed")

        # make changes to proto file
        copyfile(changed, target)

        # buf breaking
        detector_obj = BufWrapper(
            self._temp_dir.name,
            git_ref="HEAD",
            git_path=Path(self._temp_dir.name, ".git"),
            additional_args=additional_args,
            buf_path=self._buf_path,
            config_file_loc=self._config_file_loc)
        detector_obj.run_detector()

        breaking_response = detector_obj.is_breaking()
        self.assertEqual(breaking_response, is_breaking)

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
