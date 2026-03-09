from dataclasses import dataclass
from datetime import date, timedelta
from pathlib import Path
from typing import Self
import yaml

from tools.oncall.gen_ical_lib import ical_file_format, IcalEvent, IcalOverrideEvent, IcalRecurringEvent

_YAML_HEADER_COMMENT = "# In general, modify this through options to bazel run tools/oncall:rotation\n"
_YAML_OVERRIDES_COMMENT = """
# You can modify overrides manually to add or remove on-call overrides.
# After modifying, run
# bazel run //tools/oncall:rotation
# to update the ical file.
#
# Format of overrides:
# overrides:
# - start: 2026-03-01
#   duration_days: 3
#   oncall: $USERNAME
#   reason: optional reason for the override
"""


def _recent_sunday(day: date) -> date:
    """Walk start_of_this_week back to a Sunday. We could do math for this but
    up to 6 iterations is cheap and readable."""
    while day.weekday() != 6:
        day -= timedelta(days=1)
    return day


@dataclass
class Override:
    start_date: date
    duration_days: int
    oncall: str
    reason: str | None

    @property
    def end_date(self) -> date:
        return self.start_date + timedelta(days=self.duration_days)

    def as_dict(self) -> dict:
        return {
            "start_date": self.start_date,
            "duration_days": self.duration_days,
            "oncall": self.oncall,
        } | {
            "reason": self.reason,
        } if self.reason else {}

    def intersects_with(self, other: Self) -> bool:
        return self.start_date > other.end_date and self.end_date < other.start_date

    @property
    def days(self) -> list[date]:
        return [self.start_date + timedelta(days=day) for day in range(self.duration_days)]

    @classmethod
    def from_dict(cls, yaml: dict) -> Self:
        assert isinstance(yaml, dict), yaml
        assert isinstance(yaml['start_date'], date), yaml
        assert isinstance(yaml['duration_days'], int), yaml
        assert isinstance(yaml['oncall'], str), yaml
        if 'reason' in yaml:
            assert isinstance(yaml['reason'], str), yaml
        return Override(start_date=yaml['start_date'],
                        duration_days=yaml['duration_days'],
                        oncall=yaml['oncall'],
                        reason=yaml['reason'] if 'reason' in yaml else None)


class OncallCalendar:

    def __init__(self,
                 start_date: date | None = None,
                 updated: date | None = None,
                 rotation: list[str] | None = None,
                 overrides: list[Override] | None = None):
        self.start_date = start_date or date.today()
        self.updated = updated or date.today()
        self.rotation = rotation or []
        self.overrides = overrides or []

    @classmethod
    def load(cls, path: Path) -> Self:
        with open(path, 'r') as file:
            data = yaml.safe_load(file)
        assert isinstance(data, dict)
        assert isinstance(data['start_date'], date)
        assert isinstance(data['updated'], date)
        assert isinstance(data['rotation'], list)
        assert all(isinstance(name, str) for name in data['rotation'])
        assert 'overrides' not in data or isinstance(data['overrides'], list)
        return OncallCalendar(start_date=data['start_date'],
                              updated=data['updated'],
                              rotation=data['rotation'],
                              overrides=[Override.from_dict(o) for o in data['overrides']] if 'overrides' in data else [],)

    def save(self, path: Path) -> None:
        # Dump in memory first so if there's any errors during serialization
        # we don't accidentally partially overwrite the file.
        rotation_yaml = yaml.dump(
            {
                "start_date": self.start_date,
                "updated": self.updated,
                "rotation": self.rotation,
            }, sort_keys=False)
        overrides_yaml = yaml.dump({
            "overrides": [o.as_dict() for o in self.overrides],
        }) if self.overrides else ""
        with open(path, 'w') as file:
            file.write(_YAML_HEADER_COMMENT)
            file.write(rotation_yaml)
            file.write(_YAML_OVERRIDES_COMMENT)
            file.write(overrides_yaml)

    def remove_outdated_overrides(self) -> None:
        today = date.today()
        self.overrides = [o for o in self.overrides if o.end_date >= today]

    def replace(self, old: str, new: str) -> None:
        if old not in self.rotation:
            raise ValueError(f"{old} is not in the rotation")
        if new in self.rotation:
            raise ValueError(f"{new} is already in the rotation")
        self.rotation = [new if v == old else v for v in self.rotation]

    def add(self, new: str) -> None:
        if new in self.rotation:
            raise ValueError(f"{new} is already in the rotation")
        today = date.today()
        current = self.who_is_oncall(today, ignore_overrides=True)
        index = self.rotation.index(current)
        new_rotation = [*self.rotation]
        new_rotation.insert(index, new)
        new_calendar = OncallCalendar(start_date=_recent_sunday(today),
                                      updated=today,
                                      rotation=new_rotation)
        # Walk the new start_date back a week at a time until the current oncall is also current
        # with the new rotation.
        while new_calendar.who_is_oncall(today,
                                         ignore_overrides=True) != current:
            new_calendar.start_date -= timedelta(weeks=1)
        self.start_date = new_calendar.start_date
        self.updated = today
        self.rotation = new_calendar.rotation

    def remove(self, remove: str, substitute: str,
               allow_disrupt_after: timedelta) -> None:
        if remove not in self.rotation:
            raise ValueError(f"{remove} is not in the rotation")
        today = date.today()
        current = self.who_is_oncall(today)
        new_rotation = [*self.rotation]
        new_rotation.remove(remove)
        new_calendar = OncallCalendar(start_date=_recent_sunday(today),
                                      updated=today,
                                      rotation=new_rotation)
        if remove == current:
            disruption_week = 0
        else:
            # Walk the new start_date back a week at a time until the current oncall is also current
            # with the new rotation.
            while new_calendar.who_is_oncall(today,
                                             ignore_overrides=True) != current:
                new_calendar.start_date -= timedelta(weeks=1)
            disruption_week = 1
            while self.who_is_oncall(today + timedelta(weeks=disruption_week),
                                     ignore_overrides=True) != remove:
                disruption_week += 1

        if timedelta(weeks=disruption_week) >= allow_disrupt_after:
            # It is okay to remove with no overrides, because it's not going to
            # change anything too soon, or the user has selected just do it.
            self.start_date = new_calendar.start_date
            self.updated = today
            self.rotation = new_calendar.rotation
            return

        if not substitute:
            raise ValueError(
                f"Removing {remove} will disrupt schedules starting at {disruption_week} weeks away. Either add --dirty=True to signify this is okay, or add a --substitute for the first shift"
            )

        after_disruption_week = today + timedelta(weeks=disruption_week + 1)
        original_oncall_after_disruption_week = self.who_is_oncall(
            after_disruption_week, ignore_overrides=True)
        # Walk new start_date back a week at a time until the cycle after the disruption point
        # is resynced.
        while new_calendar.who_is_oncall(
                after_disruption_week, ignore_overrides=True
        ) != original_oncall_after_disruption_week:
            new_calendar.start_date -= timedelta(weeks=1)
        # Then add overrides up to that week to keep the cycle unchanged until then.
        start_of_this_week = _recent_sunday(today)
        override_week = start_of_this_week
        while override_week < start_of_this_week + timedelta(
                weeks=disruption_week - 1):
            new_override = Override(start_date=override_week,
                                    duration_days=7,
                                    oncall=self.who_is_oncall(override_week),
                                    reason=f"syncing for removal of {remove}")
            if any(
                    old_override.intersects_with(new_override)
                    for old_override in self.overrides):
                new_override.reason = "syncing for removal of {remove} - RESOLVE CONFLICT BEFORE MERGING"

            new_calendar.overrides.append(new_override)
            override_week += timedelta(weeks=1)
        new_calendar.overrides.append(
            Override(
                start_date=override_week,
                duration_days=7,
                oncall=substitute,
                reason=f"volunteered to substitute for removal of {remove}"))
        self.start_date = new_calendar.start_date
        self.updated = today
        self.rotation = new_calendar.rotation
        self.overrides.extend(new_calendar.overrides)

    @property
    def all_override_days(self) -> list[date]:
        days: list[date] = []
        for override in self.overrides:
            days += override.days
        return days

    @property
    def as_ical_file(self) -> str:
        return ical_file_format(self.ical_events)

    @property
    def ical_events(self) -> list[IcalEvent]:
        events: list[IcalEvent] = []
        today = date.today()
        for i, oncall in enumerate(self.rotation):
            events.append(
                IcalRecurringEvent(updated=today,
                                   start_date=self.start_date +
                                   timedelta(weeks=i),
                                   uid=f"envoy-oncall-{oncall}",
                                   summary=f"Envoy oncall ({oncall})",
                                   every_n_weeks=len(self.rotation),
                                   exclude_days=self.all_override_days))
        for override in self.overrides:
            events.append(
                IcalOverrideEvent(updated=today,
                                  start_date=override.start_date,
                                  uid=f"envoy-oncall-override-{override.oncall}-{override.start_date.strftime("%Y%m%d")}",
                                  summary=f"Envoy oncall ({override.oncall})",
                                  duration_days=override.duration_days))
        return events

    def who_is_oncall(self, d: date, ignore_overrides: bool = False) -> str:
        if not ignore_overrides:
            for override in self.overrides:
                if override.start_date <= d and override.end_date > d:
                    return override.oncall
        seats = len(self.rotation)
        offset_days = (d - self.start_date).days
        offset_weeks = offset_days // 7
        seat = offset_weeks % seats
        return self.rotation[seat]
