def _genrule_repository(ctx):
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

    # https://github.com/bazelbuild/bazel/issues/3766
    genrule_cmd_file = Label("@envoy//bazel").relative(str(ctx.attr.genrule_cmd_file))
    ctx.symlink(genrule_cmd_file, "_envoy_genrule_cmd.genrule_cmd")
    cat_genrule_cmd = ctx.execute(["cat", "_envoy_genrule_cmd.genrule_cmd"])
    if cat_genrule_cmd.return_code != 0:
        fail("Failed to read genrule command %r: %s" % (
            genrule_cmd_file, cat_genrule_cmd.stderr))

    ctx.file("WORKSPACE", "workspace(name=%r)" % (ctx.name,))
    ctx.symlink(ctx.attr.build_file, "BUILD.bazel")

    # Inject the genrule_cmd content into a .bzl file that can be loaded
    # from the repository BUILD file. We force the user to look up the
    # command content "by label" so the inclusion source is obvious.
    ctx.file("genrule_cmd.bzl", """
_GENRULE_CMD = {%r: %r}
def genrule_cmd(label):
    return _GENRULE_CMD[label]
""" % (str(genrule_cmd_file), cat_genrule_cmd.stdout))

genrule_repository = repository_rule(
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
        "genrule_cmd_file": attr.label(
            mandatory = True,
            allow_single_file = [".genrule_cmd"],
        ),
        "build_file": attr.label(
            mandatory = True,
            allow_single_file = [".BUILD"],
        ),
    },
    implementation = _genrule_repository,
)

def _genrule_cc_deps(ctx):
  outs = depset()
  for dep in ctx.attr.deps:
    outs = dep.cc.transitive_headers + dep.cc.libs + outs
  return DefaultInfo(files=outs)

genrule_cc_deps = rule(
    attrs = {
        "deps": attr.label_list(
            providers = [],  # CcSkylarkApiProvider
            mandatory = True,
            allow_empty = False,
        ),
    },
    implementation = _genrule_cc_deps,
)

def _genrule_environment(ctx):
  lines = []

  # Bare minimum cflags to get included test binaries to link.
  #
  # See //tools:bazel.rc for the full set.
  asan_flags = ["-fsanitize=address,undefined"]
  tsan_flags = ["-fsanitize=thread"]

  cc_flags = []
  ld_flags = []
  ld_libs = []
  if ctx.var.get('ENVOY_CONFIG_COVERAGE'):
    ld_libs += ["-lgcov"]
  if ctx.var.get('ENVOY_CONFIG_ASAN'):
    cc_flags += asan_flags
    ld_flags += asan_flags
  if ctx.var.get('ENVOY_CONFIG_TSAN'):
    cc_flags += tsan_flags
    ld_flags += tsan_flags

  lines.append("export CFLAGS=%r" % (" ".join(cc_flags),))
  lines.append("export LDFLAGS=%r" % (" ".join(ld_flags),))
  lines.append("export LIBS=%r" % (" ".join(ld_libs),))
  lines.append("export CC=%r" % (ctx.var['CC'],))
  lines.append("export CXX=%r" % (ctx.var['CC'],))

  # Some Autoconf helper binaries leak, which makes ./configure think the
  # system is unable to do anything. Turn off leak checking during part of
  # the build.
  lines.append("export ASAN_OPTIONS=detect_leaks=0")

  lines.append("")
  out = ctx.new_file(ctx.attr.name + ".sh")
  ctx.file_action(out, "\n".join(lines))
  return DefaultInfo(files=depset([out]))

genrule_environment = rule(
    implementation = _genrule_environment,
)
