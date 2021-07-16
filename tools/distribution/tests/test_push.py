import configparser
from unittest.mock import MagicMock, PropertyMock

import pytest

import git

from tools.base import runner
from tools.distribution import push


def test_pusher_constructor():
    pusher = push.GitPushRunner("path1", "path2", "path3")
    assert isinstance(pusher, runner.Runner)


@pytest.mark.parametrize("committer", [None, "COMMITTER"])
@pytest.mark.parametrize("name", [None, "NAME"])
@pytest.mark.parametrize("email", [None, "EMAIL"])
def test_pusher_committer(patches, committer, name, email):
    pusher = push.GitPushRunner("x", "y", "z")
    patched = patches(
        "parseaddr",
        ("GitPushRunner.args", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.push")

    with patched as (m_parse, m_args):
        m_args.return_value.committer = committer
        m_parse.return_value = (name, email)
        if committer and not (name and email):
            result = None
            with pytest.raises(push.PushError) as e:
                pusher.committer
        else:
            result = pusher.committer
            assert "committer" in pusher.__dict__

    if not committer:
        assert not result
        assert not m_parse.called
        return

    assert (
        list(m_parse.call_args)
        == [('COMMITTER',), {}])

    if not (name and email):
        assert not result
        assert (
            e.value.args[0]
            == 'Supplied --committer argument did not match `Name <emai.l>` format')
        return
    assert result == (name, email)


@pytest.mark.parametrize("committer", [None, ("NAME", "EMAIL")])
def test_pusher_committer_uid(patches, committer):
    pusher = push.GitPushRunner("x", "y", "z")
    patched = patches(
        "formataddr",
        ("GitPushRunner.committer", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.push")

    with patched as (m_format, m_committer):
        m_committer.return_value = committer
        if not committer:
            assert not pusher.committer_uid
        else:
            assert pusher.committer_uid == m_format.return_value

    if committer:
        assert (
            list(m_format.call_args)
            == [(('NAME', 'EMAIL'),), {}])
    else:
        assert not m_format.called


def test_pusher_message(patches):
    pusher = push.GitPushRunner("x", "y", "z")
    patched = patches(
        ("GitPushRunner.args", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.push")

    with patched as (m_args, ):
        assert pusher.message == m_args.return_value.message

    assert "message" not in pusher.__dict__


def test_pusher_overwrite(patches):
    pusher = push.GitPushRunner("x", "y", "z")
    patched = patches(
        ("GitPushRunner.args", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.push")

    with patched as (m_args, ):
        assert pusher.overwrite == m_args.return_value.overwrite

    assert "overwrite" not in pusher.__dict__


def test_pusher_path(patches):
    pusher = push.GitPushRunner("x", "y", "z")
    patched = patches(
        ("GitPushRunner.args", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.push")

    with patched as (m_args, ):
        assert pusher.path == m_args.return_value.path

    assert "path" not in pusher.__dict__


def test_pusher_tarballs(patches):
    pusher = push.GitPushRunner("x", "y", "z")
    patched = patches(
        ("GitPushRunner.args", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.push")

    with patched as (m_args, ):
        assert pusher.tarballs == m_args.return_value.tarballs

    assert "tarballs" not in pusher.__dict__


def test_pusher_uri(patches):
    pusher = push.GitPushRunner("x", "y", "z")
    patched = patches(
        ("GitPushRunner.args", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.push")

    with patched as (m_args, ):
        assert pusher.uri == m_args.return_value.uri

    assert "uri" not in pusher.__dict__


def test_pusher_add_arguments():
    pusher = push.GitPushRunner("x", "y", "z")
    parser = MagicMock()
    pusher.add_arguments(parser)
    assert (
        list(list(c) for c in parser.add_argument.call_args_list)
        == [[('--log-level', '-l'),
             {'choices': ['debug', 'info', 'warn', 'error'],
              'default': 'info',
              'help': 'Log level to display'}],
            [('uri',),
             {'help': 'Git URI'}],
            [('tarballs',),
             {'nargs': '+',
              'help': 'Tar files to extract into the repository'}],
            [('--committer',),
             {'help': 'Git committer, in the `Name <emai.l>` format. If this is not set, '
              'the git global `user.name` *and* `user.email` config must be set'}],
            [('--message', '-m'),
             {'help': 'Git commit message'}],
            [('--path',),
             {'default': '',
              'help': 'Path to add extracted files within the repository'}],
            [('--overwrite',),
             {'action': 'store_true', 'help': 'Overwrite existing files'}]])


@pytest.mark.parametrize("path", [None, "PATH"])
def test_pusher_commit(patches, path):
    pusher = push.GitPushRunner("x", "y", "z")
    repo = MagicMock()
    patched = patches(
        ("GitPushRunner.committer_uid", dict(new_callable=PropertyMock)),
        ("GitPushRunner.message", dict(new_callable=PropertyMock)),
        ("GitPushRunner.path", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.push")

    with patched as (m_committer, m_message, m_path):
        m_path.return_value = path
        assert not pusher.commit(repo)

    assert (
        list(repo.index.add.call_args)
        == [([m_path.return_value or "."],), {}])
    assert (
        list(repo.index.commit.call_args)
        == [(m_message.return_value,), {}])


def test_pusher_extract(patches):
    pusher = push.GitPushRunner("x", "y", "z")
    patched = patches(
        "GitPushRunner.extract_tarball",
        ("GitPushRunner.tarballs", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.push")
    _tarballs = ["TARB1", "TARB2", "TARB3"]

    with patched as (m_extract, m_tarballs):
        m_tarballs.return_value = _tarballs
        pusher.extract("PATH")

    assert (
        list(list(c) for c in m_extract.call_args_list)
        == [[(tarb, 'PATH'), {}] for tarb in _tarballs])


def test_pusher_extract_and_push(patches):
    pusher = push.GitPushRunner("x", "y", "z")
    patched = patches(
        "GitPushRunner.set_committer",
        "GitPushRunner.extract",
        "GitPushRunner.repo_path",
        "GitPushRunner.commit",
        "GitPushRunner.push",
        prefix="tools.distribution.push")

    with patched as (m_committer, m_extract, m_path, m_commit, m_push):
        assert not pusher.extract_and_push("REPO")

    assert (
        list(m_committer.call_args)
        == [('REPO',), {}])
    assert (
        list(m_path.call_args)
        == [('REPO',), {}])
    assert (
        list(m_extract.call_args)
        == [(m_path.return_value,), {}])
    assert (
        list(m_commit.call_args)
        == [('REPO',), {}])
    assert (
        list(m_push.call_args)
        == [('REPO',), {}])


def test_pusher_extract_tarball(patches):
    pusher = push.GitPushRunner("REPO", "TARBALL")
    patched = patches(
        "tarfile",
        "GitPushRunner.extract_tarfile",
        ("GitPushRunner.log", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.push")

    _tarfiles = ["TARF1", "TARF2", "TARF3"]

    with patched as (m_tar, m_extract, m_log):
        m_tar.open.return_value.__enter__.return_value = _tarfiles
        assert not pusher.extract_tarball("TARBALL", "PATH")

    assert (
        list(m_log.return_value.info.call_args)
        == [('Extract tarball: TARBALL -> PATH',), {}])
    assert (
        list(m_tar.open.call_args)
        == [('TARBALL',), {}])
    assert (
        list(list(c) for c in m_extract.call_args_list)
        == [[(_tarfiles, tarf, 'PATH'), {}] for tarf in _tarfiles])


@pytest.mark.parametrize("name", [".", "NAME"])
@pytest.mark.parametrize("isdir", [True, False])
@pytest.mark.parametrize("overwrite", [True, False])
@pytest.mark.parametrize("exists", [True, False])
def test_pusher_extract_tarfile(patches, name, isdir, overwrite, exists):
    pusher = push.GitPushRunner("x", "y", "z")
    tarfiles = MagicMock()
    info = MagicMock()
    patched = patches(
        "os",
        ("GitPushRunner.overwrite", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.push")

    info.name = name
    info.isdir.return_value = isdir

    should_fail = (
        name != "."
        and not isdir
        and not overwrite
        and exists)

    with patched as (m_os, m_overwrite):
        m_overwrite.return_value = overwrite
        m_os.path.exists.return_value = exists

        if should_fail:
            with pytest.raises(push.PushError) as e:
                pusher.extract_tarfile(tarfiles, info, "PATH")
            assert (
                e.value.args[0]
                == f'File to push already exists and overwrite is not set: {name}')
        else:
            assert not pusher.extract_tarfile(tarfiles, info, "PATH")

    if name == "." or isdir:
        assert not m_overwrite.called
        assert not m_os.path.exists.called
        assert not m_os.path.join.called
        assert not m_os.path.abspath.called
        assert not tarfiles.extract.called
        return

    if overwrite:
        assert not m_os.path.exists.called
        assert not m_os.path.join.called
        assert not m_os.path.abspath.called
    else:
        assert (
            list(m_os.path.join.call_args)
            == [('PATH', name), {}])
        assert (
            list(m_os.path.abspath.call_args)
            == [(m_os.path.join.return_value,), {}])
        assert (
            list(m_os.path.exists.call_args)
            == [(m_os.path.abspath.return_value,), {}])

    if overwrite or not exists:
        assert (
            list(tarfiles.extract.call_args)
            == [(info, 'PATH'), {}])
    else:
        assert not tarfiles.extract.called


@pytest.mark.parametrize("committer", [None, "COMMITTER"])
@pytest.mark.parametrize("global_committer", [None, "GLOBAL COMMITTER"])
def test_pusher_get_committer(patches, committer, global_committer):
    pusher = push.GitPushRunner("x", "y", "z")
    patched = patches(
        "formataddr",
        "GitPushRunner.global_committer",
        ("GitPushRunner.committer", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.push")

    with patched as (m_format, m_global, m_committer):
        m_global.return_value = global_committer
        m_committer.return_value = committer
        if committer or global_committer:
            assert pusher.get_committer("REPO") == m_format.return_value
        else:
            assert not pusher.get_committer("REPO")

    if committer:
        assert not m_global.called
        assert (
            list(m_format.call_args)
            == [(committer,), {}])
        return

    assert (
        list(m_global.call_args)
        == [('REPO',), {}])

    if global_committer:
        assert (
            list(m_format.call_args)
            == [(global_committer,), {}])
        return

    assert not m_format.called


@pytest.mark.parametrize(
    "raises",
    [None, configparser.NoSectionError, configparser.NoOptionError])
def test_pusher_global_committer(raises):
    pusher = push.GitPushRunner("x", "y", "z")
    repo = MagicMock()

    def _raise(section, option):
        if raises == configparser.NoSectionError:
            raise raises(section)
        raise raises(section, option)

    if raises:
        repo.config_reader.return_value.get_value.side_effect = _raise

    result = pusher.global_committer(repo)

    if raises:
        assert not result
        assert (
            list(list(c) for c in repo.config_reader.return_value.get_value.call_args_list)
            == [[('user', 'name'), {}]])
        return

    assert result == (repo.config_reader.return_value.get_value.return_value, ) * 2
    assert (
        list(list(c) for c in repo.config_reader.return_value.get_value.call_args_list)
        == [[('user', 'name'), {}],
            [('user', 'email'), {}]])


def test_pusher_push(patches):
    pusher = push.GitPushRunner("x", "y", "z")
    repo = MagicMock()

    patched = patches(
        ("GitPushRunner.log", dict(new_callable=PropertyMock)),
        ("GitPushRunner.uri", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.push")

    with patched as (m_log, m_uri):
        assert not pusher.push(repo)

    assert (
        list(m_log.return_value.info.call_args)
        == [(f"Push updates to {m_uri.return_value}",), {}])
    assert (
        list(repo.remotes.origin.push.call_args)
        == [(), {}])


def test_pusher_repo_path(patches):
    pusher = push.GitPushRunner("x", "y", "z")
    repo = MagicMock()

    patched = patches(
        "os",
        ("GitPushRunner.path", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.push")

    with patched as (m_os, m_path):
        assert pusher.repo_path(repo) == m_os.path.join.return_value

    assert (
        list(m_os.path.join.call_args)
        == [(repo.working_dir, m_path.return_value), {}])


@pytest.mark.parametrize(
    "raises",
    [None, push.PushError, git.GitCommandError, Exception])
def test_pusher_run(patches, raises):
    pusher = push.GitPushRunner("x", "y", "z")
    repo = MagicMock()

    patched = patches(
        "git_repo",
        "GitPushRunner.extract_and_push",
        ("GitPushRunner.log", dict(new_callable=PropertyMock)),
        ("GitPushRunner.uri", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.push")

    with patched as (m_repo, m_extract, m_log, m_uri):
        if raises == git.GitCommandError:
            m_repo.side_effect = raises("GIT ERROR")
        if raises == push.PushError:
            m_extract.side_effect = raises("PUSH ERROR")

        if raises == Exception:
            m_extract.side_effect = raises("OTHER ERROR")
            result = 1
            with pytest.raises(Exception):
                pusher.run()
        else:
            result = pusher.run()

    if raises == git.GitCommandError:
        assert (
            list(list(c) for c in m_log.return_value.info.call_args_list)
            == [[(f"Clone repo: {m_uri.return_value}",), {}]])
    else:
        assert (
            list(list(c) for c in m_log.return_value.info.call_args_list)
            == [[(f"Clone repo: {m_uri.return_value}",), {}],
                [(f"Cloned repository: {m_uri.return_value} -> {m_repo.return_value.__enter__.return_value.working_dir}",), {}]])
        assert (
            list(m_extract.call_args)
            == [(m_repo.return_value.__enter__.return_value,), {}])

    if raises:
        assert result == 1
        return
    assert result == m_extract.return_value


@pytest.mark.parametrize("get_committer", [None, "COMMITTER"])
@pytest.mark.parametrize("committer", [None, ["NAME", "EMAIL"]])
def test_pusher_set_committer(patches, get_committer, committer):
    pusher = push.GitPushRunner("x", "y", "z")
    repo = MagicMock()

    patched = patches(
        "GitPushRunner.get_committer",
        ("GitPushRunner.log", dict(new_callable=PropertyMock)),
        ("GitPushRunner.committer", dict(new_callable=PropertyMock)),
        ("GitPushRunner.committer_uid", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.push")

    with patched as (m_get, m_log, m_committer, m_uid):
        m_get.return_value = get_committer
        m_committer.return_value = committer

        if not get_committer:
            with pytest.raises(push.PushError) as e:
                pusher.set_committer(repo)
        else:
            assert not pusher.set_committer(repo)

    if not get_committer:
        assert (
            e.value.args[0]
            == ('You must either provide the `--committer` argument '
                'or the global git `user.name` and `user.email` configuration must be set'))
        assert not m_committer.called
        assert not repo.config_writer.called
        assert not m_log.return_value.info.called
        return

    if not committer:
        assert not repo.config_writer.called
        assert not m_log.return_value.info.called
        return

    assert (
        list(repo.config_writer.call_args)
        == [(), {}])
    assert (
        list(list(c) for c in repo.config_writer.return_value.__enter__.return_value.set_value.call_args_list)
        == [[('user', 'name', 'NAME'), {}],
            [('user', 'email', 'EMAIL'), {}]])
    assert (
        list(m_log.return_value.info.call_args)
        == [(f"Set committer: {m_uid.return_value}",), {}])


def test_push_main(patches, command_main):
    command_main(
        push.main,
        "tools.distribution.push.GitPushRunner")
