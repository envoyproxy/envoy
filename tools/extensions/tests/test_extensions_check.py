import types
from unittest.mock import patch, PropertyMock, call

import pytest

from tools.extensions import extensions_check


def test_extensions_checker_constructor():
    checker = extensions_check.ExtensionsChecker()
    assert checker.checks == ("registered", "fuzzed", "metadata")
    assert checker.extension_categories == extensions_check.EXTENSION_CATEGORIES


def test_extensions_checker_all_extensions(patches):
    checker = extensions_check.ExtensionsChecker()
    patched = patches(
        ("ExtensionsChecker.configured_extensions",
         dict(new_callable=PropertyMock)),
        ("ExtensionsChecker.configured_contrib_extensions",
         dict(new_callable=PropertyMock)),
        prefix="tools.extensions.extensions_check")
    configured_exts = {f"EXT{i}": f"EXT_DATA{i}" for i in range(0, 5)}
    contrib_exts = {f"CONTRIB_EXT{i}": f"EXT_DATA{i}" for i in range(0, 3)}

    with patched as (m_exts, m_contrib):
        m_exts.return_value = configured_exts
        m_contrib.return_value = contrib_exts
        result = checker.all_extensions

    assert (
        result
        == (set(configured_exts.keys())
            | set(extensions_check.BUILTIN_EXTENSIONS)
            | set(contrib_exts.keys())))
    assert "all_extensions" in checker.__dict__


@pytest.mark.parametrize("exts", ["", "contrib"])
def test_extensions_checker_configured_extensions(patches, exts):
    checker = extensions_check.ExtensionsChecker()
    patched = patches(
        "json",
        "pathlib",
        ("ExtensionsChecker.args",
         dict(new_callable=PropertyMock)),
        prefix="tools.extensions.extensions_check")
    attr = (
        "configured_%s_extensions" % exts
        if exts
        else "configured_extensions")
    config = (
        "%s_build_config" % exts
        if exts
        else "build_config")

    with patched as (m_json, m_plib, m_args):
        assert (
            getattr(checker, attr)
            == m_json.loads.return_value)

    assert (
        m_json.loads.call_args
        == [(m_plib.Path.return_value.read_text.return_value, ), {}])
    assert (
        m_plib.Path.call_args
        == [(getattr(m_args.return_value, config), ), {}])
    assert (
        m_plib.Path.return_value.read_text.call_args
        == [(), {}])
    assert attr in checker.__dict__


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


@pytest.mark.parametrize("is_dict", [True, False])
def test_extensions_metadata(patches, is_dict):
    checker = extensions_check.ExtensionsChecker()
    patched = patches(
        "isinstance",
        "utils",
        prefix="tools.extensions.extensions_check")

    with patched as (m_inst, m_utils):
        m_inst.return_value = is_dict

        if is_dict:
            assert (
                checker.metadata
                == m_utils.from_yaml.return_value)
        else:
            with pytest.raises(extensions_check.ExtensionsConfigurationError) as e:
                checker.metadata

    assert (
        list(m_utils.from_yaml.call_args_list)
        == [call(extensions_check.METADATA_PATH), call(extensions_check.CONTRIB_METADATA_PATH)])

    if not is_dict:
        assert (
            e.value.args[0]
            == f'Unable to parse metadata: {extensions_check.METADATA_PATH} {extensions_check.CONTRIB_METADATA_PATH}')
        return
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
async def test_extensions_check_fuzzed(patches, robust):
    checker = extensions_check.ExtensionsChecker()
    patched = patches(
        ("ExtensionsChecker.robust_to_downstream_count", dict(new_callable=PropertyMock)),
        ("ExtensionsChecker.fuzzed_count", dict(new_callable=PropertyMock)),
        "ExtensionsChecker.error",
        prefix="tools.extensions.extensions_check")

    with patched as (m_robust, m_fuzzed, m_error):
        m_fuzzed.return_value = "FUZZED_COUNT"
        m_robust.return_value = robust
        assert not await checker.check_fuzzed()

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
async def test_extensions_check_metadata(patches, meta_errors):
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
        assert not await checker.check_metadata()

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
async def test_extensions_check_registered(patches, all_ext, metadata):
    checker = extensions_check.ExtensionsChecker()
    patched = patches(
        ("ExtensionsChecker.metadata", dict(new_callable=PropertyMock)),
        ("ExtensionsChecker.all_extensions", dict(new_callable=PropertyMock)),
        "ExtensionsChecker.error",
        prefix="tools.extensions.extensions_check")

    with patched as (m_meta, m_all, m_error):
        m_meta.return_value = {k: k for k in metadata}
        m_all.return_value = set(all_ext)
        assert not await checker.check_registered()

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
            == [f'Unknown extension category for EXTENSION: {cat}. '
                'Please add it to tools/extensions/extensions_check.py' for cat in wrong_cats])
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
        args=("ARGS", ))
