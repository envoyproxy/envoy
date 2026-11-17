"""RBE execution pool selection for Envoy targets."""

_ENGFLOW_RBE_X86_64 = Label("//bazel:engflow_rbe_x86_64")

# The configurations a pool can be chosen for, each paired with the setting that matches an
# EngFlow x86_64 build with it. The sanitizer and coverage names are those of the
# `//bazel:<name>_build` settings, i.e. of `--define ENVOY_CONFIG_<NAME>=1`, which every `--config`
# enabling that instrumentation sets (`asan`, `clang-asan` and `asan-fuzzer` are all "asan" here);
# "gcc" is a build with the gcc toolchain (`//bazel:gcc_build`), "cto" one with
# `--config=compile-time-options`, "openssl" one with `--config=openssl` and "fuzz_coverage" one
# with `--config=fuzz-coverage`. They are not CI job names. Listed in order of increasing
# precedence: each gets its own select() so that two that can be enabled together (eg openssl and
# asan, or fuzz coverage, which also turns on coverage and asan) never meet in one select(), and the
# later entry's pool wins.
_CONFIGS = [
    ("gcc", Label("//bazel:engflow_rbe_x86_64_gcc")),
    ("cto", Label("//bazel:engflow_rbe_x86_64_compile_time_options")),
    ("openssl", Label("//bazel:engflow_rbe_x86_64_openssl")),
    ("coverage", Label("//bazel:engflow_rbe_x86_64_coverage")),
    ("tsan", Label("//bazel:engflow_rbe_x86_64_tsan")),
    ("msan", Label("//bazel:engflow_rbe_x86_64_msan")),
    ("asan", Label("//bazel:engflow_rbe_x86_64_asan")),
    ("fuzz_coverage", Label("//bazel:engflow_rbe_x86_64_fuzz_coverage")),
]

# The pool an action goes to when nothing names one. EngFlow resolves the name "default" to the
# platform's default pool, so a spec that names a pool for only some of a target's actions can say
# explicitly that the others stay where they were.
PLATFORM_RBE_POOL = "default"

def _pool_spec(spec, config, test, link):
    """The exec properties for one configuration's pool spec: a name, or a build/link/test dict.

    Every key the macro can set is set, to the platform default where the spec names no pool for it:
    `Pool` (compile, and every other execution group), for a C++ rule `cpp_link.Pool` (its link)
    and, for a test, `test.Pool`. The selects for the target-wide spec and for the configuration's
    are unioned key by key, so a configuration's spec replaces the target-wide one only if it sets
    every key the target-wide one did.
    """
    if type(spec) == type(""):
        build = spec
        link_pool = spec
        test_pool = spec
    elif type(spec) != type({}):
        fail("rbe_pool: expected a pool name or a {build, link, test} dict for %r, got %r" % (config, spec))
    else:
        for key in spec:
            if key not in ("build", "link", "test"):
                fail("rbe_pool: unknown key %r for %r (expected build, link or test)" % (key, config))
        if "link" in spec and not link:
            fail("rbe_pool: a link pool for %r needs a C++ rule (with a `cpp_link` execution group)" % config)
        build = spec.get("build", PLATFORM_RBE_POOL)
        link_pool = spec.get("link", PLATFORM_RBE_POOL)
        test_pool = spec.get("test", PLATFORM_RBE_POOL)
    props = {"Pool": build}
    if link:
        props["cpp_link.Pool"] = link_pool
    if test:
        props["test.Pool"] = test_pool
    return props

def envoy_exec_properties(
        rbe_pool = None,
        exec_properties = {},
        test = True,
        link = False):
    """Returns `exec_properties` with the RBE pool set for EngFlow x86_64 builds.

    `rbe_pool` is either a pool spec, used for every build, or a dict mapping the build's
    configuration ("asan", "msan", "tsan", "coverage": the `//bazel:<name>_build` settings; "gcc":
    the gcc toolchain; "cto", "openssl", "fuzz_coverage": those `--config`s) or "default" (any other
    build) to a pool spec; a configuration the dict does not name uses its "default" entry, and with
    none leaves in place whatever another enabled configuration pinned (fuzz coverage is also
    coverage and asan).

    A pool spec is a pool name, which applies to every action of the target, as it always has, or a
    dict with a `build` pool for its compile actions, a `link` pool for its link action and/or a
    `test` pool for its test action. Each action the dict does not name stays on the platform
    default, so a target whose compiles and link both need a larger pool names both.

    `test` says whether the target has a test action (a `test` execution group). Without one, test
    pools are ignored. `link` says whether it is a C++ rule, whose link runs in a `cpp_link`
    execution group (`cc_binary`, `cc_library`, `cc_test`); only those can have a link pool, and
    any other rule (eg a genrule or `sh_test`) must not set `cpp_link` properties.

    Memory-heavy builds are the reason to pin a pool, so prefer naming the configuration and the
    actions that need it, e.g. `rbe_pool = {"msan": {"test": "linux_x64_large"}}` or
    `rbe_pool = {"gcc": {"build": "linux_x64_small", "link": "linux_x64_large"}}` or
    `rbe_pool = {"coverage": {"link": "linux_x64_large"}}`, over pinning everything.
    """
    configs = [c for c, _ in _CONFIGS]
    if rbe_pool == None:
        pools = {}
    elif type(rbe_pool) == type(""):
        pools = {"default": rbe_pool}
    elif type(rbe_pool) != type({}):
        fail("rbe_pool must be a pool spec or a dict of configuration to pool spec, got %r" % rbe_pool)
    elif [k for k in rbe_pool if k not in ("build", "link", "test")]:
        for config in rbe_pool:
            if config != "default" and config not in configs:
                fail("rbe_pool: unknown configuration %r (expected one of default, %s)" % (config, ", ".join(configs)))
        pools = dict(rbe_pool)
    else:
        # A bare {build, link, test} dict is a spec for every configuration.
        pools = {"default": rbe_pool}
    default = pools.get("default")
    props = exec_properties
    if default != None:
        props = props | select({
            _ENGFLOW_RBE_X86_64: _pool_spec(default, "default", test, link),
            "//conditions:default": {},
        })
    for config, setting in _CONFIGS:
        # The configuration's effective pool: its own entry, else the target-wide one. Emitted even
        # when it equals the target-wide pool, so that where two configurations are enabled at once
        # the later one in _CONFIGS wins regardless of which entries were spelled out. An empty dict
        # is a spec too: every action of that configuration on the platform default.
        pool = pools.get(config, default)
        if pool != None:
            props = props | select({setting: _pool_spec(pool, config, test, link), "//conditions:default": {}})
    return props

def _without_link(spec):
    if type(spec) != type({}):
        return spec
    return {k: v for k, v in spec.items() if k != "link"}

def envoy_rbe_pool_without_link(rbe_pool):
    """`rbe_pool` with its link pools dropped, for a non-C++ rule given a C++ target's pins."""
    if type(rbe_pool) != type({}):
        return rbe_pool
    if not [k for k in rbe_pool if k not in ("build", "link", "test")]:
        return _without_link(rbe_pool)
    return {config: _without_link(spec) for config, spec in rbe_pool.items()}
