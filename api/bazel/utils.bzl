load("@bazel_skylib//rules:write_file.bzl", "write_file")

def json_data(
        name,
        data,
        visibility = ["//visibility:public"],
        **kwargs):
    """Write a bazel object to a file

    The provided `data` object should be json serializable.
    """
    write_file(
        name = name,
        out = "%s.json" % name,
        content = json.encode(data).split("\n"),
        visibility = visibility,
        **kwargs
    )
