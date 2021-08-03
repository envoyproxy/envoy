import collections
import contextlib
from unittest.mock import AsyncMock, MagicMock, PropertyMock

import pytest

import aiodocker

from tools.base import checker
from tools.distribution import distrotest
from tools.docker import utils as docker_utils


# # DistroTestConfig

@pytest.mark.parametrize("config_path", [None, "CONFIG_PATH"])
def test_config_constructor(config_path):
    args = ("DOCKER", "PATH", "TARBALL", "KEYFILE", "TESTFILE", "MAINTAINER", "VERSION")
    if config_path is not None:
        args += (config_path, )
    config = distrotest.DistroTestConfig(*args)

    for k in args:
        if k in ["KEYFILE", "TESTFILE", "CONFIG_PATH"]:
            assert getattr(config, f"_{k.lower()}") == k
        else:
            assert getattr(config, k.lower()) == k

    assert config._config_path == config_path


def test_config_dunder_getitem(patches):
    config = distrotest.DistroTestConfig(
        "DOCKER", "KEYFILE", "PATH", "TARBALL", "TESTFILE", "MAINTAINER", "VERSION")
    patched = patches(
        ("DistroTestConfig.config", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.distrotest")

    with patched as (m_config, ):
        assert config.__getitem__("X") == m_config.return_value.__getitem__.return_value

    assert (
        list(m_config.return_value.__getitem__.call_args)
        == [('X',), {}])


# props

def test_config_config(patches):
    config = distrotest.DistroTestConfig(
        "DOCKER", "KEYFILE", "PATH", "TARBALL", "TESTFILE", "MAINTAINER", "VERSION")
    patched = patches(
        "utils",
        ("DistroTestConfig.config_path", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.distrotest")

    with patched as (m_utils, m_path):
        assert config.config == m_utils.from_yaml.return_value

    assert (
        list(m_utils.from_yaml.call_args)
        == [(m_path.return_value,), {}])
    assert "config" in config.__dict__


@pytest.mark.parametrize("config_path", [None, "CONFIG_PATH"])
def test_config_config_path(config_path):
    args = ("DOCKER", "PATH", "TARBALL", "KEYFILE", "TESTFILE", "MAINTAINER", "VERSION")
    if config_path is not None:
        args += (config_path, )
    config = distrotest.DistroTestConfig(*args)
    assert config.config_path == config_path or distrotest.DISTROTEST_CONFIG_PATH
    assert "config_path" in config.__dict__


def test_config_ctx_dockerfile():
    path = MagicMock()
    config = distrotest.DistroTestConfig(
        "DOCKER", path, "TARBALL", "KEYFILE", "TESTFILE", "MAINTAINER", "VERSION")

    assert config.ctx_dockerfile == path.joinpath.return_value
    assert (
        list(path.joinpath.call_args)
        == [('Dockerfile', ), {}])
    assert "ctx_dockerfile" in config.__dict__


def test_config_ctx_keyfile(patches):
    path = MagicMock()
    keyfile = MagicMock()
    config = distrotest.DistroTestConfig(
        "DOCKER", path, "TARBALL", keyfile, "TESTFILE", "MAINTAINER", "VERSION")

    assert config.ctx_keyfile == path.joinpath.return_value
    assert (
        list(path.joinpath.call_args)
        == [(keyfile.name, ), {}])
    assert "ctx_keyfile" in config.__dict__


def test_config_rel_ctx_packages():
    path = MagicMock()
    config = distrotest.DistroTestConfig(
        "DOCKER", path, "TARBALL", "KEYFILE", "TESTFILE", "MAINTAINER", "VERSION")

    assert config.rel_ctx_packages == path.joinpath.return_value
    assert (
        list(path.joinpath.call_args)
        == [(config.packages_name, ), {}])
    assert "rel_ctx_packages" in config.__dict__


def test_config_ctx_testfile():
    path = MagicMock()
    testfile = MagicMock()
    config = distrotest.DistroTestConfig(
        "DOCKER", path, "TARBALL", "KEYFILE", testfile, "MAINTAINER", "VERSION")

    assert config.ctx_testfile == path.joinpath.return_value

    assert (
        list(path.joinpath.call_args)
        == [(testfile.name, ), {}])
    assert "ctx_testfile" in config.__dict__


@pytest.mark.parametrize(
    "items",
    [{},
     {f"TYPE{i}": dict(ext="EXT", images=[f"IMAGE{i}{x}" for x in ["a", "b", "c"]]) for i in range(0, 5)}])
def test_config_images(patches, items):
    config = distrotest.DistroTestConfig(
        "DOCKER", "KEYFILE", "PATH", "TARBALL", "TESTFILE", "MAINTAINER", "VERSION")
    patched = patches(
        "DistroTestConfig.items",
        prefix="tools.distribution.distrotest")

    with patched as (m_items, ):
        m_items.return_value = items.items()
        result = config.images

    expected = {}

    for k, v in items.items():
        for image in v["images"]:
            expected[image] = dict(type=k, ext=v["ext"])
    assert result == expected
    assert "images" in config.__dict__


def test_config_install_img_path(patches):
    config = distrotest.DistroTestConfig(
        "DOCKER", "PATH", "TARBALL", "INSTALL", "INSTALL", "MAINTAINER", "VERSION")
    patched = patches(
        "pathlib",
        prefix="tools.distribution.distrotest")

    with patched as (m_plib, ):
        assert config.install_img_path == m_plib.PurePosixPath.return_value

    assert (
        list(m_plib.PurePosixPath.call_args)
        == [("/tmp/install",), {}])
    assert "install_img_path" in config.__dict__


def test_config_keyfile(patches):
    config = distrotest.DistroTestConfig(
        "DOCKER", "PATH", "TARBALL", "KEYFILE", "TESTFILE", "MAINTAINER", "VERSION")
    patched = patches(
        "shutil",
        ("DistroTestConfig.ctx_keyfile", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.distrotest")

    with patched as (m_shutil, m_key):
        assert config.keyfile == m_shutil.copyfile.return_value

    assert (
        list(m_shutil.copyfile.call_args)
        == [("KEYFILE", m_key.return_value), {}])
    assert "keyfile" in config.__dict__


def test_config_keyfile_img_path(patches):
    config = distrotest.DistroTestConfig(
        "DOCKER", "PATH", "TARBALL", "KEYFILE", "KEYFILE", "MAINTAINER", "VERSION")
    patched = patches(
        "pathlib",
        prefix="tools.distribution.distrotest")

    with patched as (m_plib, ):
        assert config.keyfile_img_path == m_plib.PurePosixPath.return_value

    assert (
        list(m_plib.PurePosixPath.call_args)
        == [("/tmp/gpg/signing.key",), {}])
    assert "keyfile_img_path" in config.__dict__


def test_config_packages_dir(patches):
    config = distrotest.DistroTestConfig(
        "DOCKER", "PATH", "TARBALL", "KEYFILE", "TESTFILE", "MAINTAINER", "VERSION")
    patched = patches(
        "utils",
        ("DistroTestConfig.rel_ctx_packages", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.distrotest")

    with patched as (m_utils, m_packages):
        assert config.packages_dir == m_utils.extract.return_value

    assert (
        list(m_utils.extract.call_args)
        == [(m_packages.return_value, "TARBALL"), {}])
    assert "packages_dir" in config.__dict__


def test_config_testfile(patches):
    config = distrotest.DistroTestConfig(
        "DOCKER", "TESTFILE", "PATH", "TARBALL", "TESTFILE", "MAINTAINER", "VERSION")
    patched = patches(
        "shutil",
        ("DistroTestConfig.ctx_testfile", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.distrotest")

    with patched as (m_shutil, m_key):
        assert config.testfile == m_shutil.copyfile.return_value

    assert (
        list(m_shutil.copyfile.call_args)
        == [("TESTFILE", m_key.return_value), {}])
    assert "testfile" in config.__dict__


def test_config_testfile_img_path(patches):
    config = distrotest.DistroTestConfig(
        "DOCKER", "PATH", "TARBALL", "KEYFILE", "TESTFILE", "MAINTAINER", "VERSION")
    patched = patches(
        "pathlib",
        ("DistroTestConfig.testfile", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.distrotest")

    with patched as (m_plib, m_name):
        assert config.testfile_img_path == m_plib.PurePosixPath.return_value.joinpath.return_value

    assert (
        list(m_plib.PurePosixPath.call_args)
        == [("/tmp",), {}])
    assert (
        list(m_plib.PurePosixPath.return_value.joinpath.call_args)
        == [(m_name.return_value.name,), {}])
    assert "testfile_img_path" in config.__dict__


# methods

def test_config_get_config(patches):
    config = distrotest.DistroTestConfig(
        "DOCKER", "PATH", "TARBALL", "KEYFILE", "TESTFILE", "MAINTAINER", "VERSION")
    patched = patches(
        "DistroTestConfig.get_image_name",
        ("DistroTestConfig.images", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.distrotest")

    with patched as (m_name, m_images):
        assert config.get_config("IMAGE") == m_images.return_value.__getitem__.return_value

    assert (
        list(m_images.return_value.__getitem__.call_args)
        == [(m_name.return_value, ), {}])
    assert (
        list(m_name.call_args)
        == [("IMAGE", ), {}])


def test_config_get_image_name():
    config = distrotest.DistroTestConfig(
        "DOCKER", "PATH", "TARBALL", "KEYFILE", "TESTFILE", "MAINTAINER", "VERSION")
    image = MagicMock()
    assert config.get_image_name(image) == image.split.return_value.__getitem__.return_value
    assert (
        list(image.split.call_args)
        == [(":", ), {}])
    assert (
        list(image.split.return_value.__getitem__.call_args)
        == [(0, ), {}])


@pytest.mark.parametrize("pkg_type", ["TYPE1", "TYPE2", "TYPE3"])
@pytest.mark.parametrize("pkg_types", [[], ["TYPE1", "TYPE2", "TYPE3"], ["TYPE3", "TYPE4"]])
def test_config_get_package_type(patches, pkg_type, pkg_types):
    config = distrotest.DistroTestConfig(
        "DOCKER", "PATH", "TARBALL", "KEYFILE", "TESTFILE", "MAINTAINER", "VERSION")
    patched = patches(
        "DistroTestConfig.get_image_name",
        "DistroTestConfig.items",
        prefix="tools.distribution.distrotest")

    with patched as (m_name, m_items):
        m_name.return_value = pkg_type
        m_items.return_value = [("X", dict(images=["TYPE6", "TYPE7"])), ("Y", dict(images=pkg_types))]
        if pkg_type in pkg_types:
            assert config.get_package_type("IMAGE") == "Y"
        else:

            with pytest.raises(distrotest.ConfigurationError) as e:
                config.get_package_type("IMAGE")

            assert e.value.args[0] == f"Unrecognized image: {pkg_type}"

    assert (
        list(m_name.call_args)
        == [("IMAGE", ), {}])
    assert (
        list(m_items.call_args)
        == [(), {}])


@pytest.mark.parametrize("pkg_type", ["TYPE1", "TYPE2"])
@pytest.mark.parametrize("ext", ["TYPE1", "TYPE2", "TYPE3", "TYPE4"])
@pytest.mark.parametrize("packages", [[], ["PACKAGE1", "PACKAGE2"]])
def test_config_get_packages(patches, pkg_type, ext, packages):
    config = distrotest.DistroTestConfig(
        "DOCKER", "PATH", "TARBALL", "KEYFILE", "TESTFILE", "MAINTAINER", "VERSION")
    patched = patches(
        ("DistroTestConfig.packages_dir", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.distrotest")

    with patched as (m_pkg, ):
        m_pkg.return_value.joinpath.return_value.glob.return_value = packages
        assert config.get_packages(pkg_type, ext) == packages

    assert (
        list(m_pkg.return_value.joinpath.call_args)
        == [(pkg_type,), {}])
    assert (
        list(m_pkg.return_value.joinpath.return_value.glob.call_args)
        == [(f'*.{ext}',), {}])


def test_config_items(patches):
    config = distrotest.DistroTestConfig(
        "DOCKER", "PATH", "TARBALL", "KEYFILE", "TESTFILE", "MAINTAINER", "VERSION")
    patched = patches(
        ("DistroTestConfig.config", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.distrotest")

    with patched as (m_config, ):
        assert config.items() == m_config.return_value.items.return_value

    assert (
        list(m_config.return_value.items.call_args)
        == [(), {}])


# # DistroTestImage

@pytest.mark.parametrize("stream", [None, "STREAM"])
def test_image_constructor(patches, stream):
    args = ("CONFIG", "BUILD_IMAGE", "NAME")
    if stream is not None:
        args += (stream, )
    image = distrotest.DistroTestImage(*args)
    assert image.test_config == "CONFIG"
    assert image.build_image == "BUILD_IMAGE"
    assert image.name == "NAME"
    assert image._stream == stream

    assert image.prefix == distrotest.DOCKER_IMAGE_PREFIX
    assert "prefix" not in image.__dict__
    assert image.dockerfile_template == distrotest.DOCKERFILE_TEMPLATE
    assert "dockerfile_template" not in image.__dict__


# props

def _check_image_config_property(patches, prop, arg=None):
    config = MagicMock()
    image = distrotest.DistroTestImage(config, "BUILD_IMAGE", "NAME")
    assert getattr(image, prop) == getattr(config, arg or prop)
    assert prop not in image.__dict__


@pytest.mark.parametrize(
    "prop",
    [("ctx_dockerfile",),
     ("docker",),
     ("install_img_path",),
     ("keyfile_img_path", ),
     ("keyfile", ),
     ("path", ),
     ("packages_name", ),
     ("testfile", ),
     ("testfile_img_path", )])
def test_image_config_props(patches, prop):
    _check_image_config_property(patches, *prop)


def test_image_build_command(patches):
    image = distrotest.DistroTestImage("CONFIG", "BUILD_IMAGE", "NAME")
    patched = patches(
        ("DistroTestImage.config", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.distrotest")

    with patched as (m_config, ):
        assert (
            image.build_command
            == m_config.return_value.__getitem__.return_value.__getitem__.return_value.strip.return_value.replace.return_value)

    assert (
        list(m_config.return_value.__getitem__.call_args)
        == [('build',), {}])
    assert (
        list(m_config.return_value.__getitem__.return_value.__getitem__.call_args)
        == [('command',), {}])
    assert (
        list(m_config.return_value.__getitem__.return_value.__getitem__.return_value.strip.call_args)
        == [(), {}])
    assert (
        list(m_config.return_value.__getitem__.return_value.__getitem__.return_value.strip.return_value.replace.call_args)
        == [("\n", " && "), {}])
    assert "build_command" not in image.__dict__


def test_image_config(patches):
    config = MagicMock()
    image = distrotest.DistroTestImage(config, "BUILD_IMAGE", "NAME")
    patched = patches(
        ("DistroTestImage.package_type", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.distrotest")

    with patched as (m_type, ):
        assert image.config == config.__getitem__.return_value

    assert (
        list(config.__getitem__.call_args)
        == [(m_type.return_value,), {}])
    assert "config" in image.__dict__


def test_image_ctx_install_dir(patches):
    image = distrotest.DistroTestImage("CONFIG", "BUILD_IMAGE", "NAME")
    patched = patches(
        "pathlib",
        ("DistroTestImage.package_type", dict(new_callable=PropertyMock)),
        ("DistroTestImage.packages_name", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.distrotest")

    with patched as (m_plib, m_type, m_name):
        assert image.ctx_install_dir == m_plib.Path.return_value.joinpath.return_value

    assert (
        list(m_plib.Path.call_args)
        == [(m_name.return_value, ), {}])
    assert (
        list(m_plib.Path.return_value.joinpath.call_args)
        == [(m_type.return_value, ), {}])
    assert "ctx_install_dir" not in image.__dict__


def test_image_dockerfile(patches):
    image = distrotest.DistroTestImage("CONFIG", "BUILD_IMAGE", "NAME")
    patched = patches(
        ("DistroTestImage.build_command", dict(new_callable=PropertyMock)),
        ("DistroTestImage.env", dict(new_callable=PropertyMock)),
        ("DistroTestImage.dockerfile_template", dict(new_callable=PropertyMock)),
        ("DistroTestImage.ctx_install_dir", dict(new_callable=PropertyMock)),
        ("DistroTestImage.keyfile", dict(new_callable=PropertyMock)),
        ("DistroTestImage.keyfile_img_path", dict(new_callable=PropertyMock)),
        ("DistroTestImage.install_img_path", dict(new_callable=PropertyMock)),
        ("DistroTestImage.testfile", dict(new_callable=PropertyMock)),
        ("DistroTestImage.testfile_img_path", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.distrotest")

    with patched as patchy:
        m_command, m_env, m_template, m_install, m_kfile, m_kpath, m_minstall, m_tfile, m_tpath = patchy
        assert image.dockerfile == m_template.return_value.format.return_value

    assert (
        list(m_template.return_value.format.call_args)
        == [(),
            {'build_image': 'BUILD_IMAGE',
             'install_dir': m_install.return_value,
             'env': m_env.return_value,
             'install_mount_path': m_minstall.return_value,
             'testfile_name': m_tfile.return_value.name,
             'test_mount_path': m_tpath.return_value,
             'build_command': m_command.return_value,
             'keyfile_name': m_kfile.return_value.name,
             'key_mount_path': m_kpath.return_value}])


@pytest.mark.parametrize("env", [None, "SOMENV=OTHER"])
def test_image_env(patches, env):
    image = distrotest.DistroTestImage("CONFIG", "BUILD_IMAGE", "NAME")
    patched = patches(
        ("DistroTestImage.config", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.distrotest")

    with patched as (m_config, ):
        m_config.return_value.__getitem__.return_value.get.return_value = env
        if env:
            assert image.env == f"ENV {env}"
        else:
            assert image.env == ""

    assert (
        list(m_config.return_value.__getitem__.call_args)
        == [("build", ), {}])
    assert (
        list(m_config.return_value.__getitem__.return_value.get.call_args)
        == [("env", ""), {}])
    assert "env" not in image.__dict__


def test_image_keyfile_package_type():
    config = MagicMock()
    image = distrotest.DistroTestImage(config, "BUILD_IMAGE", "NAME")
    assert image.package_type == config.get_package_type.return_value
    assert (
        list(config.get_package_type.call_args)
        == [("BUILD_IMAGE", ), {}])
    assert "package_type" in image.__dict__


def test_image_tag(patches):
    image = distrotest.DistroTestImage("CONFIG", "BUILD_IMAGE", "NAME")

    patched = patches(
        ("DistroTestImage.prefix", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.distrotest")

    with patched as (m_prefix, ):
        assert image.tag == f"{m_prefix.return_value}NAME:latest"

    assert "tag" in image.__dict__


# methods

def test_image_add_dockerfile(patches):
    stream = MagicMock()
    image = distrotest.DistroTestImage("CONFIG", "BUILD_IMAGE", "NAME", stream=stream)
    patched = patches(
        "shutil",
        ("DistroTestImage.dockerfile", dict(new_callable=PropertyMock)),
        ("DistroTestImage.ctx_dockerfile", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.distrotest")

    with patched as (m_shutil, m_dfile, m_ctx_docker):
        assert not image.add_dockerfile()
    assert (
        list(stream.call_args)
        == [(m_dfile.return_value,), {}])
    assert (
        list(m_ctx_docker.return_value.write_text.call_args)
        == [(m_dfile.return_value,), {}])


@pytest.mark.asyncio
@pytest.mark.parametrize("raises", [True, False])
async def test_image_build(patches, raises):
    image = distrotest.DistroTestImage("CONFIG", "BUILD_IMAGE", "NAME", "STREAM")
    patched = patches(
        "docker_utils.build_image",
        "DistroTestImage.add_dockerfile",
        "DistroTestImage.stream",
        ("DistroTestImage.docker", dict(new_callable=PropertyMock)),
        ("DistroTestImage.path", dict(new_callable=PropertyMock)),
        ("DistroTestImage.tag", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.distrotest")

    with patched as (m_build, m_add, m_stream, m_docker, m_path, m_tag):
        if raises:
            m_build.side_effect = docker_utils.BuildError("AN ERROR OCCURRED")

            with pytest.raises(distrotest.BuildError) as e:
                await image.build()

            assert (
                e.value.args
                == ('AN ERROR OCCURRED',))
        else:
            assert not await image.build()

    assert (
        list(m_add.call_args)
        == [(), {}])
    assert (
        list(m_build.call_args)
        == [(m_docker.return_value,
             m_path.return_value,
             m_tag.return_value),
            {'stream': m_stream, 'forcerm': True}])


@pytest.mark.asyncio
@pytest.mark.parametrize("tag", ["TAG1", "TAG2"])
@pytest.mark.parametrize(
    "images",
    [[],
     ["TAG1"],
     ["TAG1", "TAG2"],
     ["TAG3", "TAG4"]])
async def test_image_exists(patches, tag, images):
    image = distrotest.DistroTestImage("CONFIG", "BUILD_IMAGE", "NAME", "STREAM")
    patched = patches(
        ("DistroTestImage.images", dict(new_callable=AsyncMock)),
        ("DistroTestImage.tag", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.distrotest")

    with patched as (m_images, m_tag):
        m_images.return_value = images
        m_tag.return_value = tag
        if tag in images:
            assert await image.exists() is True
        else:
            assert await image.exists() is False


@pytest.mark.parametrize("items", range(0, 5))
def test_image_get_environment(patches, items):
    config = MagicMock()
    image = distrotest.DistroTestImage(config, "BUILD_IMAGE", "NAME", "STREAM")
    patched = patches(
        "dict",
        "DistroTestImage.get_install_binary",
        "DistroTestImage.installable_img_path",
        ("DistroTestImage.config", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.distrotest")

    class MockDict(collections.UserDict):

        def __init__(self, *args, **kwargs):
            super().__init__(*args, **kwargs)
            type(self).__setitem__ = MagicMock()

    with patched as (m_dict, m_get, m_path, m_config):
        _items = [[MagicMock(), MagicMock()]] * items
        _dict = MockDict((("A", "B"), ))
        m_dict.return_value = _dict
        m_config.return_value.__getitem__.return_value.items.return_value = _items
        assert image.get_environment("PKG_FNAME", "PKG_NAME", "DISTRO_NAME") == _dict

    assert (
        list(m_dict.call_args)
        == [(),
            {'ENVOY_MAINTAINER': config.maintainer,
             'ENVOY_VERSION': config.version,
             'ENVOY_INSTALL_BINARY': m_path.return_value,
             'ENVOY_INSTALLABLE': m_path.return_value,
             'PACKAGE': 'PKG_NAME',
             'DISTRO': 'DISTRO_NAME'}])
    assert (
        list(list(c) for c in m_path.call_args_list)
        == [[(m_get.return_value,), {}],
            [('PKG_FNAME',), {}]])
    assert (
        list(m_get.call_args)
        == [('PKG_FNAME',), {}])
    assert (
        list(m_config.return_value.__getitem__.call_args)
        == [('test',), {}])
    assert (
        list(m_config.return_value.__getitem__.return_value.items.call_args)
        == [(), {}])
    assert (
        list(list(c) for c in _dict.__setitem__.call_args_list)
        == [[(m_k.upper.return_value, m_v.format.return_value), {}] for m_k, m_v in _items])

    for m_k, m_v in _items:
        assert (
            list(m_k.upper.call_args)
            == [(), {}])
        assert (
            list(m_v.format.call_args)
            == [(), {'A': 'B'}])


@pytest.mark.parametrize("contains", [True, False])
def test_image_get_install_binary(patches, contains):
    image = distrotest.DistroTestImage("CONFIG", "BUILD_IMAGE", "NAME", "STREAM")
    patched = patches(
        "re",
        ("DistroTestImage.config", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.distrotest")


    with patched as (m_re, m_config):
        m_config.return_value.__contains__.return_value = contains
        assert (
            image.get_install_binary("PACKAGE")
            == (m_re.sub.return_value
                if contains
                else "PACKAGE"))

    if not contains:
        assert not m_re.sub.called
        assert not m_config.return_value.__getitem__.called
        return

    assert (
        list(list(c) for c in m_config.return_value.__getitem__.call_args_list)
        == [[('binary_name',), {}], [('binary_name',), {}]])
    assert (
        list(list(c) for c in m_config.return_value.__getitem__.return_value.__getitem__.call_args_list)
        == [[('match',), {}], [('replace',), {}]])
    assert (
        list(m_re.sub.call_args)
        == [(m_config.return_value.__getitem__.return_value.__getitem__.return_value,
             m_config.return_value.__getitem__.return_value.__getitem__.return_value,
             'PACKAGE'), {}])


@pytest.mark.asyncio
@pytest.mark.parametrize(
    "images",
    [[],
     [dict(RepoTags=["TAGA"])],
     [dict(RepoTags=[f"TAG{i}A", f"TAG{i}B"]) for i in range(1, 4)]])
async def test_image_images(patches, images):
    image = distrotest.DistroTestImage("CONFIG", "BUILD_IMAGE", "NAME", "STREAM")
    patched = patches(
        "chain",
        ("DistroTestImage.docker", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.distrotest")

    with patched as (m_chain, m_docker):
        m_docker.return_value.images.list = AsyncMock(return_value=images)
        assert await image.images() == m_chain.from_iterable.return_value

    expected = [image["RepoTags"] for image in images]
    assert (
        list(m_chain.from_iterable.call_args)
        == [(expected,), {}])


def test_image_installable_img_path(patches):
    image = distrotest.DistroTestImage("CONFIG", "BUILD_IMAGE", "NAME", "STREAM")
    patched = patches(
        ("DistroTestImage.install_img_path", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.distrotest")

    with patched as (m_dir, ):
        assert image.installable_img_path("INSTALLABLE") == m_dir.return_value.joinpath.return_value

    assert (
        list(m_dir.return_value.joinpath.call_args)
        == [("INSTALLABLE",), {}])


@pytest.mark.parametrize("stream", [True, None])
def test_image_stream(patches, stream):
    if stream:
        stream = MagicMock()
    image = distrotest.DistroTestImage("CONFIG", "BUILD_IMAGE", "NAME", stream)
    assert not image.stream("MESSAGE")
    if stream:
        assert (
            list(stream.call_args)
            == [("MESSAGE", ), {}])


# # DistroTest

def test_distrotest_constructor(patches):
    check = checker.AsyncChecker()
    dtest = distrotest.DistroTest(check, "CONFIG", "NAME", "IMAGE", "INSTALLABLE")
    assert dtest.checker == check
    assert dtest.test_config == "CONFIG"
    assert dtest.installable == "INSTALLABLE"
    assert dtest.distro == "NAME"
    assert dtest.build_image == "IMAGE"

    assert dtest.failures == []
    dtest._failures = ["FAIL"]
    assert dtest.failures == ["FAIL"]
    assert "failures" not in dtest.__dict__
    assert dtest.prefix == distrotest.DOCKER_CONTAINER_PREFIX
    assert "prefix" not in dtest.__dict__
    assert dtest.image_class == distrotest.DistroTestImage
    assert "image_class" not in dtest.__dict__


# props

def _check_distrotest_checker_property(prop, arg=None):
    check = AsyncMock()
    dtest = distrotest.DistroTest(check, "CONFIG", "NAME", "IMAGE", "INSTALLABLE")
    assert getattr(dtest, prop) == getattr(check, arg or prop)
    assert prop not in dtest.__dict__


@pytest.mark.parametrize(
    "prop",
    [("errors",),
     ("exiting",),
     ("log",),
     ("stdout",)])
def test_distrotest_checker_props(prop):
    _check_distrotest_checker_property(*prop)


def _check_distrotest_config_property(patches, prop, arg=None):
    check = AsyncMock()
    config = MagicMock()
    dtest = distrotest.DistroTest(check, config, "NAME", "IMAGE", "INSTALLABLE")
    assert getattr(dtest, prop) == getattr(config, arg or prop)
    assert prop not in dtest.__dict__


@pytest.mark.parametrize(
    "prop",
    [("docker",),
     ("testfile", )])
def test_distrotest_config_props(patches, prop):
    _check_distrotest_config_property(patches, *prop)


def test_distrotest_config(patches):
    check = checker.AsyncChecker()
    dtest = distrotest.DistroTest(check, "CONFIG", "NAME", "IMAGE", "INSTALLABLE")
    patched = patches(
        ("DistroTest.image", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.distrotest")

    with patched as (m_image, ):
        assert dtest.config == dict(Image=m_image.return_value.tag)


def test_distrotest_environment(patches):
    check = checker.AsyncChecker()
    installable = MagicMock()
    dtest = distrotest.DistroTest(check, "CONFIG", "NAME", "IMAGE", installable)
    patched = patches(
        ("DistroTest.image", dict(new_callable=PropertyMock)),
        ("DistroTest.package_name", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.distrotest")

    with patched as (m_image, m_name):
        assert dtest.environment == m_image.return_value.get_environment.return_value

    assert (
        list(m_image.return_value.get_environment.call_args)
        == [(installable.name, m_name.return_value, 'NAME'), {}])


@pytest.mark.parametrize("failures", [[], ["FAIL1", "FAIL2"]])
def test_distrotest_failed(patches, failures):
    check = checker.AsyncChecker()
    dtest = distrotest.DistroTest(check, "CONFIG", "NAME", "IMAGE", "INSTALLABLE")
    patched = patches(
        ("DistroTest.failures", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.distrotest")

    with patched as (m_failures, ):
        m_failures.return_value = failures
        assert dtest.failed == (len(failures) > 0)


def test_distrotest_image(patches):
    check = checker.AsyncChecker()
    dtest = distrotest.DistroTest(check, "CONFIG", "NAME", "IMAGE", "INSTALLABLE")
    patched = patches(
        "DistroTest.image_class",
        ("DistroTest.docker", dict(new_callable=PropertyMock)),
        ("DistroTest.stdout", dict(new_callable=PropertyMock)),
        ("DistroTest.testfile", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.distrotest")

    with patched as (m_class, m_docker, m_stdout, m_test):
        assert dtest.image == m_class.return_value

    assert (
        list(m_class.call_args)
        == [('CONFIG',
             'IMAGE',
             'NAME'),
            {'stream': m_stdout.return_value.info}])


def test_distrotest_name(patches):
    check = checker.AsyncChecker()
    dtest = distrotest.DistroTest(check, "CONFIG", "NAME", "IMAGE", "INSTALLABLE")
    patched = patches(
        ("DistroTest.prefix", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.distrotest")

    with patched as (m_prefix, ):
        assert dtest.name == f"{m_prefix.return_value}NAME"

    assert "name" in dtest.__dict__


def test_distrotest_package_name(patches):
    check = checker.AsyncChecker()
    installable = MagicMock()
    dtest = distrotest.DistroTest(check, "CONFIG", "NAME", "IMAGE", installable)
    assert dtest.package_name == installable.name.split.return_value.__getitem__.return_value
    assert (
        list(installable.name.split.call_args)
        == [('_',), {}])
    assert (
        list(installable.name.split.return_value.__getitem__.call_args)
        == [(0,), {}])


def test_distrotest_test_cmd(patches):
    check = checker.AsyncChecker()
    config = MagicMock()
    dtest = distrotest.DistroTest(check, config, "NAME", "IMAGE", "INSTALLABLE")
    assert dtest.test_cmd == (str(config.testfile_img_path), )


# methods

@pytest.mark.asyncio
@pytest.mark.parametrize("exists", [True, False])
async def test_distrotest_build(patches, exists):
    check = checker.AsyncChecker()
    dtest = distrotest.DistroTest(check, "CONFIG", "NAME", "IMAGE", "INSTALLABLE")
    patched = patches(
        "DistroTest.run_log",
        ("DistroTest.image", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.distrotest")

    with patched as (m_run, m_image):
        m_image.return_value.exists = AsyncMock(return_value=exists)
        m_image.return_value.build = AsyncMock()
        assert not await dtest.build()

    assert (
        list(m_image.return_value.exists.call_args)
        == [(), {}])

    if exists:
        assert not m_image.return_value.build.called
        assert not m_run.called
        return

    assert (
        list(m_image.return_value.build.call_args)
        == [(), {}])
    assert (
        list(list(c) for c in m_run.call_args_list)
        == [[('Building image',), {'msg_type': 'notice'}],
            [('Image built',), {}]])


@pytest.mark.asyncio
@pytest.mark.parametrize("raises", [True, False])
async def test_distrotest_cleanup(patches, raises):
    check = checker.AsyncChecker()
    dtest = distrotest.DistroTest(check, "CONFIG", "NAME", "IMAGE", "INSTALLABLE")
    patched = patches(
        ("DistroTest.docker", dict(new_callable=AsyncMock)),
        ("DistroTest.stop", dict(new_callable=AsyncMock)),
        ("DistroTest.name", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.distrotest")

    class SomeError(Exception):
        pass

    with patched as (m_docker, m_stop, m_name):
        if raises:
            m_stop.side_effect = SomeError("AN ERROR OCCURRED")
        assert not await dtest.cleanup()

    assert (
        list(m_docker.containers.get.call_args)
        == [(m_name.return_value,), {}])
    assert (
        list(m_stop.call_args)
        == [(m_docker.containers.get.return_value,), {}])


@pytest.mark.asyncio
async def test_distrotest_create(patches):
    check = checker.AsyncChecker()
    dtest = distrotest.DistroTest(check, "CONFIG", "NAME", "IMAGE", "INSTALLABLE")
    patched = patches(
        ("DistroTest.docker", dict(new_callable=AsyncMock)),
        ("DistroTest.config", dict(new_callable=PropertyMock)),
        ("DistroTest.name", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.distrotest")

    with patched as (m_docker, m_config, m_name):
        assert await dtest.create() == m_docker.containers.create_or_replace.return_value

    assert (
        list(m_docker.containers.create_or_replace.call_args)
        == [(),
            {'config': m_config.return_value,
             'name': m_name.return_value}])


@pytest.mark.asyncio
@pytest.mark.parametrize("failed", [True, False])
@pytest.mark.parametrize("returns", [0, 1])
@pytest.mark.parametrize("msgs", range(0, 5))
async def test_distrotest_exec(patches, failed, returns, msgs):
    check = checker.AsyncChecker()
    dtest = distrotest.DistroTest(check, "CONFIG", "NAME", "IMAGE", "INSTALLABLE")
    patched = patches(
        "DistroTest.error",
        "DistroTest.handle_test_output",
        ("DistroTest.environment", dict(new_callable=PropertyMock)),
        ("DistroTest.failed", dict(new_callable=PropertyMock)),
        ("DistroTest.test_cmd", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.distrotest")
    container = AsyncMock()

    class Tracker(object):
        counter = 0
        _outs = []

        async def _out(self):
            self.counter += 1
            _mock = MagicMock()
            if msgs >= self.counter:
                self._outs.append(_mock)
                return _mock
            return ""

        @contextlib.asynccontextmanager
        async def _start(self, *args, **kwargs):
            self.stream = AsyncMock()
            await self.stream(*args, **kwargs)
            self.stream.read_out.side_effect = self._out
            yield self.stream

    _tracker = Tracker()
    container.exec.return_value.start = _tracker._start
    container.exec.return_value.inspect.return_value = dict(ExitCode=returns)

    with patched as (m_error, m_out, m_env, m_failed, m_cmd):
        m_failed.return_value = failed
        assert not await dtest.exec(container)

    assert (
        list(container.exec.call_args)
        == [(m_cmd.return_value,),
            {'environment': m_env.return_value}])
    assert (
        list(_tracker.stream.call_args)
        == [(), {'detach': False}])
    assert (
        list(container.exec.return_value.inspect.call_args)
        == [(), {}])

    assert _tracker.counter == msgs + 1
    assert len(_tracker._outs) == msgs

    for _out in _tracker._outs:
        assert (
            list(_out.data.decode.call_args)
            == [('utf-8',), {}])
        assert (
            list(_out.data.decode.return_value.strip.call_args)
            == [(), {}])

    _log_error = (msgs > 0) and (returns and not failed)

    if _log_error:
        assert dtest._failures == ['container-start']
        assert (
            list(m_error.call_args)
            == [([f"[NAME] Error executing test in container\n{_tracker._outs[-1].data.decode.return_value.strip.return_value}"],), {}])
        assert (
            list(list(c) for c in m_out.call_args_list)
            == [[(_out.data.decode.return_value.strip.return_value,), {}] for _out in _tracker._outs[:-1]])
    else:
        assert dtest._failures == []
        assert not m_error.called
        assert (
            list(list(c) for c in m_out.call_args_list)
            == [[(_out.data.decode.return_value.strip.return_value,), {}] for _out in _tracker._outs])


def test_distrotest_error():
    check = MagicMock()
    dtest = distrotest.DistroTest(check, "CONFIG", "NAME", "IMAGE", "INSTALLABLE")
    assert dtest.error(["ERR1", "ERR2"]) == check.error.return_value
    assert (
        list(check.error.call_args)
        == [(check.active_check, ['ERR1', 'ERR2']), {}])


@pytest.mark.parametrize("msg", ["MESSAGE", "MESSAGE\nEXTRA", "MESSAGE\nEXTRA\nMORE"])
def test_distrotest_handle_test_error(patches, msg):
    check = checker.AsyncChecker()
    dtest = distrotest.DistroTest(check, "CONFIG", "NAME", "IMAGE", "INSTALLABLE")
    patched = patches(
        "DistroTest.error",
        ("DistroTest.stdout", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.distrotest")
    _msg = MagicMock()
    _splitter = MagicMock()

    def _split(splitter, *args):
        if splitter == "\n":
            return msg.split("\n", *args)
        return _splitter

    with patched as (m_error, m_stdout):
        _msg.split.side_effect = _split
        _splitter.__getitem__.return_value.strip.return_value.split.return_value = (
            "TESTRUN", "TESTNAME")
        assert not dtest.handle_test_error(_msg)

    assert (
        list(list(c) for c in _msg.split.call_args_list)
        == [[(']',), {}], [('\n', 1), {}]])
    assert (
        list(_splitter.__getitem__.call_args)
        == [(0,), {}])
    assert (
        list(_splitter.__getitem__.return_value.strip.call_args)
        == [('[',), {}])
    assert (
        list(_splitter.__getitem__.return_value.strip.return_value.split.call_args)
        == [(':',), {}])

    assert dtest._failures == ['TESTNAME']
    assert (
        list(m_error.call_args)
        == [(['[TESTRUN:TESTNAME] Test failed'],), {}])

    if len(msg.split("\n")) > 1:
        assert (
            list(m_stdout.return_value.error.call_args)
            == [(msg.split("\n", 1)[1],), {}])
        return

    assert not m_stdout.called


@pytest.mark.asyncio
@pytest.mark.parametrize(
    "start",
    ["NAME", "[NAME", "[NOME", "x[NAME", "[NAME]", "ERROR"])
@pytest.mark.parametrize(
    "msg",
    ["",
     "foo",
     " bar",
     "NAME",
     "fooERROR",
     "ERRORfoo",
     "ERROR foo",
     " fooERROR",
     " ERRORfoo",
     " ERROR foo",
     "\nERROR foo",
     " ERROR\nfoo",
     "OTHER\nfoo"])
async def test_distrotest_handle_test_output(patches, start, msg):
    check = checker.AsyncChecker()
    dtest = distrotest.DistroTest(check, "CONFIG", "NAME", "IMAGE", "INSTALLABLE")
    patched = patches(
        "DistroTest.handle_test_error",
        ("DistroTest.stdout", dict(new_callable=PropertyMock)),
        ("DistroTest.log", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.distrotest")
    _msg = f"{start}{msg}"

    with patched as (m_error, m_stdout, m_log):
        assert not dtest.handle_test_output(_msg)

    if not _msg.startswith("[NAME") and "\n" in _msg:
        _parts = _msg.split("\n", 1)
        assert (
            list(m_stdout.return_value.info.call_args_list[0])
            == [(_parts[0],), {}])
        _msg = _parts[1]

    if not start.startswith("[NAME"):
        assert (
            list(m_stdout.return_value.info.call_args)
            == [(_msg,), {}])
        assert not m_error.called
        assert not m_log.called
        return

    assert not m_stdout.called

    if "ERROR" not in msg:
        assert (
            list(m_log.return_value.info.call_args)
            == [(_msg,), {}])
        assert not m_error.called
        return

    assert not m_log.called
    assert (
        list(m_error.call_args)
        == [(_msg,), {}])


@pytest.mark.parametrize("failed", [True, False])
@pytest.mark.parametrize("failures", [[], ["FAIL1"], ["FAIL1", "FAIL2"]])
def test_distrotest_log_failures(patches, failed, failures):
    check = checker.AsyncChecker()
    dtest = distrotest.DistroTest(check, "CONFIG", "NAME", "IMAGE", "INSTALLABLE")
    patched = patches(
        "DistroTest.run_log",
        ("DistroTest.failed", dict(new_callable=PropertyMock)),
        ("DistroTest.failures", dict(new_callable=PropertyMock)),
        ("DistroTest.package_name", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.distrotest")

    with patched as (m_log, m_failed, m_failures, m_name):
        m_failed.return_value = failed
        m_failures.return_value = failures
        assert not dtest.log_failures()

    if not failed:
        assert not m_log.called
        return

    assert (
        list(m_log.call_args)
        == [(f'Package test had failures: {",".join(failures)}',),
            {'msg_type': 'error', 'test': m_name.return_value}])


@pytest.mark.asyncio
async def test_distrotest_logs():
    check = checker.AsyncChecker()
    dtest = distrotest.DistroTest(check, "CONFIG", "NAME", "IMAGE", "INSTALLABLE")
    container = AsyncMock()
    _logs = ["LOG1", "LOG2", "LOG3"]
    container.log.return_value = _logs
    assert await dtest.logs(container) == "".join(_logs)
    assert (
        list(container.log.call_args)
        == [(), {'stdout': True, 'stderr': True}])


@pytest.mark.asyncio
@pytest.mark.parametrize("failed", [True, False])
@pytest.mark.parametrize("self_failed", [True, False])
async def test_distrotest_on_test_complete(patches, failed, self_failed):
    check = MagicMock()
    dtest = distrotest.DistroTest(check, "CONFIG", "NAME", "IMAGE", "INSTALLABLE")
    patched = patches(
        "DistroTest.log_failures",
        "DistroTest.run_message",
        ("DistroTest.stop", dict(new_callable=AsyncMock)),
        ("DistroTest.failed", dict(new_callable=PropertyMock)),
        ("DistroTest.package_name", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.distrotest")

    with patched as (m_log, m_msg, m_stop, m_failed, m_name):
        m_failed.return_value = self_failed
        assert not await dtest.on_test_complete("CONTAINER", failed)

    assert (
        list(m_log.call_args)
        == [(), {}])
    assert (
        list(m_stop.call_args)
        == [('CONTAINER',), {}])

    if failed or self_failed:
        assert not check.succeed.called
        assert not m_msg.called
        return

    assert (
        list(m_msg.call_args)
        == [('Package test passed',),
            {'test': m_name.return_value}])
    assert (
        list(check.succeed.call_args)
        == [(check.active_check, [m_msg.return_value]), {}])


@pytest.mark.asyncio
async def test_distrotest_run(patches):
    check = checker.AsyncChecker()
    dtest = distrotest.DistroTest(check, "CONFIG", "NAME", "IMAGE", "INSTALLABLE")
    patched = patches(
        "DistroTest.error",
        ("DistroTest._run", dict(new_callable=AsyncMock)),
        prefix="tools.distribution.distrotest")

    with patched as (m_error, m_run):
        assert not await dtest.run()

    assert (
        list(m_run.call_args)
        == [(), {}])
    assert (
        list(m_error.call_args)
        == [(m_run.return_value,), {}])


@pytest.mark.parametrize("msg_type", [None, "MSG_TYPE"])
@pytest.mark.parametrize("testname", [None, "TEST"])
def test_distrotest_run_log(patches, msg_type, testname):
    check = checker.AsyncChecker()
    dtest = distrotest.DistroTest(check, "CONFIG", "NAME", "IMAGE", "INSTALLABLE")
    patched = patches(
        "getattr",
        "DistroTest.run_message",
        ("DistroTest.log", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.distrotest")
    args = ["MESSAGE"]
    if msg_type:
        args.append(msg_type)
    kwargs = {}
    if testname:
        kwargs["test"] = testname

    with patched as (m_get, m_msg, m_log):
        assert not dtest.run_log(*args, **kwargs)

    assert (
        list(m_get.call_args)
        == [(m_log.return_value, msg_type or 'info'), {}])
    assert (
        list(m_get.return_value.call_args)
        == [(m_msg.return_value,), {}])
    assert (
        list(m_msg.call_args)
        == [('MESSAGE',), {'test': testname}])


@pytest.mark.parametrize("testname", [None, "TEST"])
def test_distrotest_run_message(patches, testname):
    check = checker.AsyncChecker()
    dtest = distrotest.DistroTest(check, "CONFIG", "NAME", "IMAGE", "INSTALLABLE")
    if testname:
        assert dtest.run_message("MESSAGE", testname) == f"[NAME/{testname}] MESSAGE"
    else:
        assert dtest.run_message("MESSAGE") == f"[NAME] MESSAGE"


@pytest.mark.asyncio
@pytest.mark.parametrize("running", [True, False])
async def test_distrotest_start(patches, running):
    check = checker.AsyncChecker()
    dtest = distrotest.DistroTest(check, "CONFIG", "NAME", "IMAGE", "INSTALLABLE")
    patched = patches(
        "DistroTest.run_log",
        "DistroTest.run_message",
        ("DistroTest.create", dict(new_callable=AsyncMock)),
        ("DistroTest.logs", dict(new_callable=AsyncMock)),
        ("DistroTest.package_name", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.distrotest")

    with patched as (m_log, m_msg, m_create, m_logs, m_name):
        m_create.return_value.show.return_value.__getitem__.return_value.__getitem__.return_value = running

        if running:
            assert await dtest.start() == m_create.return_value
        else:

            with pytest.raises(distrotest.ContainerError) as e:
                await dtest.start()

            assert e.value.args[0] == m_msg.return_value

    assert (
        list(m_create.call_args)
        == [(), {}])
    assert (
        list(m_create.return_value.start.call_args)
        == [(), {}])
    assert (
        list(m_create.return_value.show.call_args)
        == [(), {}])

    if not running:
        assert not m_log.called
        assert (
            list(m_msg.call_args)
            == [(f"Container unable to start\n{m_logs.return_value}",),
                {'test': m_name.return_value}])
        assert (
            list(m_logs.call_args)
            == [(m_create.return_value,), {}])
        return

    assert (
        list(m_log.call_args)
        == [('Container started',),
            {'test': m_name.return_value}])
    assert not m_msg.called
    assert not m_logs.called


@pytest.mark.asyncio
@pytest.mark.parametrize("container", [None, "CONTAINER"])
async def test_distrotest_stop(patches, container):
    check = checker.AsyncChecker()
    dtest = distrotest.DistroTest(check, "CONFIG", "NAME", "IMAGE", "INSTALLABLE")
    patched = patches(
        "DistroTest.run_log",
        ("DistroTest.package_name", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.distrotest")

    if container:
        container = AsyncMock()

    with patched as (m_log, m_pkg):
        assert not await dtest.stop(container)

    if not container:
        assert not m_log.called
        return

    assert (
        list(container.kill.call_args)
        == [(), {}])
    assert (
        list(container.delete.call_args)
        == [(), {}])
    assert (
        list(m_log.call_args)
        == [('Container stopped',), {'test': m_pkg.return_value}])


@pytest.mark.asyncio
@pytest.mark.parametrize("build_raises", [None, distrotest.ConfigurationError, distrotest.BuildError, aiodocker.exceptions.DockerError, Exception])
@pytest.mark.parametrize("start_raises", [None, distrotest.ContainerError, aiodocker.exceptions.DockerError, Exception])
@pytest.mark.parametrize("exec_raises", [None, aiodocker.exceptions.DockerError, Exception])
@pytest.mark.parametrize("stop_raises", [None, aiodocker.exceptions.DockerError, Exception])
async def test_distrotest__run(patches, build_raises, start_raises, exec_raises, stop_raises):
    check = checker.AsyncChecker()
    dtest = distrotest.DistroTest(check, "CONFIG", "NAME", "IMAGE", "INSTALLABLE")
    patched = patches(
        ("DistroTest.build", dict(new_callable=AsyncMock)),
        ("DistroTest.start", dict(new_callable=AsyncMock)),
        ("DistroTest.exec", dict(new_callable=AsyncMock)),
        ("DistroTest.on_test_complete", dict(new_callable=AsyncMock)),
        prefix="tools.distribution.distrotest")

    with patched as (m_build, m_start, m_exec, m_stop):
        if build_raises:
            if build_raises == aiodocker.exceptions.DockerError:
                m_build.side_effect = build_raises("ARG1", dict(message="AN ERROR OCCURRED"))
            else:
                m_build.side_effect = build_raises("AN ERROR OCCURRED")
        if start_raises:
            if start_raises == aiodocker.exceptions.DockerError:
                m_start.side_effect = start_raises("ARG1", dict(message="AN ERROR OCCURRED"))
            else:
                m_start.side_effect = start_raises("AN ERROR OCCURRED")
        if exec_raises:
            if exec_raises == aiodocker.exceptions.DockerError:
                m_exec.side_effect = exec_raises("ARG1", dict(message="AN ERROR OCCURRED"))
            else:
                m_exec.side_effect = exec_raises("AN ERROR OCCURRED")
        if stop_raises:
            if stop_raises == aiodocker.exceptions.DockerError:
                m_stop.side_effect = stop_raises("ARG1", dict(message="AN ERROR OCCURRED"))
            else:
                m_stop.side_effect = stop_raises("AN ERROR OCCURRED")

        should_fail = (
            build_raises == Exception
            or not build_raises and start_raises == Exception
            or not (build_raises or start_raises) and exec_raises == Exception
            or stop_raises == Exception)

        if should_fail:
            with pytest.raises(Exception):
                await dtest._run()
        else:
            result = await dtest._run()

    assert (
        list(m_build.call_args)
        == [(), {}])

    if build_raises or start_raises:
        assert (
            list(m_stop.call_args)
            == [(None, True), {}])
    elif exec_raises:
        assert (
            list(m_stop.call_args)
            == [(m_start.return_value, True), {}])
    else:
        assert (
            list(m_stop.call_args)
            == [(m_start.return_value, False), {}])

    if build_raises:
        assert not m_start.called
        assert not m_exec.called
        if not should_fail:
            assert result == ('AN ERROR OCCURRED',)
        return

    assert (
        list(m_start.call_args)
        == [(), {}])

    if start_raises:
        assert not m_exec.called
        if not should_fail:
            assert result == ('AN ERROR OCCURRED',)
        return

    assert (
        list(m_exec.call_args)
        == [(m_start.return_value,), {}])

    if exec_raises or stop_raises:
        if not should_fail:
            assert result == ('AN ERROR OCCURRED',)
        return

    assert not result
