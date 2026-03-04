import unittest
from tools.oncall.gen_ical_lib import gen_ical
from tools.oncall.parsing import ical_path


class TestIcal(unittest.TestCase):

    def test_ical_up_to_date(self):
        checked_in_ical_path = ical_path()
        with open(checked_in_ical_path, 'r') as file:
            checked_in_ical = str(file.read())
        assert gen_ical().strip() == checked_in_ical.strip(
        ), "generated rotation and checked in rotation don't match - to fix run\n  bazel run //tools/oncall:rotation"


if __name__ == '__main__':
    unittest.main()
