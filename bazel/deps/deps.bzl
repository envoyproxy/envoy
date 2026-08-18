# Envoy centralizes external dependency declarations here so each dep is declared once and exposed
# as a label_flag. This gives us three properties in one place:
#   1. Build-time visibility enforcement: consumers depend on the flag target, so the flag's
#      visibility is the allowlist enforcement point.
#   2. Injection / forwarding: the label is resolved in Envoy's repository context and then handed
#      to consumers, including third-party repos that could not otherwise name the dep directly
#      under bzlmod module isolation.
#   3. Downstream override: users can repoint any dep without patching Envoy, e.g.
#      --@envoy//bazel/deps:foobar=@foobaz//:whatever.
#
# label_flag.build_setting_default cannot itself take a select(), so config-dependent defaults are
# wrapped in a private alias and the flag points at that alias.

_PRIVATE_VISIBILITY = ["//visibility:private"]


def _fail_invalid_actual(name, actual):
    fail(
        (
            "envoy_dep(%r): expected 'actual' to be a label string or a dict mapping condition "
            + "labels to label strings, got %s"
        ) % (name, type(actual))
    )


def _validate_actual(name, actual):
    if type(actual) == "string":
        return
    if type(actual) != "dict":
        _fail_invalid_actual(name, actual)
    if not actual:
        fail("envoy_dep(%r): 'actual' dict must not be empty" % name)
    for condition, label in actual.items():
        if type(condition) != "string" or type(label) != "string":
            _fail_invalid_actual(name, actual)


def _emit_envoy_dep(name, actual, visibility):
    _validate_actual(name, actual)

    build_setting_default = actual
    if type(actual) == "dict":
        default_name = "_%s_default" % name
        native.alias(
            name = default_name,
            actual = select(actual),
            visibility = _PRIVATE_VISIBILITY,
        )
        build_setting_default = ":%s" % default_name

    native.label_flag(
        name = name,
        build_setting_default = build_setting_default,
        visibility = visibility,
    )


def envoy_dep(name, actual, visibility = None, aliases = None):
    """Declares one or more centralized Envoy dependency label_flags.

    Args:
      name: Name of the primary label_flag target to emit.
      actual: Label string, or a dict mapping config_setting labels to label strings.
      visibility: Visibility list for the emitted label_flag targets. Defaults to private.
      aliases: Optional dict mapping additional target names to their label string or
        dict-of-condition defaults. Each alias emits its own label_flag with the same visibility.
    """
    if visibility == None:
        visibility = _PRIVATE_VISIBILITY

    _emit_envoy_dep(name, actual, visibility)

    if aliases == None:
        return
    if type(aliases) != "dict":
        fail("envoy_dep(%r): expected 'aliases' to be a dict of target names to labels" % name)

    for alias_name, alias_actual in aliases.items():
        if type(alias_name) != "string":
            fail("envoy_dep(%r): alias names must be strings, got %s" % (name, type(alias_name)))
        if alias_name == name:
            fail("envoy_dep(%r): alias name %r duplicates the primary target name" % (name, alias_name))
        _emit_envoy_dep(alias_name, alias_actual, visibility)
