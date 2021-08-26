
from unittest.mock import AsyncMock, MagicMock, PropertyMock

import pytest

import gidgethub

from tools.base import aio
from tools.base.functional import async_property
from tools.github.release import exceptions as github_errors, release as github_release


def test_release_constructor():
    release = github_release.GithubRelease("MANAGER", "VERSION")
    assert release.manager == "MANAGER"
    assert release.version == "VERSION"

    # assert release.fetcher == github_release.GithubReleaseAssetsFetcher
    # assert "fetcher" not in release.__dict__
    # assert release.pusher == github_release.GithubReleaseAssetsPusher
    # assert "pusher" not in release.__dict__


def _check_manager_property(prop, arg=None):
    manager = MagicMock()
    checker = github_release.GithubRelease(manager, "VERSION")
    assert getattr(checker, prop) == getattr(manager, arg or prop)
    assert prop not in checker.__dict__


@pytest.mark.parametrize(
    "prop",
    [("github",),
     ("log",),
     ("releases_url",),
     ("session",)])
def test_release_manager_props(prop):
    _check_manager_property(*prop)


@pytest.mark.asyncio
async def test_release_asset_names(patches):
    release = github_release.GithubRelease("MANAGER", "VERSION")
    patched = patches(
        ("GithubRelease.assets", dict(new_callable=PropertyMock)),
        prefix="tools.github.release.release")
    assets = [MagicMock(), MagicMock()]

    with patched as (m_assets, ):
        m_assets.side_effect = AsyncMock(return_value=assets)
        assert (
            await release.asset_names
            == set(m.__getitem__.return_value for m in assets))

    for asset in assets:
        assert (
            list(asset.__getitem__.call_args)
            == [('name',), {}])

    assert "asset_names" in release.__async_prop_cache__


@pytest.mark.asyncio
@pytest.mark.parametrize("raises", [None, BaseException, gidgethub.GitHubException])
async def test_release_assets(patches, raises):
    release = github_release.GithubRelease("MANAGER", "VERSION")
    patched = patches(
        ("GithubRelease.assets_url", dict(new_callable=PropertyMock)),
        ("GithubRelease.github", dict(new_callable=PropertyMock)),
        prefix="tools.github.release.release")

    with patched as (m_url, m_github):
        get = AsyncMock()
        url = AsyncMock()
        m_github.return_value.getitem.side_effect = get
        m_url.side_effect = url
        if raises:
            get.side_effect = raises("AN ERROR OCCURRED")
            exception = (
                github_errors.GithubReleaseError
                if raises == gidgethub.GitHubException
                else raises)
            with pytest.raises(exception):
                await release.assets
        else:
            assert (
                await release.assets
                == get.return_value)

    assert (
        list(get.call_args)
        == [(url.return_value,), {}])
    if not raises:
        assert "assets" in release.__async_prop_cache__


@pytest.mark.asyncio
async def test_release_assets_url(patches):
    release = github_release.GithubRelease("MANAGER", "VERSION")
    patched = patches(
        ("GithubRelease.release", dict(new_callable=PropertyMock)),
        prefix="tools.github.release.release")

    with patched as (m_release, ):
        mock_release = AsyncMock()
        m_release.side_effect = mock_release
        assert (
            await release.assets_url
            == mock_release.return_value.__getitem__.return_value)

    assert (
        list(mock_release.return_value.__getitem__.call_args)
        == [('assets_url',), {}])
    assert "assets_url" in getattr(release, async_property.cache_name)


@pytest.mark.asyncio
async def test_release_delete_url(patches):
    release = github_release.GithubRelease("MANAGER", "VERSION")
    patched = patches(
        ("GithubRelease.releases_url", dict(new_callable=PropertyMock)),
        ("GithubRelease.release_id", dict(new_callable=PropertyMock)),
        prefix="tools.github.release.release")

    with patched as (m_url, m_id):
        mock_id = AsyncMock()
        m_id.side_effect = mock_id
        assert (
            await release.delete_url
            == m_url.return_value.joinpath.return_value)

    assert (
        list(m_url.return_value.joinpath.call_args)
        == [(str(mock_id.return_value), ), {}])
    assert "delete_url" in getattr(release, async_property.cache_name)


@pytest.mark.asyncio
@pytest.mark.parametrize("version", [f"VERSION{i}" for i in range(0, 7)])
async def test_release_exists(patches, version):
    release = github_release.GithubRelease("MANAGER", "VERSION")
    patched = patches(
        ("GithubRelease.release_names", dict(new_callable=PropertyMock)),
        ("GithubRelease.version_name", dict(new_callable=PropertyMock)),
        prefix="tools.github.release.release")
    versions = [f"VERSION{i}" for i in range(3, 5)]

    with patched as (m_release, m_name):
        m_name.return_value = version
        m_release.side_effect = AsyncMock(return_value=versions)
        assert await release.exists == (version in versions)

    assert not hasattr(release, async_property.cache_name)


@pytest.mark.asyncio
async def test_release_release(patches):
    release = github_release.GithubRelease("MANAGER", "VERSION")
    patched = patches(
        ("GithubRelease.get", dict(new_callable=AsyncMock)),
        prefix="tools.github.release.release")

    with patched as (m_get, ):
        assert await release.release == m_get.return_value

    assert "release" in getattr(release, async_property.cache_name)


@pytest.mark.asyncio
async def test_release_release_id(patches):
    release = github_release.GithubRelease("MANAGER", "VERSION")
    patched = patches(
        ("GithubRelease.release", dict(new_callable=PropertyMock)),
        prefix="tools.github.release.release")

    with patched as (m_release, ):
        mock_release = AsyncMock()
        m_release.side_effect = mock_release
        assert await release.release_id == mock_release.return_value.__getitem__.return_value

    assert (
        list(mock_release.return_value.__getitem__.call_args)
        == [('id',), {}])
    assert "release_id" in getattr(release, async_property.cache_name)


@pytest.mark.asyncio
async def test_release_release_names(patches):
    manager = MagicMock()

    release_names = [dict(tag_name=f"TAG{i}") for i in range(0, 3)]

    async def releases_fun():
        return release_names

    manager.releases = releases_fun()
    release = github_release.GithubRelease(manager, "VERSION")
    assert (
        await release.release_names
        == tuple(t["tag_name"] for t in release_names))


@pytest.mark.asyncio
async def test_release_upload_url(patches):
    release = github_release.GithubRelease("MANAGER", "VERSION")
    patched = patches(
        ("GithubRelease.release", dict(new_callable=PropertyMock)),
        prefix="tools.github.release.release")

    with patched as (m_release, ):
        mock_release = AsyncMock()
        m_release.side_effect = mock_release
        assert (
            await release.upload_url
            == mock_release.return_value.__getitem__.return_value.split.return_value.__getitem__.return_value)

    assert (
        list(mock_release.return_value.__getitem__.call_args)
        == [('upload_url',), {}])
    assert (
        list(mock_release.return_value.__getitem__.return_value.split.call_args)
        == [('{',), {}])
    assert (
        list(mock_release.return_value.__getitem__.return_value.split.return_value.__getitem__.call_args)
        == [(0,), {}])
    assert "upload_url" in release.__async_prop_cache__


def test_release_version_name(patches):
    manager = MagicMock()
    release = github_release.GithubRelease(manager, "VERSION")
    release.version_name == manager.format_version.return_value
    assert (
        list(manager.format_version.call_args)
        == [("VERSION",), {}])


def test_release_version_url(patches):
    release = github_release.GithubRelease("MANAGER", "VERSION")
    patched = patches(
        ("GithubRelease.releases_url", dict(new_callable=PropertyMock)),
        ("GithubRelease.version_name", dict(new_callable=PropertyMock)),
        prefix="tools.github.release.release")

    with patched as (m_releases, m_version):
        assert release.version_url == m_releases.return_value.joinpath.return_value

    assert (
        list(m_releases.return_value.joinpath.call_args)
        == [("tags", m_version.return_value), {}])
    assert "version_url" in release.__dict__


@pytest.mark.asyncio
@pytest.mark.parametrize("exists", [True, False])
@pytest.mark.parametrize("assets", [None, [], [f"ASSET{i}" for i in range(0, 3)]])
@pytest.mark.parametrize("raises", [None, BaseException, gidgethub.GitHubException])
async def test_release_create(patches, exists, assets, raises):
    release = github_release.GithubRelease("MANAGER", "VERSION")
    patched = patches(
        "GithubRelease.fail",
        ("GithubRelease.push", dict(new_callable=AsyncMock)),
        ("GithubRelease.exists", dict(new_callable=PropertyMock)),
        ("GithubRelease.github", dict(new_callable=PropertyMock)),
        ("GithubRelease.log", dict(new_callable=PropertyMock)),
        ("GithubRelease.releases_url", dict(new_callable=PropertyMock)),
        ("GithubRelease.version_name", dict(new_callable=PropertyMock)),
        prefix="tools.github.release.release")
    args = (
        (assets, )
        if assets is not None
        else ())

    with patched as (m_fail, m_push, m_exists, m_github, m_log, m_url, m_version):
        m_exists.side_effect = AsyncMock(return_value=exists)
        m_push.return_value = dict(PUSHED=True)
        m_github.return_value.post = AsyncMock()
        if raises:
            m_github.return_value.post.side_effect = raises("AN ERROR OCCURRED")
        if raises and not exists:
            exception = (
                github_errors.GithubReleaseError
                if raises == gidgethub.GitHubException
                else raises)
            with pytest.raises(exception):
                await release.create(*args)
        else:
            result = await release.create(*args)

    expected = {}
    if not exists:
        assert (
            list(m_log.return_value.notice.call_args)
            == [(f"Creating release VERSION", ), {}])
        assert (
            list(m_github.return_value.post.call_args)
            == [(str(m_url.return_value), ), dict(data=dict(tag_name=m_version.return_value))])
        assert not m_fail.called
        if not raises:
            expected["release"] = m_github.return_value.post.return_value
            assert (
                list(m_log.return_value.success.call_args)
                == [(f"Release created VERSION", ), {}])
        else:
            assert not m_log.return_value.success.called
    else:
        assert not m_github.return_value.post.called
        assert not m_log.called
        assert (
            list(m_fail.call_args)
            == [(f"Release {m_version.return_value} already exists", ), {}])

    if not exists and raises:
        assert not m_push.called
        return
    if assets:
        expected["PUSHED"] = True
        assert (
            list(m_push.call_args)
            == [(assets, ), {}])
    else:
        assert not m_push.called
    assert result == expected


@pytest.mark.asyncio
@pytest.mark.parametrize("exists", [True, False])
@pytest.mark.parametrize("raises", [None, BaseException, gidgethub.GitHubException])
async def test_release_delete(patches, exists, raises):
    release = github_release.GithubRelease("MANAGER", "VERSION")
    patched = patches(
        ("GithubRelease.delete_url", dict(new_callable=PropertyMock)),
        ("GithubRelease.exists", dict(new_callable=PropertyMock)),
        ("GithubRelease.github", dict(new_callable=PropertyMock)),
        ("GithubRelease.log", dict(new_callable=PropertyMock)),
        ("GithubRelease.version_name", dict(new_callable=PropertyMock)),
        prefix="tools.github.release.release")

    with patched as (m_url, m_exists, m_github, m_log, m_version):
        url = AsyncMock()
        m_url.side_effect = url
        m_exists.side_effect = AsyncMock(return_value=exists)
        m_github.return_value.delete = AsyncMock()
        if raises:
            m_github.return_value.delete.side_effect = raises("AN ERROR OCCURRED")

        if exists and not raises:
            assert not await release.delete()
        elif raises == BaseException:
            with pytest.raises(BaseException) as e:
                await release.delete()
        else:
            with pytest.raises(github_errors.GithubReleaseError) as e:
                await release.delete()

        if not exists:
            assert (
                e.value.args[0]
                == f"Unable to delete version {m_version.return_value} as it does not exist")
            assert not m_log.called
            assert not m_github.called
            return
        assert (
            list(m_log.return_value.notice.call_args)
            == [(f"Deleting release version: {m_version.return_value}", ), {}])
        assert (
            list(m_github.return_value.delete.call_args)
            == [(str(url.return_value), ), {}])
        if raises:
            assert not m_log.return_value.success.called
            return
        assert (
            list(m_log.return_value.success.call_args)
            == [(f"Release version deleted: {m_version.return_value}", ), {}])


def test_release_fail():
    manager = MagicMock()
    release = github_release.GithubRelease(manager, "VERSION")
    assert release.fail("FAILURE") == manager.fail.return_value
    assert (
        list(manager.fail.call_args)
        == [("FAILURE", ), {}])


@pytest.mark.asyncio
@pytest.mark.parametrize("asset_types", [None, (), tuple(f"ASSET_TYPE{i}" for i in range(0, 3))])
@pytest.mark.parametrize("errors", [[], [0], [2, 4], range(0, 5)])
async def test_release_fetch(patches, asset_types, errors):
    release = github_release.GithubRelease("MANAGER", "VERSION")
    patched = patches(
        ("GithubRelease.fetcher", dict(new_callable=PropertyMock)),
        ("GithubRelease.log", dict(new_callable=PropertyMock)),
        ("GithubRelease.version_name", dict(new_callable=PropertyMock)),
        prefix="tools.github.release.release")

    kwargs = {} if asset_types is None else dict(asset_types=asset_types)
    fetched = MagicMock()

    async def fetcher(_releaser, path, asset_types, append=False):
        fetched(_releaser, path, asset_types, append)
        for x in range(0, 5):
            response = dict(name=f"FETCHED{x}")
            if x in errors:
                response["error"] = f"ERROR{x}"
            else:
                response["outfile"] = f"OUTFILE{x}"
            yield response
    expected = dict(
        errors=[
            dict(name=f"FETCHED{i}", error=f"ERROR{i}")
            for i in errors],
        assets=[
            dict(name=f"FETCHED{i}", outfile=f"OUTFILE{i}")
            for i in range(0, 5) if i not in errors])

    with patched as (m_fetcher, m_log, m_version):
        m_fetcher.return_value = fetcher
        assert await release.fetch("PATH", **kwargs) == expected

    assert (
        list(m_log.return_value.notice.call_args)
        == [(f"Downloading assets for release version: {m_version.return_value} -> PATH", ), {}])
    assert (
        list(fetched.call_args)
        == [(release, 'PATH', asset_types, False), {}])


@pytest.mark.asyncio
@pytest.mark.parametrize("raises", [None, BaseException, gidgethub.GitHubException])
async def test_release_get(patches, raises):
    release = github_release.GithubRelease("MANAGER", "VERSION")
    patched = patches(
        ("GithubRelease.version_url", dict(new_callable=PropertyMock)),
        ("GithubRelease.github", dict(new_callable=PropertyMock)),
        prefix="tools.github.release.release")

    with patched as (m_url, m_github):
        m_github.return_value.getitem = AsyncMock()
        if raises:
            m_github.return_value.getitem.side_effect = raises("AN ERROR OCCURRED")
            exception = (
                github_errors.GithubReleaseError
                if raises == gidgethub.GitHubException
                else raises)
            with pytest.raises(exception):
                await release.get()
        else:
            assert await release.get() == m_github.return_value.getitem.return_value
    assert (
        list(m_github.return_value.getitem.call_args)
        == [(str(m_url.return_value), ), {}])


@pytest.mark.asyncio
@pytest.mark.parametrize("raises", [None, BaseException, aio.ConcurrentError])
@pytest.mark.parametrize("errors", [[], [1, 3], range(0, 5)])
async def test_release_push(patches, raises, errors):
    release = github_release.GithubRelease("MANAGER", "VERSION")
    patched = patches(
        ("GithubRelease.log", dict(new_callable=PropertyMock)),
        ("GithubRelease.pusher", dict(new_callable=PropertyMock)),
        prefix="tools.github.release.release")
    artefacts = [f"ARTEFACTS{i}" for i in range(0, 5)]
    expected = dict(assets=[], errors=[])

    for i in range(0, 5):
        for x in range(0, 5):
            result = dict(
                name=f"ARTEFACTS{i}_ASSET{x}",
                foo=f"ARTEFACTS{i}_BAR{x}")
            if x in errors:
                result["error"] = f"GOT AN ERROR ARTEFACTS{i} {x}"
                expected["errors"].append(result)
            else:
                expected["assets"].append(result)


    class SomeError(Exception):
        pass

    async def pusher(path):
        if raises:
            raise raises(SomeError("AN ERROR OCCURRED"))
        for i in range(0, 5):
            response = dict(
                name=f"{path}_ASSET{i}",
                foo=f"{path}_BAR{i}")
            if i in errors:
                response["error"] = f"GOT AN ERROR {path} {i}"
            yield response

    with patched as (m_log, m_pusher):
        m_pusher.return_value.side_effect = lambda _self, path: pusher(path)
        if raises:
            with pytest.raises(BaseException if raises == BaseException else SomeError):
                await release.push(artefacts)
        else:
            assert await release.push(artefacts) == expected

    assert (
        list(m_log.return_value.notice.call_args)
        == [(f"Pushing assets for VERSION", ), {}])

    if raises:
        assert (
            list(list(c) for c in m_pusher.return_value.call_args_list)
            == [[(release, 'ARTEFACTS0'), {}]])
        assert not m_log.return_value.info.called
    else:
        assert (
            list(list(c) for c in m_pusher.return_value.call_args_list)
            == [[(release, f'ARTEFACTS{x}'), {}] for x in range(0, 5)])

    if raises or errors:
        assert not m_log.return_value.success.called
    else:
        assert (
            list(m_log.return_value.success.call_args)
            == [(f"Assets uploaded: VERSION", ), {}])
        assert (
            list(list(c) for c in m_log.return_value.info.call_args_list)
            == [[(f'Release file uploaded ARTEFACTS{i}_ASSET{x}',), {}]
                for i in range(0, 5)
                for x in range(0, 5)])
