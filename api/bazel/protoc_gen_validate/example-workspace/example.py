import sys

from google.protobuf import text_format
from foo import bar_pb2
from com_envoyproxy_protoc_gen_validate.python.protoc_gen_validate import validator
from typing import List


def main(filenames: List[str]) -> None:
    if not filenames:
        print("No inputs provided; exiting")

    success_count = 0
    for filename in filenames:
        bars = bar_pb2.Bars()
        fullname = bars.DESCRIPTOR.name
        try:
            with open(filename, 'r') as fh:
                text_format.Parse(fh.read(), bars)
        except IOError as error:
            msg = f"Failed to open file '{filename}': {error}"
            raise RuntimeError(msg) from error
        except text_format.ParseError as error:
            msg = f"Failed to parse file '{filename}' as a {fullname} textproto: {error}"
            raise RuntimeError(msg) from error

        try:
            validator.validate(bars)
        except validator.ValidationFailed as error:
            print(
                f"Failed to validate file '{filename}' as a {fullname} textproto: {error}",
                file=sys.stderr,
            )
        else:
            print(f"Successfully validated file '{filename}' as a {fullname} textproto")
            success_count += 1

    failure_count = len(filenames) - success_count
    if failure_count:
        s = "s" if failure_count > 1 else ""
        msg = f"Failed to validate {failure_count} file{s}"
        raise RuntimeError(msg)


if __name__ == "__main__":
    try:
        main(sys.argv[1:])
    except RuntimeError as error:
        print(error, file=sys.stderr)
        sys.exit(1)
