from aio.run import runner
from datetime import date, timedelta
import sys

from tools.oncall.gen_ical_lib import gen_ical
from tools.oncall.parsing import ical_path, maintainers_from_reviewers, overrides_from_yaml, overrides_to_yaml, rotation_from_yaml, rotation_to_yaml, who_would_be_oncall, Override

CONSIDER_DIRTY_REMOVES_SAFE_AFTER = timedelta(weeks=8)


class OncallRotation(runner.Runner):

    def update_rotation(self, add: str | None, remove: str | None, dirty: bool,
                        substitute: str | None) -> None:
        start_date, _, rotation = rotation_from_yaml()
        if remove and remove not in rotation:
            raise ValueError(f"{remove} is already not in rotation.yaml")
        if add and add in rotation:
            raise ValueError(f"{add} is already in rotation.yaml")
        if add and add not in maintainers_from_reviewers():
            raise ValueError(
                f"Cannot add {add} - that name is not a maintainer in reviewers.yaml"
            )
        today = date.today()
        if add and remove:
            rotation = [add if v == remove else v for v in rotation]
            rotation_to_yaml(start_date, today, rotation)
            return
        current = who_would_be_oncall(today, start_date, rotation, [])
        start_of_this_week = today
        # Walk start_of_this_week back to a Sunday. We could do math for this but
        # up to 6 iterations is cheap and readable.
        while start_of_this_week.weekday() != 6:
            start_of_this_week -= timedelta(days=1)
        new_start_date = start_of_this_week
        if add:
            index = rotation.index(current)
            rotation.insert(index, add)
            # Walk new_start_date back a week at a time until the current oncall is also current
            # with the new rotation.
            while who_would_be_oncall(today, new_start_date, rotation,
                                      []) != current:
                new_start_date -= timedelta(weeks=1)
            rotation_to_yaml(new_start_date, today, rotation)
            return
        # remove
        new_rotation = [*rotation]
        new_rotation.remove(remove)
        if remove == current:
            disruption_week = 0
        else:
            # Walk new_start_date back a week at a time until the current oncall is also current
            # with the new rotation.
            while who_would_be_oncall(today, new_start_date, new_rotation,
                                      []) != current:
                new_start_date -= timedelta(weeks=1)
            disruption_week = 1
            while who_would_be_oncall(today + timedelta(weeks=disruption_week),
                                      start_date, rotation, []) != remove:
                disruption_week += 1

        if dirty or timedelta(
                weeks=disruption_week) >= CONSIDER_DIRTY_REMOVES_SAFE_AFTER:
            # It is okay to just remove despite disruption, because it's not going to
            # change anything too soon, or the user has selected just do it.
            rotation_to_yaml(new_start_date, today, new_rotation)
            return
        if not substitute:
            raise ValueError(
                f"Removing {remove} will disrupt schedules starting at {disruption_week} weeks away. Either add --dirty=True to signify this is okay, or add a --substitute for the first shift"
            )
        original_oncall_after_disruption_week = who_would_be_oncall(
            today + timedelta(weeks=disruption_week + 1), start_date, rotation,
            [])
        # Walk new_start_date back a week at a time until the cycle after the disruption point
        # is resynced.
        while who_would_be_oncall(today + timedelta(weeks=disruption_week + 1),
                                  new_start_date, new_rotation,
                                  []) != original_oncall_after_disruption_week:
            new_start_date -= timedelta(weeks=1)
        # Add overrides up to the disruption week.
        override_week = start_of_this_week
        new_overrides: list[Override] = []
        while override_week < start_of_this_week + timedelta(
                weeks=disruption_week - 1):
            new_overrides.append(
                Override(start=override_week,
                         duration_days=7,
                         oncall=who_would_be_oncall(override_week, start_date,
                                                    rotation, [])))
            override_week += timedelta(weeks=1)
        # Add the substitute override at the disruption point.
        new_overrides.append(
            Override(start=override_week, duration_days=7, oncall=substitute))
        old_overrides = overrides_from_yaml()
        for no in new_overrides:
            for oo in old_overrides:
                if oo.end > no.start and oo.start < no.end:
                    print(
                        f"Warning: generated override intersects with pre-existing override:\n{no}\n{oo}"
                    )
        overrides_to_yaml([*old_overrides, *new_overrides])
        rotation_to_yaml(new_start_date, today, new_rotation)

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
        if self.args.add or self.args.remove:
            self.update_rotation(self.args.add, self.args.remove,
                                 self.args.dirty, self.args.substitute)
            # Whenever updating, also remove overrides from the past.
            overrides_to_yaml(overrides_from_yaml())

        ical_path().write_text(gen_ical())


if __name__ == "__main__":
    sys.exit(OncallRotation(*sys.argv[1:])())
