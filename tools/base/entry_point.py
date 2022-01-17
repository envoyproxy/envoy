import pathlib
import subprocess
import sys

ENTRY_POINT_ALIAS = "_ENTRY_POINT_ALIAS_"


def find_tool_path():
    entry_point_alias = f"external/{ENTRY_POINT_ALIAS.split('/external/')[1]}"
    if pathlib.Path(entry_point_alias).exists():
        return entry_point_alias
    for x in pathlib.Path(".").glob(f"**/{entry_point_alias}"):
        return x


def main(*args) -> int:
    tool_path = find_tool_path()

    if not tool_path:
        print(f"Unable to locate tool: {ENTRY_POINT_ALIAS}")
        return 1

    try:
        return subprocess.run([tool_path, *args]).returncode
    except KeyboardInterrupt:
        return 1


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
