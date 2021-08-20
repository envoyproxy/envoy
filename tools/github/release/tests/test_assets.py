
from unittest.mock import AsyncMock, MagicMock, PropertyMock

import pytest

from tools.base import aio
from tools.base.functional import async_property
from tools.github.release import assets as github_assets


def test_assets_constructor():
    assets = github_assets.GithubReleaseAssets("RELEASE", "PATH")
    assert assets.release == "RELEASE"
    assert assets._path == "PATH"
    assert assets.concurrency == 4

    assert assets.path == "PATH"
    assert "path" not in assets.__dict__


def _check_assets_property(patches, prop, arg=None):
    release = MagicMock()
    assets = github_assets.GithubReleaseAssets(release, "VERSION")
    assert getattr(assets, prop) == getattr(release, arg or prop)
    assert prop not in assets.__dict__


@pytest.mark.parametrize(
    "prop",
    [("github",),
     ("session",)])
def test_assets_props(patches, prop):
    _check_assets_property(patches, *prop)


def test_assets_context(patches):
    release = github_assets.GithubReleaseAssets("RELEASE", "PATH")
    patched = patches(
        "GithubReleaseAssets.cleanup",
        prefix="tools.github.release.assets")

    with patched as (m_cleanup, ):
        with release as _release:
            pass

    assert _release is release
    assert m_cleanup.called


@pytest.mark.asyncio
@pytest.mark.parametrize("raises", [None, BaseException, aio.ConcurrentIteratorError])
async def test_assets_dunder_aiter(patches, raises):
    assets = github_assets.GithubReleaseAssets("RELEASE", "PATH")
    patched = patches(
        "GithubReleaseAssets.__enter__",
        "GithubReleaseAssets.__exit__",
        ("GithubReleaseAssets.run", dict(new_callable=MagicMock)),
        prefix="tools.github.release.assets")
    _results = []

    async def _run():
        for x in range(0, 5):
            if x == 3 and raises:
                raise raises("AN ERROR OCCURRED")
            yield x

    with patched as (m_enter, m_exit, m_run):
        m_run.return_value = _run()
        m_enter.return_value = None
        m_exit.return_value = None
        if raises == BaseException:
            with pytest.raises(BaseException) as e:
                async for result in assets:
                    _results.append(result)
        elif raises:
            with pytest.raises(github_assets.GithubReleaseError) as e:
                async for result in assets:
                    _results.append(result)
        else:
            async for result in assets:
                _results.append(result)

    assert (
        list(m_run.call_args)
        == [(), {}])
    assert (
        list(m_enter.call_args)
        == [(), {}])

    if raises:
        assert (
            m_exit.call_args[0][0]
            == (raises
                if raises == BaseException
                else github_assets.GithubReleaseError))
        assert m_exit.call_args[0][1] == e.value
        assert _results == [0, 1, 2]
        assert e.value.args[0] == "AN ERROR OCCURRED"
        return

    assert (
        list(m_exit.call_args)
        == [(None, None, None), {}])
    assert _results == list(range(0, 5))


def test_assets_tasks(patches):
    assets = github_assets.GithubReleaseAssets("RELEASE", "PATH")
    patched = patches(
        "aio",
        ("GithubReleaseAssets.awaitables", dict(new_callable=PropertyMock)),
        prefix="tools.github.release.assets")
    assets.concurrency = 23

    with patched as (m_aio, m_await):
        assert assets.tasks == m_aio.concurrent.return_value

    assert (
        list(m_aio.concurrent.call_args)
        == [(m_await.return_value,), dict(limit=23)])
    assert "tasks" not in assets.__dict__


def test_assets_tempdir(patches):
    assets = github_assets.GithubReleaseAssets("RELEASE", "PATH")
    patched = patches(
        "tempfile",
        prefix="tools.github.release.assets")
    assets.concurrency = 23

    with patched as (m_temp, ):
        assert assets.tempdir == m_temp.TemporaryDirectory.return_value

    assert (
        list(m_temp.TemporaryDirectory.call_args)
        == [(), {}])


def test_fetcher_constructor(patches):
    patched = patches(
        "GithubReleaseAssets.__init__",
        prefix="tools.github.release.assets")

    with patched as (m_super, ):
        fetcher = github_assets.GithubReleaseAssetsFetcher("RELEASE", "PATH", "ASSET_TYPES")

    assert (
        list(m_super.call_args)
        == [("RELEASE", "PATH"), {}])
    assert fetcher._asset_types == "ASSET_TYPES"


@pytest.mark.asyncio
async def test_fetcher_assets(patches):
    release = MagicMock()
    type(release).assets = PropertyMock(side_effect=AsyncMock(return_value="ASSETS"))
    fetcher = github_assets.GithubReleaseAssetsFetcher("RELEASE", "PATH", "ASSET_TYPES")
    fetcher.release = release
    assert await fetcher.assets == "ASSETS"
    assert not hasattr(fetcher, async_property.cache_name)


@pytest.mark.asyncio
async def test_fetcher_download(patches):
    fetcher = github_assets.GithubReleaseAssetsFetcher("RELEASE", "PATH", "ASSET_TYPES")
    patched = patches(
        ("GithubReleaseAssetsFetcher.save", dict(new_callable=AsyncMock)),
        ("GithubReleaseAssetsFetcher.session", dict(new_callable=PropertyMock)),
        prefix="tools.github.release.assets")
    asset = dict(
        asset_type="ASSET TYPE",
        browser_download_url="ASSET DOWNLOAD URL",
        name="ASSET NAME")

    with patched as (m_save, m_session):
        m_session.return_value.get = AsyncMock()
        assert (
            await fetcher.download(asset)
            == m_save.return_value)

    assert (
        list(m_save.call_args)
        == [("ASSET TYPE", "ASSET NAME", m_session.return_value.get.return_value), {}])
    assert (
        list(m_session.return_value.get.call_args)
        == [('ASSET DOWNLOAD URL',), {}])


@pytest.mark.asyncio
@pytest.mark.parametrize("status", [None, 200, 201])
async def test_fetcher_save(patches, status):
    fetcher = github_assets.GithubReleaseAssetsFetcher("RELEASE", "PATH", "ASSET_TYPES")
    patched = patches(
        "stream",
        "GithubReleaseAssetsFetcher.fail",
        ("GithubReleaseAssetsFetcher.path", dict(new_callable=PropertyMock)),
        prefix="tools.github.release.assets")
    download = MagicMock()
    download.status = status

    with patched as (m_stream, m_fail, m_path):
        m_stream.__aenter__ = AsyncMock()
        outfile = m_path.return_value.joinpath.return_value
        result = await fetcher.save("ASSET TYPE", "NAME", download)

    expected = dict(name="NAME", outfile=outfile)
    if status != 200:
        assert (
            list(m_fail.call_args)
            == [(f"Failed downloading, got response:\n{download}", ), {}])
        expected["error"] = m_fail.return_value
    else:
        assert not m_fail.called

    assert result == expected
    assert (
        list(m_path.return_value.joinpath.call_args)
        == [('ASSET TYPE', 'NAME'), {}])
    assert (
        list(outfile.parent.mkdir.call_args)
        == [(), dict(exist_ok=True)])
    assert (
        list(m_stream.async_writer.call_args)
        == [(outfile, ), {}])
    assert (
        list(m_stream.async_writer.return_value.__aenter__.return_value.stream_bytes.call_args)
        == [(download, ), {}])


def test_pusher_constructor(patches):
    patched = patches(
        "GithubReleaseAssets.__init__",
        prefix="tools.github.release.assets")

    with patched as (m_super, ):
        m_super.return_value = None
        github_assets.GithubReleaseAssetsPusher("RELEASE", "PATH")

    assert (
        list(m_super.call_args)
        == [("RELEASE", "PATH"), {}])


@pytest.mark.asyncio
async def test_fetcher_asset_names(patches):
    release = MagicMock()
    type(release).asset_names = PropertyMock(side_effect=AsyncMock(return_value="ASSET NAMES"))
    pusher = github_assets.GithubReleaseAssetsPusher("RELEASE", "PATH")
    pusher.release = release
    assert await pusher.asset_names == "ASSET NAMES"
    assert not hasattr(pusher, async_property.cache_name)


@pytest.mark.asyncio
async def test_fetcher_upload_url(patches):
    release = MagicMock()
    type(release).upload_url = PropertyMock(side_effect=AsyncMock(return_value="ASSET NAMES"))
    pusher = github_assets.GithubReleaseAssetsPusher("RELEASE", "PATH")
    pusher.release = release
    assert await pusher.upload_url == "ASSET NAMES"
    assert not hasattr(pusher, async_property.cache_name)


def test_fetcher_version(patches):
    release = MagicMock()
    type(release).version = PropertyMock(return_value="VERSION")
    pusher = github_assets.GithubReleaseAssetsPusher("RELEASE", "PATH")
    pusher.release = release
    assert pusher.version == "VERSION"
    assert not "version" in pusher.__dict__
