# Oncall rotation

This folder is for generating the oncall rotation.

`overrides.yaml` can be updated manually, after which you should run
```
bazel run //tools/oncall:rotation
```
to generate the new ical.

`rotation.yaml` can also potentially be updated manually, but it is recommended
to use one of
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
