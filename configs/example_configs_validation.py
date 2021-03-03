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
  parse_errors = []
  other_errors = []
  for arg in sys.argv[1:]:
    if not arg.endswith(".yaml"):
      continue
    try:
      ValidateFragment("envoy.config.bootstrap.v3.Bootstrap",
                       yaml.safe_load(pathlib.Path(arg).read_text()))
    except ParseError as e:
      parse_errors.append(arg)
      print()
      print(f"ERROR (parse failed): {arg}")
      print(e)
      print()
    except Exception as e:
      other_errors.append(arg)
      print()
      print(f"ERROR ({e.__class__.__name__}): {arg}")
      print(e)
      traceback.print_exc()
      print()

  if parse_errors:
    print(f"ERROR: some configuration files ({len(parse_errors)}) failed to parse")

  if other_errors:
    print(f"ERROR: some configuration files ({len(other_errors)}) contained other errors")

  if parse_errors or other_errors:
    raise SystemExit("ERROR: some configuration files are invalid")


if __name__ == "__main__":
  main()
