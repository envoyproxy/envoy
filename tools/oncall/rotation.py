from aio.run import runner
from datetime import timedelta
import os
from pathlib import Path
import sys

from tools.oncall.oncall_calendar import OncallCalendar

CONSIDER_DIRTY_REMOVES_SAFE_AFTER = timedelta(weeks=8)


def _envoy_path(path: str) -> Path:
    return Path(os.getenv('ENVOY_SRCDIR'), path)


def _calendar_yaml_path() -> Path:
    return _envoy_path("tools/oncall/rotation.yaml")


def _ical_path() -> Path:
    return _envoy_path("tools/oncall/rotation.ical")


class OncallRotation(runner.Runner):

    def add_arguments(self, parser) -> None:
        super().add_arguments(parser)
        parser.add_argument(
            "--add",
            type=str,
            help=
            "a username to add to rotation. The script will insert such that the first shift for the new oncall is as far in the future as possible, to minimize schedule disruption. If add and remove are both set, the added name simply replaces the removed name.",
        )
        parser.add_argument(
            "--remove",
            type=str,
            help=
            "a username to remove from rotation. If this would disrupt the schedule soon, either --dirty=True must be passed to allow disruption, or a --substitute for the soonest oncall shift of the removed maintainer",
        )
        parser.add_argument(
            "--dirty",
            type=bool,
            default=False,
            help=
            "when removing an oncall, passing --dirty=True allows removal to just be simple removal with immediate schedule compression, which may disrupt other oncalls' expectations - adjusting overrides manually may also be required",
        )
        parser.add_argument(
            "--substitute",
            type=str,
            help=
            "when removing an oncall, passing a different oncall as --substitute will add the substitute oncall as an override for the first oncall shift to be removed, effectively keeping the schedule from 'compressing' until the next cycle",
        )

    async def run(self):
        path = _calendar_yaml_path()
        calendar = OncallCalendar.load(path)
        if self.args.add or self.args.remove:
            calendar.remove_outdated_overrides()
            if self.args.add and self.args.remove:
                calendar.replace(new=self.args.add, old=self.args.remove)
            elif self.args.add:
                calendar.add(self.args.add)
            else:
                calendar.remove(
                    self.args.remove, self.args.substitute,
                    timedelta(0)
                    if self.args.dirty else CONSIDER_DIRTY_REMOVES_SAFE_AFTER)
            calendar.save(path)

        _ical_path().write_text(calendar.as_ical_file)


if __name__ == "__main__":
    sys.exit(OncallRotation(*sys.argv[1:])())
