import types
from unittest.mock import patch, PropertyMock

import pytest

from tools.extensions import extensions_check


def test_extensions_checker_constructor():
    checker = extensions_check.ExtensionsChecker()
    assert checker.checks == ("registered", "fuzzed", "metadata")
    assert checker.extension_categories == extensions_check.EXTENSION_CATEGORIES


def test_extensions_checker_all_extensions():
    checker = extensions_check.ExtensionsChecker()
    exts_mock = patch(
        "tools.extensions.extensions_check.ExtensionsChecker.configured_extensions",
        new_callable=PropertyMock)
    _configured = dict(foo="FOO", bar="BAR")

    with exts_mock as m_exts:
        m_exts.return_value = _configured
        result = checker.all_extensions

    assert (
        result
        == set(_configured.keys()) | set(extensions_check.BUILTIN_EXTENSIONS))
    assert "all_extensions" in checker.__dict__


def test_extensions_checker_configured_extensions(patches):
    checker = extensions_check.ExtensionsChecker()
    patched = patches(
        "spec_from_loader",
        "SourceFileLoader",
        "module_from_spec",
        prefix="tools.extensions.extensions_check")

    with patched as (m_spec, m_loader, m_module):
        assert (
            checker.configured_extensions
            == m_module.return_value.EXTENSIONS)

    assert (
        list(m_spec.call_args)
        == [('extensions_build_config', m_loader.return_value), {}])
    assert (
        list(m_loader.call_args)
        == [('extensions_build_config', extensions_check.BUILD_CONFIG_PATH), {}])
    assert (
        list(m_module.call_args)
        == [(m_spec.return_value,), {}])
    assert (
        list(m_spec.return_value.loader.exec_module.call_args)
        == [(m_module.return_value,), {}])
    assert "configured_extensions" in checker.__dict__


def test_extensions_fuzzed_count(patches):
    checker = extensions_check.ExtensionsChecker()
    patched = patches(
        "re",
        "pathlib",
        prefix="tools.extensions.extensions_check")

    with patched as (m_re, m_path):
        data = m_path.Path.return_value.read_text.return_value
        data.splitlines.return_value.__getitem__.return_value = ["FOO", "BAR", "BAZ"]
        m_re.findall.return_value = "X" * 23
        assert checker.fuzzed_count == 23

    assert (
        list(m_path.Path.call_args)
        == [(extensions_check.FUZZ_TEST_PATH,), {}])
    assert (
        list(m_path.Path.return_value.read_text.call_args)
        == [(), {}])
    assert (
        list(data.splitlines.call_args)
        == [(), {}])
    assert (
        list(data.splitlines.return_value.__getitem__.call_args)
        == [(slice(None, 50, None),), {}])
    assert (
        list(m_re.findall.call_args)
        == [(extensions_check.FILTER_NAMES_PATTERN, 'FOOBARBAZ'), {}])
    assert "fuzzed_count" not in checker.__dict__


def test_extensions_metadata(patches):
    checker = extensions_check.ExtensionsChecker()
    patched = patches(
        "utils",
        prefix="tools.extensions.extensions_check")

    with patched as (m_utils, ):
        assert (
            checker.metadata
            == m_utils.from_yaml.return_value)

    assert (
        list(m_utils.from_yaml.call_args)
        == [(extensions_check.METADATA_PATH,), {}])
    assert "metadata" in checker.__dict__


def test_extensions_robust_to_downstream_count():
    checker = extensions_check.ExtensionsChecker()

    metadata_mock = patch(
        "tools.extensions.extensions_check.ExtensionsChecker.metadata",
        new_callable=PropertyMock)

    metadata = dict(
        foo0=dict(security_posture="robust_to_untrusted_downstream"),
        bar0=dict(security_posture="robust_to_untrusted_downstream"),
        baz0=dict(security_posture="robust_to_untrusted_downstream"),
        foo1=dict(security_posture="NOT_robust_to_untrusted_downstream"),
        bar1=dict(security_posture="NOT_robust_to_untrusted_downstream"),
        baz1=dict(security_posture="NOT_robust_to_untrusted_downstream"),
        foo0network=dict(security_posture="robust_to_untrusted_downstream"),
        bar0network=dict(security_posture="robust_to_untrusted_downstream"),
        baz0network=dict(security_posture="robust_to_untrusted_downstream"),
        foo1network=dict(security_posture="NOT_robust_to_untrusted_downstream"),
        bar1network=dict(security_posture="NOT_robust_to_untrusted_downstream"),
        baz1network=dict(security_posture="NOT_robust_to_untrusted_downstream"))

    with metadata_mock as m_meta:
        m_meta.return_value = metadata
        assert checker.robust_to_downstream_count == 3

    assert "robust_to_downstream_count" not in checker.__dict__


@pytest.mark.parametrize("robust", ["FUZZED_COUNT", "NOT_FUZZED_COUNT"])
def test_extensions_check_fuzzed(patches, robust):
    checker = extensions_check.ExtensionsChecker()
    patched = patches(
        ("ExtensionsChecker.robust_to_downstream_count", dict(new_callable=PropertyMock)),
        ("ExtensionsChecker.fuzzed_count", dict(new_callable=PropertyMock)),
        "ExtensionsChecker.error",
        prefix="tools.extensions.extensions_check")

    with patched as (m_robust, m_fuzzed, m_error):
        m_fuzzed.return_value = "FUZZED_COUNT"
        m_robust.return_value = robust
        checker.check_fuzzed()

    ERR_MESSAGE = (
        "Check that all network filters robust against untrusted downstreams are fuzzed "
        f"by adding them to filterNames() in {extensions_check.FUZZ_TEST_PATH}")

    if robust != "FUZZED_COUNT":
        assert (
            list(m_error.call_args)
            == [('fuzzed', [ERR_MESSAGE]), {}])
    else:
        assert not m_error.called


@pytest.mark.parametrize(
    "meta_errors",
    [dict(foo=True, bar=False, baz=True),
     dict(foo=False, bar=False, baz=False)])
def test_extensions_check_metadata(patches, meta_errors):
    checker = extensions_check.ExtensionsChecker()
    patched = patches(
        ("ExtensionsChecker.metadata", dict(new_callable=PropertyMock)),
        "ExtensionsChecker._check_metadata",
        "ExtensionsChecker.error",
        prefix="tools.extensions.extensions_check")

    def _check(k):
        if meta_errors[k]:
            return k

    with patched as (m_meta, m_check, m_error):
        m_meta.return_value = meta_errors
        m_check.side_effect = _check
        checker.check_metadata()

    assert (
        list(list(c) for c in m_check.call_args_list)
        == [[('foo',), {}], [('bar',), {}], [('baz',), {}]])
    if True in meta_errors.values():
        assert (
            list(list(c) for c in m_error.call_args_list)
            == [[('metadata', 'foo'), {}], [('metadata', 'baz'), {}]])
    else:
        assert not m_error.called


@pytest.mark.parametrize(
    "all_ext",
    [("A", "B", "C", "D"),
     ("A", "B"),
     ("B", "C", "D")])
@pytest.mark.parametrize(
    "metadata",
    [("A", "B", "C", "D"),
     ("A", "B"),
     ("B", "C", "D")])
def test_extensions_registered(patches, all_ext, metadata):
    checker = extensions_check.ExtensionsChecker()
    patched = patches(
        ("ExtensionsChecker.metadata", dict(new_callable=PropertyMock)),
        ("ExtensionsChecker.all_extensions", dict(new_callable=PropertyMock)),
        "ExtensionsChecker.error",
        prefix="tools.extensions.extensions_check")

    with patched as (m_meta, m_all, m_error):
        m_meta.return_value = {k: k for k in metadata}
        m_all.return_value = set(all_ext)
        checker.check_registered()

    if set(all_ext) == set(metadata):
        assert not m_error.called
        return

    only_metadata = set(metadata) - set(all_ext)
    missing_metadata = set(all_ext) - set(metadata)
    errors = []

    for ext in only_metadata:
        errors.append(f"Metadata for unused extension found: {ext}")
    for ext in missing_metadata:
        errors.append(f"Metadata missing for extension: {ext}")

    assert (
        list(list(c) for c in m_error.call_args_list)
        == [[('registered', [error]), {}]
            for error
            in errors])


def test_extensions__check_metadata(patches):
    checker = extensions_check.ExtensionsChecker()
    patched = patches(
        "ExtensionsChecker._check_metadata_categories",
        "ExtensionsChecker._check_metadata_security_posture",
        "ExtensionsChecker._check_metadata_status",
        prefix="tools.extensions.extensions_check")

    with patched as (m_cat, m_sec, m_status):
        m_cat.return_value = ["CAT"]
        m_sec.return_value = ["SEC"]
        m_status.return_value = ["STATUS"]
        assert (
            checker._check_metadata("EXTENSION")
            == ['CAT', 'SEC', 'STATUS'])

    for _patch in (m_cat, m_sec, m_status):
        assert (
            list(_patch.call_args)
            == [('EXTENSION',), {}])


@pytest.mark.parametrize(
    "ext_cats",
    [(),
     ("A", "B", "C", "D"),
     ("A", "B"),
     ("B", "C", "D")])
@pytest.mark.parametrize(
    "all_cats",
    [("A", "B", "C", "D"),
     ("A", "B"),
     ("B", "C", "D")])
def test_extensions__check_metadata_categories(ext_cats, all_cats):
    checker = extensions_check.ExtensionsChecker()
    metadata_mock = patch(
        "tools.extensions.extensions_check.ExtensionsChecker.metadata",
        new_callable=PropertyMock)

    checker.extension_categories = all_cats

    with metadata_mock as m_meta:
        m_meta.return_value.__getitem__.return_value.get.return_value = ext_cats
        _result = checker._check_metadata_categories("EXTENSION")
        assert isinstance(_result, types.GeneratorType)
        result = list(_result)

    assert (
        list(m_meta.return_value.__getitem__.call_args)
        == [('EXTENSION',), {}])
    assert (
        list(m_meta.return_value.__getitem__.return_value.get.call_args)
        == [('categories', ()), {}])

    if not ext_cats:
        assert (
            result
            == ['Missing extension category for EXTENSION. '
                'Please make sure the target is an envoy_cc_extension and category is set'])
        return

    wrong_cats = [cat for cat in ext_cats if cat not in all_cats]
    if wrong_cats:
        assert (
            result
            == [f'Unknown extension category for EXTENSION: {cat}' for cat in wrong_cats])
        return

    assert result == []


@pytest.mark.parametrize("sec_posture", [None, "A", "Z"])
def test_extensions__check_metadata_security_posture(sec_posture):
    checker = extensions_check.ExtensionsChecker()
    metadata_mock = patch(
        "tools.extensions.extensions_check.ExtensionsChecker.metadata",
        new_callable=PropertyMock)

    checker.extension_security_postures = ["A", "B", "C"]

    with metadata_mock as m_meta:
        m_meta.return_value.__getitem__.return_value.__getitem__.return_value = sec_posture
        _result = checker._check_metadata_security_posture("EXTENSION")
        assert isinstance(_result, types.GeneratorType)
        result = list(_result)

    assert (
        list(m_meta.return_value.__getitem__.call_args)
        == [('EXTENSION',), {}])
    assert (
        list(m_meta.return_value.__getitem__.return_value.__getitem__.call_args)
        == [('security_posture',), {}])

    if not sec_posture:
        assert (
            result
            == [f"Missing security posture for EXTENSION. "
                "Please make sure the target is an envoy_cc_extension and security_posture is set"])
    elif sec_posture not in checker.extension_security_postures:
        assert (
            result
            == [f"Unknown security posture for EXTENSION: {sec_posture}"])
    else:
        assert result == []


@pytest.mark.parametrize("status", ["A", "Z"])
def test_extensions__check_metadata_status(status):
    checker = extensions_check.ExtensionsChecker()
    metadata_mock = patch(
        "tools.extensions.extensions_check.ExtensionsChecker.metadata",
        new_callable=PropertyMock)

    checker.extension_status_values = ["A", "B", "C"]

    with metadata_mock as m_meta:
        m_meta.return_value.__getitem__.return_value.__getitem__.return_value = status
        _result = checker._check_metadata_status("EXTENSION")
        assert isinstance(_result, types.GeneratorType)
        result = list(_result)

    assert (
        list(m_meta.return_value.__getitem__.call_args)
        == [('EXTENSION',), {}])
    assert (
        list(m_meta.return_value.__getitem__.return_value.__getitem__.call_args)
        == [('status',), {}])

    if status not in checker.extension_status_values:
        assert result == [f'Unknown status for EXTENSION: {status}']
    else:
        assert result == []


def test_extensions_checker_main(command_main):
    command_main(
        extensions_check.main,
        "tools.extensions.extensions_check.ExtensionsChecker",
        args=())
