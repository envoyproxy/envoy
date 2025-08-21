"""
Propagate the generated Swift header from a swift_library target
This exists to work around https://github.com/bazelbuild/rules_swift/issues/291
"""

def _swift_header_collector(ctx):
    headers = [
        DefaultInfo(
            files = ctx.attr.library[CcInfo].compilation_context.headers,
        ),
    ]

    if len(headers[0].files.to_list()) != 1:
        header_names = [header.basename for header in headers[0].files.to_list()]
        fail("Expected exactly 1 '-Swift.h' header, got {}".format(header_names))

    return headers

swift_header_collector = rule(
    attrs = dict(
        library = attr.label(
            mandatory = True,
            providers = [CcInfo],
        ),
    ),
    implementation = _swift_header_collector,
)
