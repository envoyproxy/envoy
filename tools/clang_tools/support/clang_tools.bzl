def envoy_clang_tools_cc_binary(name, copts = [], tags = [], **kwargs):
    native.cc_binary(
        name = name,
        copts = copts + [
            "-fno-exceptions",
            "-fno-rtti",
        ],
        tags = tags + ["manual"],
        **kwargs
    )
