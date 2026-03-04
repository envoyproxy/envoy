from dataclasses import dataclass
from datetime import date, timedelta
from pathlib import Path
import os
import yaml


@dataclass
class Override:
    start: date
    duration_days: int
    oncall: str

    @property
    def end(self) -> date:
        return self.start + timedelta(days=self.duration_days)

    def as_dict(self) -> dict:
        return {
            "start": self.start,
            "duration_days": self.duration_days,
            "oncall": self.oncall
        }


def _envoy_path(path: str) -> Path:
    return Path(
        os.getenv('ENVOY_SRCDIR', Path(os.getenv('RUNFILES_DIR'), "envoy")),
        path)


def _reviewers_path():
    return _envoy_path("reviewers.yaml")


def _rotation_path():
    return _envoy_path("tools/oncall/rotation.yaml")


def _overrides_path():
    return _envoy_path("tools/oncall/overrides.yaml")


def ical_path():
    return _envoy_path("tools/oncall/rotation.ical")


def _yaml_from_file(path: str):
    with open(path, 'r') as file:
        return yaml.safe_load(file)


def maintainers_from_reviewers() -> set[str]:
    maintainers = []
    data = _yaml_from_file(_reviewers_path())
    for k, v in data.items():
        if v.get('maintainer', False):
            maintainers.append(k)
    return set(maintainers)


def _diff_rotation_and_maintainers(rotation: list[str]) -> str:
    m = maintainers_from_reviewers()
    r = set(rotation)
    if m == r:
        return ""
    ret = f"Mismatch between {_rotation_path()} and maintainers in {_reviewers_path()}:\n"
    if m - r:
        ret += f"  set of maintainers contains extra {m - r} - either remove maintainer: True from them in reviewers.yaml, or add them to rotation.yaml"
    if r - m:
        ret += f"  rotation contains extra {r - m} - either remove them from rotation.yaml or add them with maintainer: True to reviewers.yaml"
    return ret


def rotation_to_yaml(start_date: date, updated: date,
                     rotation: list[str]) -> None:
    with open(_rotation_path(), 'w') as file:
        file.write(
            "# In general, modify this through options to bazel run tools/oncall:rotation\n"
        )
        yaml.dump(
            {
                "start_date": start_date,
                "updated": updated,
                "rotation": rotation,
            },
            file,
            sort_keys=False)


def rotation_from_yaml() -> tuple[date, date, list[str]]:
    """Returns start_date, updated, rotation"""
    data = _yaml_from_file(_rotation_path())
    assert isinstance(data['start_date'], date)
    assert isinstance(data['updated'], date)
    assert isinstance(data['rotation'], list)
    diff = _diff_rotation_and_maintainers(data['rotation'])
    assert diff == "", diff
    return data['start_date'], data['updated'], data['rotation']


def overrides_to_yaml(overrides: list[Override]) -> None:
    with open(_overrides_path(), 'w') as file:
        file.write(
            """# This file can be modified manually to add or remove on-call overrides.
# After modifying, run
# bazel run //tools/oncall:rotation
# to update the ical file.
#
# Format: start
# overrides:
# - start: 2026-03-01
#   duration_days: 3
#   oncall: $USERNAME
""")
        # Forget any overrides more than 30 days in the past.
        # We keep a short amount in the past for people checking calendar history.
        # If you need older calendar history, you can check the git history of overrides.yaml!
        recency_threshold = date.today() - timedelta(days=30)
        filtered_overrides = filter(
            lambda o: o.start + timedelta(days=o.duration_days) >
            recency_threshold, overrides)
        dict_overrides = [o.as_dict() for o in filtered_overrides]
        yaml.dump({
            "overrides": dict_overrides,
        }, file, sort_keys=False)


def overrides_from_yaml() -> list[Override]:
    data = _yaml_from_file(_overrides_path())
    if not data:
        return []
    if 'overrides' not in data:
        return []
    overrides = data['overrides']
    assert isinstance(overrides, list)
    for override in overrides:
        assert 'start' in override, override
        assert 'duration_days' in override, override
        assert 'oncall' in override, override
        assert isinstance(override['start'], date), override
        assert isinstance(override['duration_days'], int), override
        assert isinstance(override['oncall'], str), override
    return [Override(**o) for o in overrides]


def who_would_be_oncall(d: date, start_date: date, rotation: list[str],
                        overrides: list[Override]) -> str:
    for override in overrides:
        if override.start <= d and override.end > d:
            return override.oncall
    seats = len(rotation)
    offset_days = (d - start_date).days
    offset_weeks = offset_days // 7
    seat = offset_weeks % seats
    return rotation[seat]


def who_is_oncall(d: date) -> str:
    start_date, _, rotation = rotation_from_yaml()
    overrides = overrides_from_yaml()
    return who_would_be_oncall(d, start_date, rotation, overrides)
