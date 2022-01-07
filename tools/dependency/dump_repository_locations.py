import json
import pathlib
import sys


def _format_version(s, version):
    return s.format(
        version=version,
        dash_version=version.replace(".", "-"),
        underscore_version=version.replace(".", "_"))


def main(*repo_location_files):
    locations = {}

    for repo_file in repo_location_files:
        repo_locations = json.loads(pathlib.Path(repo_file).read_text())
        for location in repo_locations.values():
            if "version" in location:
                if "strip_prefix" in location:
                    location["strip_prefix"] = _format_version(
                        location["strip_prefix"], location["version"])
                location["urls"] = [
                    _format_version(url, location["version"]) for url in location["urls"]
                ]
        locations.update(repo_locations)
    print(json.dumps(locations))


if __name__ == "__main__":
    main(*sys.argv[1:])
