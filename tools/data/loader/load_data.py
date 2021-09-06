import os
import pathlib
from rules_python.python.runfiles import runfiles

__IMPORTS__  # noqa: F821

_loader = __LOADER__  # noqa: F821


def _resolve(provided_path):
    # Resolve the path, to the data file
    # Adapts the filepath to work in different invokations
    #   ie - run, build, genrules, etc
    path = pathlib.Path(provided_path)
    if path.exists():
        return path
    run = runfiles.Create()
    location = run.Rlocation(str(path).strip(".").strip("/"))
    if location:
        path = pathlib.Path(location)
        if not path.exists():
            # If the build is invoked from the envoy workspace it has no prefix,
            # so search in the runfiles with `envoy/` prefix to path.
            path = pathlib.Path(run.Rlocation(os.path.join("envoy", provided_path)))
        return path
    raise Exception(f"Unable to find data file {provided_path}")


data = _loader(_resolve("__DATA_FILE__").read_text())

_filters = __FILTERS__  # noqa: F821

_filters = (_filters,) if not isinstance(_filters, tuple) else _filters

for _filter in _filters:
    data = _filter(data)

__all__ = ("data",)
