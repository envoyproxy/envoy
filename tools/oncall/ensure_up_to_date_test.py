import os
import unittest
from pathlib import Path
import yaml

from tools.oncall.oncall_calendar import OncallCalendar


def _runfiles_path(path: str) -> Path:
    return Path(os.getenv('RUNFILES_DIR'), "envoy", path)


def _ical_path() -> Path:
    return _runfiles_path("tools/oncall/rotation.ical")


def _rotation_path() -> Path:
    return _runfiles_path("tools/oncall/rotation.yaml")


def _reviewers_path() -> Path:
    return _runfiles_path("reviewers.yaml")


class TestIcal(unittest.TestCase):

    def test_reviewers_in_sync_with_oncall(self):
        calendar = OncallCalendar.load(_rotation_path())
        with open(_reviewers_path(), 'r') as file:
            reviewers = yaml.safe_load(file)
        maintainers = set([
            reviewer for reviewer, props in reviewers.items()
            if props.get('maintainer', False)
        ])
        rotation = set(calendar.rotation)
        if maintainers - rotation:
            self.fail(
                f"maintainers in reviewers.yaml include {maintainers - rotation}; missing from rotation.yaml"
            )
        if rotation - maintainers:
            self.fail(
                f"rotation.yaml includes {rotation - maintainers}; not maintainers in reviewers.yaml"
            )

    def test_ical_up_to_date(self):
        checked_in_ical = _ical_path().read_text()
        calendar = OncallCalendar.load(_rotation_path())
        assert calendar.as_ical_file.strip() == checked_in_ical.strip(
        ), "generated rotation and checked in rotation don't match - to fix run\n  bazel run //tools/oncall:rotation"


if __name__ == '__main__':
    unittest.main()
