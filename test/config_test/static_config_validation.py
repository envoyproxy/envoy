import argparse

import pathlib
import sys

from yaml.scanner import ScannerError

from google.protobuf.json_format import ParseError

from envoy.base.utils import ProtobufValidator

# TODO (phlax): move this to `envoy.code.check`


def main():
    errors = []
    parser = argparse.ArgumentParser()
    parser.add_argument('paths', nargs="+")
    parser.add_argument('--descriptor_path')
    parsed = parser.parse_args(sys.argv[1:])
    protobuf = ProtobufValidator(parsed.descriptor_path)

    for example in parsed.paths:
        try:
            protobuf.validate_yaml(pathlib.Path(example).read_text())
        except (ParseError, KeyError, ScannerError) as e:
            errors.append(example)
            print(f"\nERROR (validation failed): {example}\n{e}\n\n")

    if errors:
        raise SystemExit(f"ERROR: some configuration files ({len(errors)}) failed to validate")


if __name__ == "__main__":
    main()
