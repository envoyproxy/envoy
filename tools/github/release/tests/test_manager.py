
from unittest.mock import AsyncMock, MagicMock, PropertyMock

import pytest

import packaging.version

from tools.base.functional import async_property
from tools.github.release import manager, exceptions as github_errors


@pytest.mark.parametrize("continues", [None, True, False])
@pytest.mark.parametrize("create", [None, True, False])
@pytest.mark.parametrize("user", [None, "USER"])
@pytest.mark.parametrize("oauth_token", [None, "OAUTH TOKEN"])
@pytest.mark.parametrize("log", [None, "LOG"])
@pytest.mark.parametrize("asset_types", [None, "ASSET TYPES"])
@pytest.mark.parametrize("github", [None, "GITHUB"])
@pytest.mark.parametrize("session", [None, "SESSION"])
def test_release_manager_constructor(continues, create, user, oauth_token, log, asset_types, github, session):
    kwargs = dict(
        continues=continues,
        create=create,
        user=user,
        oauth_token=oauth_token,
        log=log,
        asset_types=asset_types,
        github=github,
        session=session)
    kwargs = {k: v for k, v in kwargs.items() if v is not None}
    releaser = manager.GithubReleaseManager("PATH", "REPOSITORY", **kwargs)
    assert releaser._path == "PATH"
    assert releaser.repository == "REPOSITORY"
    assert releaser.continues == (continues if continues is not None else False)
    assert releaser.create == (create if create is not None else True)
    assert releaser._log == log
    assert releaser.oauth_token == oauth_token
    assert releaser.user == user
    assert releaser._asset_types == asset_types
    assert releaser._github == github
    assert releaser._session == session

    assert releaser._version_re == r"v(\w+)"
    assert (
        releaser.version_min
        == manager.VERSION_MIN
        == packaging.version.Version("0"))
    assert "version_min" not in releaser.__dict__


@pytest.mark.asyncio
async def test_release_manager_async_contextmanager(patches):
    patched = patches(
        ("GithubReleaseManager.close", dict(new_callable=AsyncMock)),
        prefix="tools.github.release.manager")

    with patched as (m_close, ):
        async with manager.GithubReleaseManager("PATH", "REPOSITORY") as releaser:
            assert isinstance(releaser, manager.GithubReleaseManager)
            assert not m_close.called
        assert (
            list(m_close.call_args)
            == [(), {}])


def test_release_manager_dunder_getitem():
    releaser = manager.GithubReleaseManager("PATH", "REPOSITORY")

    with pytest.raises(NotImplementedError):
        releaser["X.Y.Z"]


@pytest.mark.parametrize("oauth_token", [None, "OAUTH_TOKEN"])
@pytest.mark.parametrize("user", [None, "USER"])
@pytest.mark.parametrize("github", [True, False])
def test_release_manager_github(patches, oauth_token, user, github):
    kwargs = {}
    if oauth_token:
        kwargs["oauth_token"] = oauth_token
    if user:
        kwargs["user"] = user
    if github:
        kwargs["github"] = "GITHUB"
    releaser = manager.GithubReleaseManager("PATH", "REPOSITORY", **kwargs)
    patched = patches(
        "gidgethub",
        ("GithubReleaseManager.session", dict(new_callable=PropertyMock)),
        prefix="tools.github.release.manager")

    with patched as (m_api, m_session):
        assert (
            releaser.github
            == (m_api.aiohttp.GitHubAPI.return_value
                if not github
                else "GITHUB"))

    assert "github" in releaser.__dict__
    if github:
        assert not m_api.aiohttp.GitHubAPI.called
        return
    assert (
        list(m_api.aiohttp.GitHubAPI.call_args)
        == [(m_session.return_value, user),
            {'oauth_token': oauth_token}])


@pytest.mark.parametrize("log", [True, False])
def test_release_manager_log(patches, log):
    releaser = manager.GithubReleaseManager("PATH", "REPOSITORY")
    patched = patches(
        "verboselogs",
        prefix="tools.github.release.manager")
    if log:
        releaser._log = "LOG"

    with patched as (m_log, ):
        assert (
            releaser.log
            == (m_log.VerboseLogger.return_value
                if not log
                else "LOG"))

    assert "log" in releaser.__dict__

    if log:
        assert not m_log.VerboseLogger.called
        return

    assert (
        list(m_log.VerboseLogger.call_args)
        == [('tools.github.release.manager',), {}])


def test_release_manager_path(patches):
    releaser = manager.GithubReleaseManager("PATH", "REPOSITORY")
    patched = patches(
        "pathlib",
        prefix="tools.github.release.manager")

    with patched as (m_plib, ):
        assert (
            releaser.path
            == m_plib.Path.return_value)

    assert "path" in releaser.__dict__
    assert (
        list(m_plib.Path.call_args)
        == [('PATH',), {}])


@pytest.mark.asyncio
async def test_release_manager_latest(patches):
    releaser = manager.GithubReleaseManager("PATH", "REPOSITORY")
    patched = patches(
        "GithubReleaseManager.parse_version",
        ("GithubReleaseManager.releases", dict(new_callable=PropertyMock)),
        prefix="tools.github.release.manager")

    versions = [dict(tag_name=v) for v in ("1.19.2", "X", "1.19.1", "Y_Z", "1.20.3", "", "0.0.1")]

    with patched as (m_version, m_releases):
        m_version.side_effect = lambda version: (packaging.version.Version(version) if "." in version else None)
        m_releases.side_effect = AsyncMock(return_value=versions)
        result = await releaser.latest

    assert (
        result
        == {'0.0.1': packaging.version.Version('0.0.1'),
            '0.0': packaging.version.Version('0.0.1'),
            '1.19.2': packaging.version.Version('1.19.2'),
            '1.19': packaging.version.Version('1.19.2'),
            '1.19.1': packaging.version.Version('1.19.1'),
            '1.20.3': packaging.version.Version('1.20.3'),
            '1.20': packaging.version.Version('1.20.3')})
    assert not hasattr(releaser, "__async_prop_cache__")


@pytest.mark.asyncio
async def test_release_manager_releases(patches):
    releaser = manager.GithubReleaseManager("PATH", "REPOSITORY")
    patched = patches(
        ("GithubReleaseManager.github", dict(new_callable=PropertyMock)),
        ("GithubReleaseManager.releases_url", dict(new_callable=PropertyMock)),
        prefix="tools.github.release.manager")
    getiter_mock = MagicMock()

    async def getiter(url):
        getiter_mock(url)
        for i in range(0, 5):
            yield i

    with patched as (m_github, m_releases):
        m_github.return_value.getiter = getiter
        assert await releaser.releases == list(range(0, 5))

    assert (
        list(getiter_mock.call_args)
        == [(str(m_releases.return_value), ), {}])
    assert not hasattr(releaser, async_property.cache_name)


def test_release_manager_releases_url(patches):
    releaser = manager.GithubReleaseManager("PATH", "REPOSITORY")
    patched = patches(
        "pathlib",
        prefix="tools.github.release.manager")

    with patched as (m_plib, ):
        assert releaser.releases_url == m_plib.PurePosixPath.return_value

    assert (
        list(m_plib.PurePosixPath.call_args)
        == [(f"/repos/REPOSITORY/releases", ), {}])
    assert "releases_url" in releaser.__dict__


@pytest.mark.parametrize("session", [True, False])
def test_release_manager_session(patches, session):
    releaser = manager.GithubReleaseManager("PATH", "REPOSITORY")
    patched = patches(
        "aiohttp",
        prefix="tools.github.release.manager")
    if session:
        releaser._session = "SESSION"

    with patched as (m_http, ):
        assert (
            releaser.session
            == (m_http.ClientSession.return_value
                if not session
                else "SESSION"))

    assert "session" in releaser.__dict__
    if session:
        assert not m_http.ClientSession.called
        return
    assert (
        list(m_http.ClientSession.call_args)
        == [(), {}])


def test_release_manager_version_re(patches):
    releaser = manager.GithubReleaseManager("PATH", "REPOSITORY")
    patched = patches(
        "re",
        prefix="tools.github.release.manager")
    releaser._version_re = "VERSION RE"

    with patched as (m_re, ):
        assert releaser.version_re == m_re.compile.return_value

    assert (
        list(m_re.compile.call_args)
        == [("VERSION RE", ), {}])


@pytest.mark.asyncio
@pytest.mark.parametrize("session", [True, False])
async def test_release_manager_close(patches, session):
    releaser = manager.GithubReleaseManager("PATH", "REPOSITORY")
    patched = patches(
        ("GithubReleaseManager.session", dict(new_callable=PropertyMock)),
        prefix="tools.github.release.manager")

    if session:
        releaser.__dict__["session"] = "SESSION"

    with patched as (m_session, ):
        m_session.return_value.close = AsyncMock()
        assert not await releaser.close()

    assert "session" not in releaser.__dict__

    if not session:
        assert not m_session.called
        return

    assert (
        list(m_session.return_value.close.call_args)
        == [(), {}])


@pytest.mark.parametrize("continues", [True, False])
def test_release_manager_fail(patches, continues):
    releaser = manager.GithubReleaseManager("PATH", "REPOSITORY", continues=continues)
    parser = MagicMock()
    patched = patches(
        ("GithubReleaseManager.log", dict(new_callable=PropertyMock)),
        prefix="tools.github.release.manager")

    with patched as (m_log, ):
        if continues:
            assert (
                releaser.fail("MESSAGE")
                == "MESSAGE")
        else:
            with pytest.raises(github_errors.GithubReleaseError):
                releaser.fail("MESSAGE")

    if not continues:
        assert not m_log.return_value.warning.called
        return

    assert (
        list(m_log.return_value.warning.call_args)
        == [("MESSAGE", ), {}])


def test_release_manager_format_version():
    releaser = manager.GithubReleaseManager("PATH", "REPOSITORY")
    releaser._version_format = MagicMock()
    assert releaser.format_version("VERSION") == releaser._version_format.format.return_value
    assert (
        list(releaser._version_format.format.call_args)
        == [(), dict(version="VERSION")])


@pytest.mark.parametrize("version", [None, 0, "", "1.2.3"])
@pytest.mark.parametrize("raises", [None, BaseException, packaging.version.InvalidVersion])
def test_release_manager_parse_version(patches, version, raises):
    releaser = manager.GithubReleaseManager("PATH", "REPOSITORY")
    patched = patches(
        "packaging.version.Version",
        ("GithubReleaseManager.log", dict(new_callable=PropertyMock)),
        ("GithubReleaseManager.version_re", dict(new_callable=PropertyMock)),
        prefix="tools.github.release.manager")

    with patched as (m_packaging, m_log, m_version):
        m_version.return_value.sub.return_value = version
        if raises:
            m_packaging.side_effect = raises()

        if version and raises == BaseException:
            with pytest.raises(BaseException):
                releaser.parse_version("VERSION")
        else:
            assert (
                releaser.parse_version("VERSION")
                == (None
                    if not version or raises
                    else m_packaging.return_value))

    assert (
        list(m_version.return_value.sub.call_args)
        == [(r"\1", "VERSION"), {}])
    if version:
        assert (
            list(m_packaging.call_args)
            == [(m_version.return_value.sub.return_value, ), {}])
    else:
        assert not m_packaging.called

    if not version or raises and raises != BaseException:
        assert (
            list(m_log.return_value.warning.call_args)
            == [("Unable to parse version: VERSION", ), {}])
    else:
        assert not m_log.called
