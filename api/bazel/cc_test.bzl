load("@rules_cc//cc:defs.bzl", "cc_test")

def api_cc_test(name, **kwargs):
    cc_test(
        name = name,
        **kwargs
    )
