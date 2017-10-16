def _patched_http_archive(ctx):
    ctx.download_and_extract(
        ctx.attr.urls,
        "", # output
        ctx.attr.sha256,
        "", # type
        ctx.attr.strip_prefix,
    )
    for ii, patch in enumerate(ctx.attr.patches):
        patch_input = "patch-input-%d.patch" % (ii,)
        ctx.symlink(patch, patch_input)
        patch_result = ctx.execute(["patch", "-p0", "--input", patch_input])
        if patch_result.return_code != 0:
          fail("Failed to apply patch %r: %s" % (patch, patch_result.stderr))

patched_http_archive = repository_rule(
    attrs = {
        "urls": attr.string_list(
            mandatory = True,
            allow_empty = False,
        ),
        "sha256": attr.string(),
        "strip_prefix": attr.string(),
        "patches": attr.label_list(
            allow_files = [".patch"],
            allow_empty = True,
        ),
    },
    implementation = _patched_http_archive,
)
