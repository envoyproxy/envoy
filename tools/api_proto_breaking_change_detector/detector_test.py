from pathlib import Path
import unittest

from detector import BufWrapper, ChangeDetectorInitializeError


class BreakingChangeDetectorTests(object):
    detector_type = None

    def run_detector_test(self, testname, is_breaking, expects_changes, additional_args=None):
        tests_path = Path(
            Path(__file__).absolute().parent.parent, "testdata",
            "api_proto_breaking_change_detector", "breaking" if is_breaking else "allowed")

        current = Path(tests_path, f"{testname}_current")
        changed = Path(tests_path, f"{testname}_next")

        detector_obj = self.detector_type(current, changed, additional_args)
        detector_obj.run_detector()

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

    def run_allowed_test(self, testname, additional_args=None):
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

    def test_force_breaking_change(self):
        self.run_allowed_test(self.test_force_breaking_change.__name__, additional_args=["--force"])


class BufTests(TestAllowedChanges, TestBreakingChanges, unittest.TestCase):
    detector_type = BufWrapper

    @unittest.skip("PGV field support not yet added to buf")
    def test_change_pgv_field(self):
        pass

    @unittest.skip("PGV message option support not yet added to buf")
    def test_change_pgv_message(self):
        pass

    @unittest.skip("PGV oneof option support not yet added to buf")
    def test_change_pgv_oneof(self):
        pass

    # copied from protolock tests but might remove
    # It doesn't make sense to evaluate 'forcing' a breaking change in buf because by default,
    # buf lets you re-build without checking for breaking changes
    # Buf does not require forcing breaking changes into the lock file like protolock does
    @unittest.skip("'forcing' a breaking change does not make sense for buf")
    def test_force_breaking_change(self):
        pass


if __name__ == '__main__':
    unittest.main()
