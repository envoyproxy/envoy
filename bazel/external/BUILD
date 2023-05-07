licenses(["notice"])  # Apache 2

exports_files(["boringssl_fips.genrule_cmd"])

genrule(
    name = "empty_cc",
    outs = ["empty.cc"],
    cmd = "touch \"$(@D)/empty.cc\"",
    visibility = ["//visibility:public"],
)
