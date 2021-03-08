import os
import pathlib
import subprocess
import sys
import traceback

import yaml

from google.protobuf.json_format import ParseError

sys.path = [p for p in sys.path if not p.endswith('bazel_tools')]

from tools.config_validation.validate_fragment import ValidateFragment


def main():
  errors = []
  for arg in sys.argv[1:]:
    if not arg.endswith(".yaml"):
      raise SystemExit(f"ERROR: non yaml file {arg}")
    try:
      ValidateFragment("envoy.config.bootstrap.v3.Bootstrap",
                       yaml.safe_load(pathlib.Path(arg).read_text()))
    except (ParseError, KeyError) as e:
      errors.append(arg)
      print()
      print(f"ERROR (validation failed): {arg}")
      print(e)
      print()

  if errors:
    raise SystemExit(f"ERROR: some configuration files ({len(errors)}) failed to parse")


if __name__ == "__main__":
  main()
