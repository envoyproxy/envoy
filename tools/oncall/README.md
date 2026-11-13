# Oncall rotation

This folder is for generating the oncall rotation.

`overrides` in `rotation.yaml` can be updated manually, after which you should run
```
bazel run //tools/oncall:rotation
```
to generate the new ical.

The rest of `rotation.yaml` can also potentially be updated manually, but it
is recommended to use one of
```
# Simple add - will put the new oncall at the end of the current cycle to
# minimize disruption.
bazel run //tools/oncall:rotation -- --add=$NEW_ONCALL

# Simple remove - will give further instructions if the removal would
# cause imminent schedule disruption.
bazel run //tools/oncall:rotation -- --remove=$DEPRECATED_ONCALL

# Replacement - will put the new oncall in place of the old oncall, so
# guaranteed no disruption.
bazel run //tools/oncall:rotation -- --add=$NEW_ONCALL --remove=$REPLACED_ONCALL
```

Remove actions may insert temporary overrides to minimize existing schedule
disruption.

Changes must be synchronized with `//reviewers.yaml` - every
maintainer in `reviewers.yaml` must be included in the rotation, and every
name in `rotation.yaml` should be a maintainer in `reviewers.yaml`.

To verify this, and that `rotation.ical` is synchronized with `rotation.yaml`,
you can run
```
bazel test //tools/oncall:oncall_rotation_test
```

## Implementation details

The goal of the update script is to avoid changing schedules within the next
8 weeks. When removing someone from the rotation, if their shift is within
the next 8 weeks, the `--substitute` option can be used to replace that person's
next shift with a volunteer, leaving the rest of the schedule unchanged until
the next time that oncall's shift would have been. Alternatively, the `--dirty`
option can be used, which will allow disruption of the schedule from the very
next shift.

If the next shift of a removed oncall is more than 8 weeks out, they can be
just removed directly, and the schedule will be changed from that shift on.

When adding an oncall, they are added at the end of the current cycle, e.g.
if Bob is the current oncall, the new oncall will be inserted before Bob's
*next* shift - so the calendar will remain unchanged for one full cycle.

In the ical format, repeated events have a "start date", and changing the
length of the cycle without disrupting the upcoming schedule requires either
moving that start date, or reordering the rotation list. To make changes more
readable in the PR, this is implemented as moving the start date.
