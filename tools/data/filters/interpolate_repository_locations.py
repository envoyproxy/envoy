def format_data(s, data):
    return s.format(
        version=data["version"],
        underscore_version=data["version"].replace(".", "_"),
        dash_version=data["version"].replace(".", "-"))


def main(data):
    for k, v in data.items():
        # this should reflect any transformations in `api/bazel/repository_locations_utils.bzl`
        if not v.get("version"):
            data[k] = v
            continue
        v["strip_prefix"] = format_data(v.get("strip_prefix", ""), v)
        v["urls"] = [format_data(url, v) for url in v.get("urls", [])]
        data[k] = v
    return data
