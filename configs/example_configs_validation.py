import pathlib
import sys

import yaml

from google.protobuf.json_format import ParseError

sys.path = [p for p in sys.path if not p.endswith('bazel_tools')]

from tools.config_validation.validate_fragment import ValidateFragment


def main():
  errors = []
  for arg in sys.argv[1:]:
    try:
      ValidateFragment("envoy.config.bootstrap.v3.Bootstrap",
                       yaml.safe_load(pathlib.Path(arg).read_text()))
    except (ParseError, KeyError) as e:
      errors.append(arg)
      print(f"\nERROR (validation failed): {arg}\n{e}\n\n")

  if errors:
    raise SystemExit(f"ERROR: some configuration files ({len(errors)}) failed to validate")


if __name__ == "__main__":
  main()
