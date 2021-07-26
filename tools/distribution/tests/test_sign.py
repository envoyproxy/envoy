import types
from unittest.mock import MagicMock, PropertyMock

import pytest

from tools.base import runner
from tools.distribution import sign
from tools.gpg import identity


# DirectorySigningUtil

@pytest.mark.parametrize("command", ["", None, "COMMAND", "OTHERCOMMAND"])
def test_util_constructor(command):
    packager = sign.PackageSigningRunner("x", "y", "z")
    maintainer = identity.GPGIdentity(packager)
    args = ("PATH", maintainer, "LOG")
    if command is not None:
        args += (command, )
    util = sign.DirectorySigningUtil(*args)
    assert util.path == "PATH"
    assert util.maintainer == maintainer
    assert util.log == "LOG"
    assert util._command == (command or "")
    assert util.command_args == ()


@pytest.mark.parametrize("command_name", ["", None, "CMD", "OTHERCMD"])
@pytest.mark.parametrize("command", ["", None, "COMMAND", "OTHERCOMMAND"])
@pytest.mark.parametrize("which", ["", None, "PATH", "OTHERPATH"])
def test_util_command(patches, command_name, command, which):
    packager = sign.PackageSigningRunner("x", "y", "z")
    maintainer = identity.GPGIdentity(packager)
    util = sign.DirectorySigningUtil("PATH", maintainer, "LOG", command=command)
    patched = patches(
        "shutil",
        ("DirectorySigningUtil.package_type", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.sign")
    if command_name is not None:
        util.command_name = command_name

    with patched as (m_shutil, m_type):
        m_shutil.which.return_value = which

        if not which and not command:
            with pytest.raises(sign.SigningError) as e:
                util.command

            assert (
                list(m_shutil.which.call_args)
                == [(command_name,), {}])
            assert (
                e.value.args[0]
                == f"Signing software missing ({m_type.return_value}): {command_name}")
            return

        result = util.command

    assert "command" in util.__dict__
    assert not m_type.called

    if command:
        assert not m_shutil.which.called
        assert result == command
        return

    assert (
        list(m_shutil.which.call_args)
        == [(command_name,), {}])
    assert result == m_shutil.which.return_value


def test_util_sign(patches):
    packager = sign.PackageSigningRunner("x", "y", "z")
    maintainer = identity.GPGIdentity(packager)
    util = sign.DirectorySigningUtil("PATH", maintainer, "LOG")
    patched = patches(
        "DirectorySigningUtil.sign_pkg",
        ("DirectorySigningUtil.pkg_files", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.sign")

    with patched as (m_sign, m_pkgs):
        m_pkgs.return_value = ("PKG1", "PKG2", "PKG3")
        assert not util.sign()

    assert (
        list(list(c) for c in m_sign.call_args_list)
        == [[('PKG1',), {}],
            [('PKG2',), {}],
            [('PKG3',), {}]])


def test_util_sign_command(patches):
    packager = sign.PackageSigningRunner("x", "y", "z")
    maintainer = identity.GPGIdentity(packager)
    util = sign.DirectorySigningUtil("PATH", maintainer, "LOG")
    patched = patches(
        ("DirectorySigningUtil.command", dict(new_callable=PropertyMock)),
        ("DirectorySigningUtil.command_args", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.sign")

    with patched as (m_command, m_args):
        m_args.return_value = ("ARG1", "ARG2", "ARG3")
        assert (
            util.sign_command("PACKAGE")
            == (m_command.return_value, ) + m_args.return_value + ("PACKAGE", ))


@pytest.mark.parametrize("returncode", [0, 1])
def test_util_sign_pkg(patches, returncode):
    packager = sign.PackageSigningRunner("x", "y", "z")
    maintainer = identity.GPGIdentity(packager)
    util = sign.DirectorySigningUtil("PATH", maintainer, "LOG")
    patched = patches(
        "os",
        "subprocess",
        "DirectorySigningUtil.sign_command",
        ("PackageSigningRunner.log", dict(new_callable=PropertyMock)),
        ("DirectorySigningUtil.package_type", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.sign")

    util.log = MagicMock()

    with patched as (m_os, m_subproc, m_command, m_log, m_type):
        m_subproc.run.return_value.returncode = returncode
        if returncode:
            with pytest.raises(sign.SigningError) as e:
                util.sign_pkg("PACKAGE")
        else:
            assert not util.sign_pkg("PACKAGE")

    assert (
        list(m_os.path.basename.call_args)
        == [('PACKAGE',), {}])
    assert (
        list(util.log.notice.call_args)
        == [(f"Sign package ({m_type.return_value}): {m_os.path.basename.return_value}",), {}])
    assert (
        list(m_command.call_args)
        == [('PACKAGE',), {}])
    assert (
        list(m_subproc.run.call_args)
        == [(m_command.return_value,),
            {'capture_output': True,
             'encoding': 'utf-8'}])

    if not returncode:
        assert (
            list(util.log.success.call_args)
            == [(f"Signed package ({m_type.return_value}): {m_os.path.basename.return_value}",), {}])
        return
    assert e.value.args[0] == m_subproc.run.return_value.stdout + m_subproc.run.return_value.stderr


@pytest.mark.parametrize("ext", ["EXT1", "EXT2"])
@pytest.mark.parametrize("package_type", [None, "", "TYPE1", "TYPE2"])
def test_util_package_type(ext, package_type):
    packager = sign.PackageSigningRunner("x", "y", "z")
    maintainer = identity.GPGIdentity(packager)
    util = sign.DirectorySigningUtil("PATH", maintainer, "LOG")
    util.ext = ext
    util._package_type = package_type
    assert util.package_type == package_type or ext


@pytest.mark.parametrize(
    "files",
    [[],
     ["abc", "xyz"],
     ["abc.EXT", "xyz.EXT", "abc.FOO", "abc.BAR"],
     ["abc.NOTEXT", "xyz.NOTEXT"]])
def test_util_pkg_files(patches, files):
    packager = sign.PackageSigningRunner("x", "y", "z")
    maintainer = identity.GPGIdentity(packager)
    util = sign.DirectorySigningUtil("PATH", maintainer, "LOG")
    patched = patches(
        "os",
        ("DirectorySigningUtil.ext", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.sign")
    with patched as (m_os, m_ext):
        m_ext.return_value = "EXT"
        m_os.listdir.return_value = files
        result = util.pkg_files

    expected = [fname for fname in files if fname.endswith(".EXT")]

    assert (
        list(m_os.listdir.call_args)
        == [("PATH",), {}])
    if not expected:
        assert not m_os.path.join.called
        assert not result
    else:
        assert (
            result
            == tuple(
                m_os.path.join.return_value
                for fname in expected))
        assert (
            list(list(c) for c in m_os.path.join.call_args_list)
            == [[("PATH", fname), {}]
                for fname in expected])

    assert "pkg_files" not in util.__dict__


# PackageSigningRunner

def test_packager_constructor():
    packager = sign.PackageSigningRunner("x", "y", "z")
    assert isinstance(packager, runner.Runner)
    assert packager.maintainer_class == identity.GPGIdentity
    assert packager._signing_utils == ()


def test_packager_cls_register_util():
    assert sign.PackageSigningRunner._signing_utils == ()

    class Util1(object):
        pass

    class Util2(object):
        pass

    sign.PackageSigningRunner.register_util("util1", Util1)
    assert (
        sign.PackageSigningRunner._signing_utils
        == (('util1', Util1),))

    sign.PackageSigningRunner.register_util("util2", Util2)
    assert (
        sign.PackageSigningRunner._signing_utils
        == (('util1', Util1),
            ('util2', Util2),))


def test_packager_extract(patches):
    packager = sign.PackageSigningRunner("x", "y", "z")
    patched = patches(
        ("PackageSigningRunner.args", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.sign")

    with patched as (m_args, ):
        assert packager.extract == m_args.return_value.extract

    assert "extract" not in packager.__dict__


def test_packager_maintainer(patches):
    packager = sign.PackageSigningRunner("x", "y", "z")
    patched = patches(
        ("PackageSigningRunner.log", dict(new_callable=PropertyMock)),
        ("PackageSigningRunner.maintainer_class", dict(new_callable=PropertyMock)),
        ("PackageSigningRunner.maintainer_email", dict(new_callable=PropertyMock)),
        ("PackageSigningRunner.maintainer_name", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.sign")

    with patched as (m_log, m_class, m_email, m_name):
        assert packager.maintainer == m_class.return_value.return_value

    assert (
        list(m_class.return_value.call_args)
        == [(m_name.return_value, m_email.return_value, m_log.return_value), {}])

    assert "maintainer" in packager.__dict__


def test_packager_maintainer_email(patches):
    packager = sign.PackageSigningRunner("x", "y", "z")
    patched = patches(
        ("PackageSigningRunner.args", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.sign")

    with patched as (m_args, ):
        assert packager.maintainer_email == m_args.return_value.maintainer_email

    assert "maintainer_email" not in packager.__dict__


def test_packager_maintainer_name(patches):
    packager = sign.PackageSigningRunner("x", "y", "z")

    patched = patches(
        ("PackageSigningRunner.args", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.sign")

    with patched as (m_args, ):
        assert packager.maintainer_name == m_args.return_value.maintainer_name

    assert "maintainer_name" not in packager.__dict__


def test_packager_package_type(patches):
    packager = sign.PackageSigningRunner("x", "y", "z")

    patched = patches(
        ("PackageSigningRunner.args", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.sign")

    with patched as (m_args, ):
        assert packager.package_type == m_args.return_value.package_type

    assert "package_type" not in packager.__dict__


def test_packager_path(patches):
    packager = sign.PackageSigningRunner("x", "y", "z")

    patched = patches(
        ("PackageSigningRunner.args", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.sign")

    with patched as (m_args, ):
        assert packager.path == m_args.return_value.path

    assert "path" not in packager.__dict__


def test_packager_tar(patches):
    packager = sign.PackageSigningRunner("x", "y", "z")
    patched = patches(
        ("PackageSigningRunner.args", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.sign")

    with patched as (m_args, ):
        assert packager.tar == m_args.return_value.tar

    assert "tar" not in packager.__dict__


def test_packager_signing_utils():
    packager = sign.PackageSigningRunner("x", "y", "z")
    _utils = (("NAME1", "UTIL1"), ("NAME2", "UTIL2"))
    packager._signing_utils = _utils
    assert packager.signing_utils == dict(_utils)


def test_packager_add_arguments():
    packager = sign.PackageSigningRunner("x", "y", "z")
    parser = MagicMock()
    packager.add_arguments(parser)
    assert (
        list(list(c) for c in parser.add_argument.call_args_list)
        == [[('--log-level', '-l'),
             {'choices': ['debug', 'info', 'warn', 'error'],
              'default': 'info',
              'help': 'Log level to display'}],
            [('path',),
             {'default': '',
              'help': 'Path to the directory containing packages to sign'}],
            [('--extract',),
             {'action': 'store_true',
              'help': 'If set, treat the path as a tarball containing directories '
              'according to package_type'}],
            [('--tar',),
             {'help': 'Path to save the signed packages as tar file'}],
            [('--type',),
             {'choices': ['util1', 'util2', ''],
              'default': '',
              'help': 'Package type to sign'}],
            [('--maintainer-name',),
             {'default': '', 'help': 'Maintainer name to match when searching for a GPG key to match with'}],
            [('--maintainer-email',),
             {'default': '',
              'help': 'Maintainer email to match when searching for a GPG key to match with'}]])


def test_packager_archive(patches):
    packager = sign.PackageSigningRunner("x", "y", "z")
    patched = patches(
        "tarfile",
        ("PackageSigningRunner.tar", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.sign")

    with patched as (m_tarfile, m_tar):
        assert not packager.archive("PATH")

    assert (
        list(m_tarfile.open.call_args)
        == [(m_tar.return_value, 'w'), {}])
    assert (
        list(m_tarfile.open.return_value.__enter__.return_value.add.call_args)
        == [('PATH',), {'arcname': '.'}])


def test_packager_get_signing_util(patches):
    packager = sign.PackageSigningRunner("x", "y", "z")
    patched = patches(
        ("PackageSigningRunner.log", dict(new_callable=PropertyMock)),
        ("PackageSigningRunner.maintainer", dict(new_callable=PropertyMock)),
        ("PackageSigningRunner.signing_utils", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.sign")

    with patched as (m_log, m_maintainer, m_utils):
        assert packager.get_signing_util("UTIL", "PATH") == m_utils.return_value.__getitem__.return_value.return_value

    assert (
        list(m_utils.return_value.__getitem__.call_args)
        == [("UTIL",), {}])
    assert (
        list(m_utils.return_value.__getitem__.return_value.call_args)
        == [("PATH", m_maintainer.return_value, m_log.return_value), {}])


@pytest.mark.parametrize("extract", [True, False])
@pytest.mark.parametrize("raises", [None, Exception, identity.GPGError, sign.SigningError])
def test_packager_run(patches, extract, raises):
    packager = sign.PackageSigningRunner("x", "y", "z")
    patched = patches(
        "PackageSigningRunner.sign_tarball",
        "PackageSigningRunner.sign_directory",
        ("PackageSigningRunner.extract", dict(new_callable=PropertyMock)),
        ("PackageSigningRunner.log", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.sign")

    with patched as (m_tarb, m_dir, m_extract, m_log):
        m_extract.return_value = extract
        if raises:
            _error = raises("AN ERROR OCCURRED")
            m_extract.side_effect = _error

        if raises == Exception:
            with pytest.raises(raises):
                packager.run()
        else:
            assert packager.run() == (1 if raises else None)

    if raises:
        assert not m_tarb.called
        assert not m_dir.called
        assert not m_log.return_value.success.called

        if raises == Exception:
            return
        assert (
            list(m_log.return_value.error.call_args)
            == [(str(_error),), {}])
        return

    assert (
        list(m_log.return_value.success.call_args)
        == [('Successfully signed packages',), {}])

    if extract:
        assert (
            list(m_tarb.call_args)
            == [(), {}])
        assert not m_dir.called
        return
    assert not m_tarb.called
    assert (
        list(m_dir.call_args)
        == [(), {}])


def test_packager_sign(patches):
    packager = sign.PackageSigningRunner("x", "y", "z")
    patched = patches(
        "PackageSigningRunner.get_signing_util",
        ("PackageSigningRunner.log", dict(new_callable=PropertyMock)),
        ("PackageSigningRunner.maintainer", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.sign")

    with patched as (m_util, m_log, m_maintainer):
        assert not packager.sign("PACKAGE_TYPE", "PATH")

    assert (
        list(m_log.return_value.notice.call_args)
        == [(f"Signing PACKAGE_TYPEs ({m_maintainer.return_value}) PATH",), {}])
    assert (
        list(m_util.call_args)
        == [('PACKAGE_TYPE', 'PATH'), {}])
    assert (
        list(m_util.return_value.sign.call_args)
        == [(), {}])


@pytest.mark.parametrize("utils", [[], ["a", "b", "c"]])
@pytest.mark.parametrize("listdir", [[], ["a", "b"], ["b", "c"], ["c", "d"]])
def test_packager_sign_all(patches, listdir, utils):
    packager = sign.PackageSigningRunner("x", "y", "z")
    patched = patches(
        "os",
        "PackageSigningRunner.sign",
        ("PackageSigningRunner.signing_utils", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.sign")

    with patched as (m_os, m_sign, m_utils):
        m_os.listdir.return_value = listdir
        m_utils.return_value = utils
        assert not packager.sign_all("PATH")
    assert (
        list(m_os.listdir.call_args)
        == [('PATH',), {}])
    expected = [x for x in listdir if x in utils]
    assert (
        list(list(c) for c in m_os.path.join.call_args_list)
        == [[('PATH', k), {}] for k in expected])
    assert (
        list(list(c) for c in m_sign.call_args_list)
        == [[(k, m_os.path.join.return_value), {}] for k in expected])


@pytest.mark.parametrize("tar", [True, False])
def test_packager_sign_directory(patches, tar):
    packager = sign.PackageSigningRunner("x", "y", "z")
    patched = patches(
        "PackageSigningRunner.archive",
        "PackageSigningRunner.sign",
        ("PackageSigningRunner.package_type", dict(new_callable=PropertyMock)),
        ("PackageSigningRunner.path", dict(new_callable=PropertyMock)),
        ("PackageSigningRunner.tar", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.sign")

    with patched as (m_archive, m_sign, m_type, m_path, m_tar):
        m_tar.return_value = tar
        assert not packager.sign_directory()

    assert (
        list(m_sign.call_args)
        == [(m_type.return_value, m_path.return_value), {}])
    if not tar:
        assert not m_archive.called
        return

    assert (
        list(m_archive.call_args)
        == [(m_path.return_value, ), {}])


@pytest.mark.parametrize("tar", [True, False])
def test_packager_sign_tarball(patches, tar):
    packager = sign.PackageSigningRunner("x", "y", "z")
    patched = patches(
        "utils",
        "PackageSigningRunner.archive",
        "PackageSigningRunner.sign_all",
        ("PackageSigningRunner.path", dict(new_callable=PropertyMock)),
        ("PackageSigningRunner.tar", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.sign")

    with patched as (m_utils, m_archive, m_sign, m_path, m_tar):
        m_tar.return_value = tar
        if not tar:
            with pytest.raises(sign.SigningError) as e:
                packager.sign_tarball()
        else:
            assert not packager.sign_tarball()

    if not tar:
        assert (
            e.value.args[0]
            == 'You must set a `--tar` file to save to when `--extract` is set')
        assert not m_utils.untar.called
        assert not m_sign.called
        assert not m_archive.called
        return

    assert (
        list(m_utils.untar.call_args)
        == [(m_path.return_value,), {}])
    assert (
        list(m_sign.call_args)
        == [(m_utils.untar.return_value.__enter__.return_value,), {}])
    assert (
        list(m_archive.call_args)
        == [(m_utils.untar.return_value.__enter__.return_value,), {}])


# RPMMacro

@pytest.mark.parametrize("overwrite", [[], None, True, False])
@pytest.mark.parametrize("kwargs", [{}, dict(K1="V1", K2="V2")])
def test_rpmmacro_constructor(patches, overwrite, kwargs):
    rpmmacro = (
        sign.RPMMacro("HOME", overwrite=overwrite, **kwargs)
        if overwrite != []
        else sign.RPMMacro("HOME", **kwargs))
    assert rpmmacro._macro_filename == ".rpmmacros"
    assert rpmmacro.home == "HOME"
    assert rpmmacro.overwrite == bool(overwrite or False)
    assert rpmmacro.kwargs == kwargs
    assert rpmmacro.template == sign.RPMMACRO_TEMPLATE


def test_rpmmacro_path(patches):
    rpmmacro = sign.RPMMacro("HOME")
    patched = patches(
        "os",
        prefix="tools.distribution.sign")
    with patched as (m_os, ):
        assert rpmmacro.path == m_os.path.join.return_value

    assert (
        list(m_os.path.join.call_args)
        == [('HOME', rpmmacro._macro_filename), {}])


@pytest.mark.parametrize("kwargs", [{}, dict(K1="V1", K2="V2")])
def test_rpmmacro_macro(patches, kwargs):
    rpmmacro = sign.RPMMacro("HOME", **kwargs)
    patched = patches(
        ("RPMMacro.template", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.sign")
    with patched as (m_template, ):
        result = rpmmacro.macro

    expected = m_template.return_value
    for k, v in kwargs.items():
        assert (
            list(expected.replace.call_args)
            == [(f"__{k.upper()}__", v), {}])
        expected = expected.replace.return_value

    assert result == expected
    assert "macro" not in rpmmacro.__dict__


@pytest.mark.parametrize("overwrite", [True, False])
@pytest.mark.parametrize("exists", [True, False])
def test_rpmmacro_write(patches, overwrite, exists):
    rpmmacro = sign.RPMMacro("HOME")
    patched = patches(
        "open",
        "os",
        ("RPMMacro.macro", dict(new_callable=PropertyMock)),
        ("RPMMacro.path", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.sign")
    rpmmacro.overwrite = overwrite

    with patched as (m_open, m_os, m_macro, m_path):
        m_os.path.exists.return_value = exists
        assert not rpmmacro.write()

    if not overwrite:
        assert (
            list(m_os.path.exists.call_args)
            == [(m_path.return_value,), {}])
    else:
        assert not m_os.path.join.called
        assert not m_os.exists.join.called

    if not overwrite and exists:
        assert not m_open.called
        return

    assert (
        list(m_open.call_args)
        == [(m_path.return_value, 'w'), {}])
    assert (
        list(m_open.return_value.__enter__.return_value.write.call_args)
        == [(m_macro.return_value,), {}])


# RPMSigningUtil

@pytest.mark.parametrize("args", [(), ("ARG1", "ARG2")])
@pytest.mark.parametrize("kwargs", [{}, dict(K1="V1", K2="V2")])
def test_rpmsign_constructor(patches, args, kwargs):
    packager = sign.PackageSigningRunner("x", "y", "z")
    maintainer = identity.GPGIdentity(packager)
    patched = patches(
        "RPMSigningUtil.setup",
        "DirectorySigningUtil.__init__",
        prefix="tools.distribution.sign")

    with patched as (m_setup, m_super):
        rpmsign = sign.RPMSigningUtil("PATH", maintainer, *args, **kwargs)

    assert isinstance(rpmsign, sign.DirectorySigningUtil)
    assert rpmsign.ext == "rpm"
    assert rpmsign.command_name == "rpmsign"
    assert (
        list(m_setup.call_args)
        == [(), {}])
    assert (
        list(m_super.call_args)
        == [('PATH', maintainer) + args, kwargs])
    assert rpmsign.rpmmacro == sign.RPMMacro


@pytest.mark.parametrize("gpg2", [True, False])
def test_rpmsign_command(patches, gpg2):
    maintainer = identity.GPGIdentity()
    patched = patches(
        "os",
        "RPMSigningUtil.__init__",
        ("DirectorySigningUtil.command", dict(new_callable=PropertyMock)),
        ("identity.GPGIdentity.gpg_bin", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.sign")

    with patched as (m_os, m_init, m_super, m_gpg):
        m_os.path.basename.return_value = "gpg2" if gpg2 else "notgpg2"
        m_init.return_value = None
        rpmsign = sign.RPMSigningUtil("PATH", maintainer, "LOG")
        rpmsign.maintainer = maintainer

        if gpg2:
            assert rpmsign.command == m_super.return_value
        else:
            with pytest.raises(sign.SigningError) as e:
                rpmsign.command

            assert (
                e.value.args[0]
                == 'GPG2 is required to sign RPM packages')

    assert (
        list(m_os.path.basename.call_args)
        == [(m_gpg.return_value,), {}])
    if gpg2:
        assert "command" in rpmsign.__dict__
    else:
        assert "command" not in rpmsign.__dict__


def test_rpmsign_command_args(patches):
    packager = sign.PackageSigningRunner("x", "y", "z")
    maintainer = identity.GPGIdentity(packager)
    patched = patches(
        "RPMSigningUtil.setup",
        ("identity.GPGIdentity.fingerprint", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.sign")

    with patched as (m_setup, m_fingerprint):
        rpmsign = sign.RPMSigningUtil("PATH", maintainer, "LOG")
        assert (
            rpmsign.command_args
            == ("--key-id", m_fingerprint.return_value,
                "--addsign"))

    assert "command_args" in rpmsign.__dict__


class DummyRPMSigningUtil(sign.RPMSigningUtil):

    def __init__(self, path, maintainer):
        self.path = path
        self.maintainer = maintainer


def test_rpmsign_setup(patches):
    packager = sign.PackageSigningRunner("x", "y", "z")
    maintainer = MagicMock()

    rpmsign = DummyRPMSigningUtil("PATH", maintainer)

    patched = patches(
        ("RPMSigningUtil.rpmmacro", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.sign")

    with patched as (m_macro, ):
        assert not rpmsign.setup()

    assert (
        list(m_macro.return_value.call_args)
        == [(maintainer.home,),
            {'maintainer': maintainer.name,
             'gpg_bin': maintainer.gpg_bin,
             'gpg_config': maintainer.gnupg_home}])


def test_rpmsign_sign_pkg(patches):
    packager = sign.PackageSigningRunner("x", "y", "z")
    maintainer = identity.GPGIdentity(packager)
    rpmsign = DummyRPMSigningUtil("PATH", maintainer)
    patched = patches(
        "os",
        "DirectorySigningUtil.sign_pkg",
        prefix="tools.distribution.sign")

    with patched as (m_os, m_sign):
        assert not rpmsign.sign_pkg("FILE")

    assert (
        list(m_os.chmod.call_args)
        == [('FILE', 0o755), {}])
    assert (
        list(m_sign.call_args)
        == [('FILE',), {}])


# DebChangesFiles

def test_changes_constructor():
    changes = sign.DebChangesFiles("SRC")
    assert changes.src == "SRC"


def test_changes_dunder_iter(patches):
    changes = sign.DebChangesFiles("SRC")

    patched = patches(
        "os",
        ("DebChangesFiles.files", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.sign")
    _files = ["FILE1", "FILE2", "FILE3"]

    with patched as (m_os, m_files):
        m_files.return_value = _files
        result = changes.__iter__()
        assert list(result) == _files

    assert isinstance(result, types.GeneratorType)
    assert (
        list(m_os.unlink.call_args)
        == [('SRC',), {}])


@pytest.mark.parametrize(
    "lines",
    [([], None),
     (["FOO", "BAR"], None),
     (["FOO", "BAR", "Distribution: distro1"], "distro1"),
     (["FOO", "BAR", "Distribution: distro1 distro2"], "distro1 distro2"),
     (["FOO", "BAR", "Distribution: distro1 distro2", "BAZ"], "distro1 distro2"),
     (["FOO", "BAR", "", "Distribution: distro1 distro2"], None)])
def test_changes_distributions(patches, lines):
    lines, expected = lines
    changes = sign.DebChangesFiles("SRC")
    patched = patches(
        "open",
        prefix="tools.distribution.sign")

    class DummyFile(object):
        line = 0

        def __init__(self, lines):
            self.lines = lines

        def readline(self):
            if len(self.lines) > self.line:
                line = self.lines[self.line]
                self.line += 1
                return line

    _file = DummyFile(lines)

    with patched as (m_open, ):
        m_open.return_value.__enter__.return_value.readline.side_effect = _file.readline
        if expected:
            assert changes.distributions == expected
        else:
            with pytest.raises(sign.SigningError) as e:
                changes.distributions
            assert (
                e.value.args[0]
                == "Did not find Distribution field in changes file SRC")

    if "" in lines:
        lines = lines[:lines.index("")]

    if expected:
        breakon = 0
        for line in lines:
            if line.startswith("Distribution:"):
                break
            breakon += 1
        lines = lines[:breakon]
    count = len(lines) + 1
    assert (
        list(list(c) for c in m_open.return_value.__enter__.return_value.readline.call_args_list)
        == [[(), {}]] * count)


def test_changes_files(patches):
    changes = sign.DebChangesFiles("SRC")

    patched = patches(
        "DebChangesFiles.changes_file",
        ("DebChangesFiles.distributions", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.sign")

    with patched as (m_changes, m_distros):
        m_distros.return_value = "DISTRO1 DISTRO2 DISTRO3"
        result = changes.files
        assert list(result) == [m_changes.return_value] * 3

    assert isinstance(result, types.GeneratorType)
    assert (
        list(list(c) for c in m_changes.call_args_list)
        == [[('DISTRO1',), {}],
            [('DISTRO2',), {}],
            [('DISTRO3',), {}]])


def test_changes_changes_file(patches):
    changes = sign.DebChangesFiles("SRC")
    patched = patches(
        "open",
        "DebChangesFiles.changes_file_path",
        ("DebChangesFiles.distributions", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.sign")

    with patched as (m_open, m_path, m_distros):
        assert (
            changes.changes_file("DISTRO")
            == m_path.return_value)

    assert (
        list(m_path.call_args)
        == [('DISTRO',), {}])
    assert (
        list(list(c) for c in m_open.call_args_list)
        == [[(m_path.return_value, 'w'), {}],
            [('SRC',), {}]])
    assert (
        list(m_open.return_value.__enter__.return_value.write.call_args)
        == [(m_open.return_value.__enter__.return_value.read.return_value.replace.return_value,), {}])
    assert (
        list(m_open.return_value.__enter__.return_value.read.call_args)
        == [(), {}])
    assert (
        list(m_open.return_value.__enter__.return_value.read.return_value.replace.call_args)
        == [(m_distros.return_value, 'DISTRO'), {}])


@pytest.mark.parametrize(
    "path",
    [("SRC", "SRC.DISTRO.changes"),
     ("SRC.changes", "SRC.DISTRO.changes"),
     ("SRC.FOO.BAR.changes", "SRC.FOO.BAR.DISTRO.changes")])
def test_changes_file_path(path):
    path, expected = path
    changes = sign.DebChangesFiles(path)
    assert changes.changes_file_path("DISTRO") == expected


# DebSigningUtil

@pytest.mark.parametrize("args", [(), ("ARG1", ), ("ARG2", )])
def test_debsign_constructor(patches, args):
    packager = sign.PackageSigningRunner("x", "y", "z")
    maintainer = identity.GPGIdentity(packager)
    debsign = sign.DebSigningUtil("PATH", maintainer, "LOG", *args)

    assert isinstance(debsign, sign.DirectorySigningUtil)
    assert debsign.ext == "changes"
    assert debsign.command_name == "debsign"
    assert debsign._package_type == "deb"
    assert debsign.changes_files == sign.DebChangesFiles
    assert debsign.path == "PATH"
    assert debsign.maintainer == maintainer
    assert debsign.log == "LOG"


def test_debsign_command_args(patches):
    packager = sign.PackageSigningRunner("x", "y", "z")
    maintainer = identity.GPGIdentity(packager)
    patched = patches(
        ("identity.GPGIdentity.fingerprint", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.sign")

    with patched as (m_fingerprint, ):
        debsign = sign.DebSigningUtil("PATH", maintainer, "LOG")
        assert (
            debsign.command_args
            == ("-k", m_fingerprint.return_value))

    assert "command_args" in debsign.__dict__


def test_debsign_pkg_files(patches):
    packager = sign.PackageSigningRunner("x", "y", "z")
    maintainer = identity.GPGIdentity(packager)
    debsign = sign.DebSigningUtil("PATH", maintainer, "LOG")
    patched = patches(
        "chain",
        ("DirectorySigningUtil.pkg_files", dict(new_callable=PropertyMock)),
        ("DebSigningUtil.changes_files", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.sign")

    with patched as (m_chain, m_pkg, m_changes):
        m_pkg.return_value = ("FILE1", "FILE2", "FILE3")
        m_chain.from_iterable.side_effect = lambda _iter: list(_iter)
        assert (
            debsign.pkg_files
            == (m_changes.return_value.return_value, ) * 3)

    assert m_chain.from_iterable.called
    assert (
        list(list(c) for c in m_changes.return_value.call_args_list)
        == [[('FILE1',), {}], [('FILE2',), {}], [('FILE3',), {}]])


# Module

def test_sign_main(patches, command_main):
    patched = patches(
        "_register_utils",
        prefix="tools.distribution.sign")

    with patched as (m_reg, ):
        command_main(
            sign.main,
            "tools.distribution.sign.PackageSigningRunner")

    assert (
        list(m_reg.call_args)
        == [(), {}])


def test_sign_register_utils(patches, command_main):
    patched = patches(
        "PackageSigningRunner.register_util",
        prefix="tools.distribution.sign")

    with patched as (m_reg, ):
        sign._register_utils()

    assert (
        list(list(c) for c in m_reg.call_args_list)
        == [[('deb', sign.DebSigningUtil), {}],
            [('rpm', sign.RPMSigningUtil), {}]])
