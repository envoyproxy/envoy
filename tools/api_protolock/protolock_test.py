import os
import unittest
import tempfile
from shutil import copyfile
import io
import subprocess
from rules_python.python.runfiles import runfiles

DEFAULT_PROTOLOCK_ARGS = ["--plugins="]


class ProtolockTests(unittest.TestCase):

    def setUp(self):
        # create a temporary directory to house the proto.lock file and current proto file
        self.temp_dir = tempfile.TemporaryDirectory()
        self.protolock_path = runfiles.Create().Rlocation(
            "com_github_nilslice_protolock/protolock_/protolock")

    def tearDown(self):
        self.temp_dir.cleanup()

    def run_protolock_test(self, testtype, testname, additional_args=None, expect_no_changes=False):
        # there's probably a better/safer way to navigate
        # but it seems like check_spelling_pedantic_test.py takes this approach as well
        tests_path = os.path.join(
            os.path.dirname(os.path.realpath(__file__)), "..", "testdata", "api_protolock",
            testtype)
        protolock_args = DEFAULT_PROTOLOCK_ARGS + [
            f"--protoroot={self.temp_dir.name}", f"--lockdir={self.temp_dir.name}"
        ]
        if additional_args is not None:
            protolock_args.extend(additional_args)

        # check that test files exist
        current = os.path.join(tests_path, f"{testname}_current")
        changed = os.path.join(tests_path, f"{testname}_next")
        self.assertTrue(os.path.isfile(current))
        self.assertTrue(os.path.isfile(changed))

        # 1) copy start file into temp dir
        # 2) protolock init
        # 3) copy changed file into temp dir
        # 4) protolock commit
        # 5) check for differences (if changes are breaking, there should be none)

        target = os.path.join(self.temp_dir.name, f"{testname}.proto")
        copyfile(current, target)

        # TODO: change this to use tools/run_command.py?
        initial_result = subprocess.run([self.protolock_path, "init", *protolock_args],
                                        capture_output=True)

        self.assertEqual(len(initial_result.stdout), 0)
        self.assertEqual(len(initial_result.stderr), 0)

        lock_location = os.path.join(self.temp_dir.name, "proto.lock")
        with open(lock_location) as f:
            initial_lock = f.readlines()

        copyfile(changed, target)

        final_result = subprocess.run([self.protolock_path, "commit", *protolock_args],
                                      capture_output=True)
        with open(lock_location) as f:
            post_lock = f.readlines()

        if testtype == "breaking":
            self.assertGreater(len(final_result.stdout), 0)
            self.assertRegex(str(final_result.stdout), r"CONFLICT")

            self.assertListEqual(initial_lock, post_lock)
        elif testtype == "allowed":
            self.assertEqual(len(final_result.stdout), 0)
            self.assertEqual(len(final_result.stderr), 0)

            if not expect_no_changes:
                self.assertGreater(len(post_lock), 0)
                self.assertNotEqual(initial_lock, post_lock)
        else:
            raise ValueError(f"Unknown test type: {testtype}")


class TestBreakingChanges(ProtolockTests):

    def test_change_field_id(self):
        self.run_protolock_test("breaking", self.test_change_field_id.__name__)

    def test_change_field_type(self):
        self.run_protolock_test("breaking", self.test_change_field_type.__name__)

    def test_change_field_plurality(self):
        self.run_protolock_test("breaking", self.test_change_field_plurality.__name__)

    def test_change_field_name(self):
        self.run_protolock_test("breaking", self.test_change_field_name.__name__)

    @unittest.skip("package name detection not yet added to Protolock (simply requires plugin)")
    def test_change_package_name(self):
        self.run_protolock_test("breaking", self.test_change_package_name.__name__)

    @unittest.skip(
        "oneof support not yet added to Protolock (requires Protolock changes AND plugin)")
    def test_change_field_from_oneof(self):
        self.run_protolock_test("breaking", self.test_change_field_from_oneof.__name__)

    @unittest.skip(
        "oneof support not yet added to Protolock (requires Protolock changes AND plugin)")
    def test_change_field_to_oneof(self):
        self.run_protolock_test("breaking", self.test_change_field_to_oneof.__name__)

    @unittest.skip("PGV field support not yet added to Protolock (simply requires plugin)")
    def test_change_pgv_field(self):
        self.run_protolock_test("breaking", self.test_change_pgv_field.__name__)

    @unittest.skip("PGV message option support not yet added to Protolock (simply requires plugin)")
    def test_change_pgv_message(self):
        self.run_protolock_test("breaking", self.test_change_pgv_message.__name__)

    @unittest.skip(
        "PGV oneof option support not yet added to Protolock (requires Protolock changes AND plugin)"
    )
    def test_change_pgv_oneof(self):
        self.run_protolock_test("breaking", self.test_change_pgv_oneof.__name__)


class TestAllowedChanges(ProtolockTests):

    def test_add_comment(self):
        self.run_protolock_test("allowed", self.test_add_comment.__name__, expect_no_changes=True)

    def test_add_field(self):
        self.run_protolock_test("allowed", self.test_add_field.__name__)

    def test_add_option(self):
        self.run_protolock_test("allowed", self.test_add_option.__name__)

    def test_add_enum_value(self):
        self.run_protolock_test("allowed", self.test_add_enum_value.__name__)

    def test_remove_and_reserve_field(self):
        self.run_protolock_test("allowed", self.test_remove_and_reserve_field.__name__)

    def test_force_breaking_change(self):
        self.run_protolock_test("allowed", self.test_force_breaking_change.__name__, ["--force"])


if __name__ == '__main__':
    unittest.main()
