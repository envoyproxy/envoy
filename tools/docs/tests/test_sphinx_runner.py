from unittest.mock import MagicMock, PropertyMock

import pytest

from tools.docs import sphinx_runner


def test_sphinx_runner_constructor():
    runner = sphinx_runner.SphinxRunner()
    assert runner.build_dir == "."
    runner._build_dir = "foo"
    assert runner.build_dir == "foo"
    assert runner._build_sha == "UNKNOWN"
    assert "blob_dir" not in runner.__dict__


@pytest.mark.parametrize("docs_tag", [None, "", "SOME_DOCS_TAG"])
def test_sphinx_runner_blob_sha(patches, docs_tag):
    runner = sphinx_runner.SphinxRunner()
    patched = patches(
        ("SphinxRunner.build_sha", dict(new_callable=PropertyMock)),
        ("SphinxRunner.docs_tag", dict(new_callable=PropertyMock)),
        prefix="tools.docs.sphinx_runner")

    with patched as (m_sha, m_tag):
        m_tag.return_value = docs_tag
        if docs_tag:
            assert runner.blob_sha == docs_tag
        else:
            assert runner.blob_sha == m_sha.return_value
    assert "blob_sha" not in runner.__dict__


@pytest.mark.parametrize("build_sha", [None, "", "SOME_BUILD_SHA"])
def test_sphinx_runner_build_sha(patches, build_sha):
    runner = sphinx_runner.SphinxRunner()
    patched = patches(
        ("SphinxRunner.args", dict(new_callable=PropertyMock)),
        prefix="tools.docs.sphinx_runner")

    with patched as (m_args, ):
        m_args.return_value.build_sha = build_sha
        if build_sha:
            assert runner.build_sha == build_sha
        else:
            assert runner.build_sha == "UNKNOWN"

    assert "build_sha" not in runner.__dict__


def test_sphinx_runner_colors(patches):
    runner = sphinx_runner.SphinxRunner()
    patched = patches(
        "Fore",
        prefix="tools.docs.sphinx_runner")

    with patched as (m_colors, ):
        assert (
            runner.colors
            == dict(
                chrome=m_colors.LIGHTYELLOW_EX,
                key=m_colors.LIGHTCYAN_EX,
                value=m_colors.LIGHTMAGENTA_EX))

    assert "colors" in runner.__dict__


def test_sphinx_runner_config_file(patches):
    runner = sphinx_runner.SphinxRunner()
    patched = patches(
        "utils",
        ("SphinxRunner.config_file_path", dict(new_callable=PropertyMock)),
        ("SphinxRunner.configs", dict(new_callable=PropertyMock)),
        prefix="tools.docs.sphinx_runner")

    with patched as (m_utils, m_fpath,  m_configs):
        assert (
            runner.config_file
            == m_utils.to_yaml.return_value)

    assert (
        list(m_utils.to_yaml.call_args)
        == [(m_configs.return_value, m_fpath.return_value), {}])
    assert "config_file" in runner.__dict__


def test_sphinx_runner_config_file_path(patches):
    runner = sphinx_runner.SphinxRunner()
    patched = patches(
        "os.path",
        ("SphinxRunner.build_dir", dict(new_callable=PropertyMock)),
        prefix="tools.docs.sphinx_runner")

    with patched as (m_path, m_build):
        assert runner.config_file_path == m_path.join.return_value

    assert (
        list(m_path.join.call_args)
        == [(m_build.return_value, 'build.yaml',), {}])
    assert "config_file_path" not in runner.__dict__


def test_sphinx_runner_configs(patches):
    runner = sphinx_runner.SphinxRunner()
    mapping = dict(
        version_string="version_string",
        release_level="release_level",
        blob_sha="blob_sha",
        version_number="version_number",
        docker_image_tag_name="docker_image_tag_name",
        validator_path="validator_path",
        descriptor_path="descriptor_path")

    patched = patches(
        *[f"SphinxRunner.{v}" for v in mapping.values()],
        prefix="tools.docs.sphinx_runner")

    with patched as _mocks:
        result = runner.configs

    _configs = {}
    for k, v in mapping.items():
        _configs[k] = _mocks[list(mapping.values()).index(v)]
    assert result == _configs
    assert "configs" in runner.__dict__


def test_sphinx_runner_descriptor_path(patches):
    runner = sphinx_runner.SphinxRunner()
    patched = patches(
        "os.path",
        ("SphinxRunner.args", dict(new_callable=PropertyMock)),
        prefix="tools.docs.sphinx_runner")

    with patched as (m_path, m_args):
        assert (
            runner.descriptor_path
            == m_path.abspath.return_value)

    assert (
        list(m_path.abspath.call_args)
        == [(m_args.return_value.descriptor_path,), {}])
    assert "descriptor_path" not in runner.__dict__


def test_sphinx_runner_docker_image_tag_name(patches):
    runner = sphinx_runner.SphinxRunner()
    patched = patches(
        "re",
        ("SphinxRunner.version_number", dict(new_callable=PropertyMock)),
        prefix="tools.docs.sphinx_runner")

    with patched as (m_re, m_version):
        assert (
            runner.docker_image_tag_name
            == m_re.sub.return_value)

    assert (
        list(m_re.sub.call_args)
        == [('([0-9]+\\.[0-9]+)\\.[0-9]+.*', 'v\\1-latest',
             m_version.return_value), {}])
    assert "docker_image_tag_name" not in runner.__dict__


def test_sphinx_runner_docs_tag(patches):
    runner = sphinx_runner.SphinxRunner()
    patched = patches(
        ("SphinxRunner.args", dict(new_callable=PropertyMock)),
        prefix="tools.docs.sphinx_runner")

    with patched as (m_args, ):
        assert runner.docs_tag == m_args.return_value.docs_tag

    assert "docs_tag" not in runner.__dict__


def test_sphinx_runner_html_dir(patches):
    runner = sphinx_runner.SphinxRunner()
    patched = patches(
        "os.path",
        ("SphinxRunner.build_dir", dict(new_callable=PropertyMock)),
        ("SphinxRunner.args", dict(new_callable=PropertyMock)),
        prefix="tools.docs.sphinx_runner")

    with patched as (m_path, m_build, m_args):
        assert runner.html_dir == m_path.join.return_value

    assert (
        list(m_path.join.call_args)
        == [(m_build.return_value, 'generated/html'), {}])

    assert "html_dir" in runner.__dict__


def test_sphinx_runner_output_filename(patches):
    runner = sphinx_runner.SphinxRunner()
    patched = patches(
        ("SphinxRunner.args", dict(new_callable=PropertyMock)),
        prefix="tools.docs.sphinx_runner")

    with patched as (m_args, ):
        assert runner.output_filename == m_args.return_value.output_filename
    assert "output_filename" not in runner.__dict__


@pytest.mark.parametrize("major", [2, 3, 4])
@pytest.mark.parametrize("minor", [5, 6, 7, 8, 9])
def test_sphinx_runner_py_compatible(patches, major, minor):
    runner = sphinx_runner.SphinxRunner()
    patched = patches(
        "bool",
        "sys",
        prefix="tools.docs.sphinx_runner")

    with patched as (m_bool, m_sys):
        m_sys.version_info.major = major
        m_sys.version_info.minor = minor
        assert runner.py_compatible == m_bool.return_value
    expected = (
        True
        if major == 3 and minor >= 8
        else False)
    assert (
        list(m_bool.call_args)
        == [(expected,), {}])
    assert "py_compatible" not in runner.__dict__


@pytest.mark.parametrize("docs_tag", [None, "", "SOME_DOCS_TAG"])
def test_sphinx_runner_release_level(patches, docs_tag):
    runner = sphinx_runner.SphinxRunner()
    patched = patches(
        ("SphinxRunner.docs_tag", dict(new_callable=PropertyMock)),
        prefix="tools.docs.sphinx_runner")

    with patched as (m_tag, ):
        m_tag.return_value = docs_tag
        if docs_tag:
            assert runner.release_level == "tagged"
        else:
            assert runner.release_level == "pre-release"
    assert "release_level" not in runner.__dict__


@pytest.mark.parametrize("rst_tar", [None, "", "SOME_DOCS_TAG"])
def test_sphinx_runner_rst_dir(patches, rst_tar):
    runner = sphinx_runner.SphinxRunner()
    patched = patches(
        "os.path",
        "tarfile",
        ("SphinxRunner.build_dir", dict(new_callable=PropertyMock)),
        ("SphinxRunner.rst_tar", dict(new_callable=PropertyMock)),
        prefix="tools.docs.sphinx_runner")

    with patched as (m_path, m_tar, m_dir, m_rst):
        m_rst.return_value = rst_tar
        assert runner.rst_dir == m_path.join.return_value

    assert (
        list(m_path.join.call_args)
        == [(m_dir.return_value, 'generated/rst'), {}])

    if rst_tar:
        assert (
            list(m_tar.open.call_args)
            == [(rst_tar,), {}])
        assert (
            list(m_tar.open.return_value.__enter__.return_value.extractall.call_args)
            == [(), {'path': m_path.join.return_value}])
    else:
        assert not m_tar.open.called
    assert "rst_dir" in runner.__dict__


def test_sphinx_runner_rst_tar(patches):
    runner = sphinx_runner.SphinxRunner()
    patched = patches(
        ("SphinxRunner.args", dict(new_callable=PropertyMock)),
        prefix="tools.docs.sphinx_runner")

    with patched as (m_args, ):
        assert runner.rst_tar == m_args.return_value.rst_tar

    assert "rst_tar" not in runner.__dict__


def test_sphinx_runner_sphinx_args(patches):
    runner = sphinx_runner.SphinxRunner()
    patched = patches(
        ("SphinxRunner.html_dir", dict(new_callable=PropertyMock)),
        ("SphinxRunner.rst_dir", dict(new_callable=PropertyMock)),
        prefix="tools.docs.sphinx_runner")

    with patched as (m_html, m_rst):
        assert (
            runner.sphinx_args
            == ['-W', '--keep-going', '--color', '-b', 'html',
                m_rst.return_value,
                m_html.return_value])

    assert "sphinx_args" not in runner.__dict__


def test_sphinx_runner_validator_path(patches):
    runner = sphinx_runner.SphinxRunner()
    patched = patches(
        "os.path",
        ("SphinxRunner.args", dict(new_callable=PropertyMock)),
        prefix="tools.docs.sphinx_runner")

    with patched as (m_path, m_args):
        assert (
            runner.validator_path
            == m_path.abspath.return_value)

    assert (
        list(m_path.abspath.call_args)
        == [(m_args.return_value.validator_path,), {}])
    assert "validator_path" not in runner.__dict__


def test_sphinx_runner_version_file(patches):
    runner = sphinx_runner.SphinxRunner()
    patched = patches(
        ("SphinxRunner.args", dict(new_callable=PropertyMock)),
        prefix="tools.docs.sphinx_runner")

    with patched as (m_args, ):
        assert runner.version_file == m_args.return_value.version_file

    assert "version_file" not in runner.__dict__


def test_sphinx_runner_version_number(patches):
    runner = sphinx_runner.SphinxRunner()
    patched = patches(
        "open",
        ("SphinxRunner.version_file", dict(new_callable=PropertyMock)),
        prefix="tools.docs.sphinx_runner")

    with patched as (m_open, m_file):
        assert (
            runner.version_number
            == m_open.return_value.__enter__.return_value.read.return_value.strip.return_value)

    assert (
        list(m_open.call_args)
        == [(m_file.return_value,), {}])
    assert (
        list(m_open.return_value.__enter__.return_value.read.call_args)
        == [(), {}])
    assert (
        list(m_open.return_value.__enter__.return_value.read.return_value.strip.call_args)
        == [(), {}])

    assert "version_number" in runner.__dict__


@pytest.mark.parametrize("docs_tag", [None, "", "SOME_DOCS_TAG"])
def test_sphinx_runner_version_string(patches, docs_tag):
    runner = sphinx_runner.SphinxRunner()
    patched = patches(
        ("SphinxRunner.docs_tag", dict(new_callable=PropertyMock)),
        ("SphinxRunner.build_sha", dict(new_callable=PropertyMock)),
        ("SphinxRunner.version_number", dict(new_callable=PropertyMock)),
        prefix="tools.docs.sphinx_runner")

    with patched as (m_tag, m_sha, m_version):
        m_tag.return_value = docs_tag
        if docs_tag:
            assert runner.version_string == f"tag-{docs_tag}"
        else:
            assert runner.version_string == f"{m_version.return_value}-{m_sha.return_value.__getitem__.return_value}"
            assert (
                list(m_sha.return_value.__getitem__.call_args)
                == [(slice(None, 6, None),), {}])

    assert "version_string" not in runner.__dict__


def test_sphinx_runner_add_arguments():
    runner = sphinx_runner.SphinxRunner()
    parser = MagicMock()
    runner.add_arguments(parser)
    assert (
        list(list(c) for c in parser.add_argument.call_args_list)
        == [[('--build_sha',), {}],
            [('--docs_tag',), {}],
            [('--version_file',), {}],
            [('--validator_path',), {}],
            [('--descriptor_path',), {}],
            [('rst_tar',), {}],
            [('output_filename',), {}]])


@pytest.mark.parametrize("fails", [True, False])
def test_sphinx_runner_build_html(patches, fails):
    runner = sphinx_runner.SphinxRunner()
    patched = patches(
        "sphinx_build",
        ("SphinxRunner.sphinx_args", dict(new_callable=PropertyMock)),
        prefix="tools.docs.sphinx_runner")

    with patched as (m_sphinx, m_args):
        m_sphinx.side_effect = lambda s: fails
        e = None
        if fails:
            with pytest.raises(sphinx_runner.SphinxBuildError) as e:
                runner.build_html()
        else:
            runner.build_html()

    assert (
        list(m_sphinx.call_args)
        == [(m_args.return_value,), {}])

    if fails:
        assert e.value.args == ('BUILD FAILED',)
    else:
        assert not e


def test_sphinx_runner_build_summary(patches):
    runner = sphinx_runner.SphinxRunner()
    patched = patches(
        "print",
        "SphinxRunner._color",
        ("SphinxRunner.configs", dict(new_callable=PropertyMock)),
        prefix="tools.docs.sphinx_runner")

    with patched as (m_print, m_color, m_configs):
        m_configs.return_value.items.return_value = (("a", "A"), ("b", "B"))
        runner.build_summary()

    assert (
        list(list(c) for c in m_print.call_args_list)
        == [[(), {}],
            [(m_color.return_value,), {}],
            [(m_color.return_value,), {}],
            [(f"{m_color.return_value} {m_color.return_value}: {m_color.return_value}",), {}],
            [(f"{m_color.return_value} {m_color.return_value}: {m_color.return_value}",), {}],
            [(m_color.return_value,), {}],
            [(m_color.return_value,), {}],
            [(), {}]])
    assert (
        list(list(c) for c in m_color.call_args_list)
        == [[('#### Sphinx build configs #####################',), {}],
            [('###',), {}],
            [('###',), {}],
            [('a', 'key'), {}],
            [('A', 'value'), {}],
            [('###',), {}],
            [('b', 'key'), {}],
            [('B', 'value'), {}],
            [('###',), {}],
            [('###############################################',), {}]])


@pytest.mark.parametrize("py_compat", [True, False])
@pytest.mark.parametrize("release_level", ["pre-release", "tagged"])
@pytest.mark.parametrize("version_number", ["1.17", "1.23", "1.43"])
@pytest.mark.parametrize("docs_tag", ["v1.17", "v1.23", "v1.73"])
@pytest.mark.parametrize("current", ["XXX v1.17 ZZZ", "AAA v1.23 VVV", "BBB v1.73 EEE"])
def test_sphinx_runner_check_env(patches, py_compat, release_level, version_number, docs_tag, current):
    runner = sphinx_runner.SphinxRunner()
    patched = patches(
        "open",
        "os.path",
        "platform",
        ("SphinxRunner.configs", dict(new_callable=PropertyMock)),
        ("SphinxRunner.version_number", dict(new_callable=PropertyMock)),
        ("SphinxRunner.docs_tag", dict(new_callable=PropertyMock)),
        ("SphinxRunner.py_compatible", dict(new_callable=PropertyMock)),
        ("SphinxRunner.rst_dir", dict(new_callable=PropertyMock)),
        prefix="tools.docs.sphinx_runner")

    fails = (
        not py_compat
        or (release_level == "tagged"
            and (f"v{version_number}" != docs_tag
                 or version_number not in current)))

    with patched as (m_open, m_path, m_platform, m_configs, m_version, m_tag, m_py, m_rst):
        m_py.return_value = py_compat
        m_configs.return_value.__getitem__.return_value = release_level
        m_version.return_value = version_number
        m_tag.return_value = docs_tag
        m_open.return_value.__enter__.return_value.read.return_value = current

        if fails:
            with pytest.raises(sphinx_runner.SphinxEnvError) as e:
                runner.check_env()
        else:
            runner.check_env()

    if not py_compat:
        assert (
            e.value.args
            == ("ERROR: python version must be >= 3.8, "
                f"you have {m_platform.python_version.return_value}", ))
        assert not m_open.called
        return

    if release_level != "tagged":
        assert not m_open.called
        return

    if f"v{version_number}" != docs_tag:
        assert not m_open.called
        assert (
            e.value.args
            == ("Given git tag does not match the VERSION file content:"
                f"{docs_tag} vs v{version_number}", ))
        return

    assert (
        list(m_open.call_args)
        == [(m_path.join.return_value,), {}])
    assert (
        list(m_path.join.call_args)
        == [(m_rst.return_value, "version_history/current.rst"), {}])
    assert (
        list(m_open.return_value.__enter__.return_value.read.call_args)
        == [(), {}])

    if version_number not in current:
        assert (
            e.value.args
            == (f"Git tag ({version_number}) not found in version_history/current.rst", ))


def test_sphinx_runner_create_tarball(patches):
    runner = sphinx_runner.SphinxRunner()
    patched = patches(
        "tarfile",
        ("SphinxRunner.output_filename", dict(new_callable=PropertyMock)),
        ("SphinxRunner.html_dir", dict(new_callable=PropertyMock)),
        prefix="tools.docs.sphinx_runner")

    with patched as (m_tar, m_out, m_html):
        runner.create_tarball()

    assert (
        list(m_tar.open.call_args)
        == [(m_out.return_value, 'w'), {}])
    assert (
        list(m_tar.open.return_value.__enter__.return_value.add.call_args)
        == [(m_html.return_value,), {'arcname': '.'}])


def test_sphinx_runner_run(patches):
    runner = sphinx_runner.SphinxRunner()
    patched = patches(
        "tempfile",
        "SphinxRunner._run",
        prefix="tools.docs.sphinx_runner")

    with patched as (m_tmp, m_run):
        assert runner.run() == m_run.return_value

    assert (
        list(m_run.call_args)
        == [(m_tmp.TemporaryDirectory.return_value.__enter__.return_value,), {}])


@pytest.mark.parametrize("color", [None, "COLOR"])
def test_sphinx_runner__color(patches, color):
    runner = sphinx_runner.SphinxRunner()
    patched = patches(
        "Style",
        ("SphinxRunner.colors", dict(new_callable=PropertyMock)),
        prefix="tools.docs.sphinx_runner")

    with patched as (m_style, m_colors):
        assert (
            runner._color("MSG", color)
            == f"{m_colors.return_value.__getitem__.return_value}MSG{m_style.RESET_ALL}")
    assert (
        list(m_colors.return_value.__getitem__.call_args)
        == [(color or "chrome",), {}])


@pytest.mark.parametrize("check_fails", [True, False])
@pytest.mark.parametrize("build_fails", [True, False])
def test_sphinx_runner__run(patches, check_fails, build_fails):
    runner = sphinx_runner.SphinxRunner()
    patched = patches(
        "print",
        "os",
        "SphinxRunner.build_summary",
        "SphinxRunner.check_env",
        "SphinxRunner.build_html",
        "SphinxRunner.create_tarball",
        ("SphinxRunner.config_file", dict(new_callable=PropertyMock)),
        prefix="tools.docs.sphinx_runner")

    def _raise(error):
        raise error

    with patched as (m_print, m_os, m_summary, m_check, m_build, m_create, m_config):
        if check_fails:
            _check_error = sphinx_runner.SphinxEnvError("CHECK FAILED")
            m_check.side_effect = lambda: _raise(_check_error)
        if build_fails:
            _build_error = sphinx_runner.SphinxBuildError("BUILD FAILED")
            m_build.side_effect = lambda: _raise(_build_error)
        assert runner._run("BUILD_DIR") == (1 if (check_fails or build_fails) else None)

    assert (
        runner._build_dir
        == "BUILD_DIR")
    assert (
        list(m_check.call_args)
        == [(), {}])
    assert (
        list(m_os.environ.__setitem__.call_args)
        == [('ENVOY_DOCS_BUILD_CONFIG', m_config.return_value), {}])

    if check_fails:
        assert (
            list(m_print.call_args)
            == [(_check_error,), {}])
        assert not m_summary.called
        assert not m_build.called
        assert not m_create.called
        return

    assert (
        list(m_summary.call_args)
        == [(), {}])
    assert (
        list(m_build.call_args)
        == [(), {}])

    if build_fails:
        assert (
            list(m_print.call_args)
            == [(_build_error,), {}])
        assert not m_create.called
        return

    assert not m_print.called
    assert (
        list(m_create.call_args)
        == [(), {}])


def test_sphinx_runner_main(command_main):
    command_main(
        sphinx_runner.main,
        "tools.docs.sphinx_runner.SphinxRunner")
