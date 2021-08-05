from unittest.mock import MagicMock, PropertyMock

import pytest

from tools.gpg import identity


@pytest.mark.parametrize("name", ["NAME", None])
@pytest.mark.parametrize("email", ["EMAIL", None])
@pytest.mark.parametrize("log", ["LOG", None])
def test_identity_constructor(name, email, log):
    gpg = identity.GPGIdentity(name, email, log)
    assert gpg.provided_name == name
    assert gpg.provided_email == email
    assert gpg._log == log


def test_identity_dunder_str(patches):
    gpg = identity.GPGIdentity()
    patched = patches(
        ("GPGIdentity.uid", dict(new_callable=PropertyMock)),
        prefix="tools.gpg.identity")

    with patched as (m_uid, ):
        m_uid.return_value = "SOME BODY"
        assert str(gpg) == "SOME BODY"


def test_identity_email(patches):
    gpg = identity.GPGIdentity()
    patched = patches(
        "parseaddr",
        ("GPGIdentity.uid", dict(new_callable=PropertyMock)),
        prefix="tools.gpg.identity")

    with patched as (m_parse, m_uid):
        assert gpg.email == m_parse.return_value.__getitem__.return_value

    assert (
        list(m_parse.return_value.__getitem__.call_args)
        == [(1,), {}])
    assert (
        list(m_parse.call_args)
        == [(m_uid.return_value,), {}])
    assert "email" in gpg.__dict__


def test_identity_fingerprint(patches):
    gpg = identity.GPGIdentity()
    patched = patches(
        ("GPGIdentity.signing_key", dict(new_callable=PropertyMock)),
        prefix="tools.gpg.identity")

    with patched as (m_key, ):
        assert gpg.fingerprint == m_key.return_value.__getitem__.return_value

    assert (
        list(m_key.return_value.__getitem__.call_args)
        == [('fingerprint',), {}])

    assert "fingerprint" not in gpg.__dict__


def test_identity_gpg(patches):
    gpg = identity.GPGIdentity()
    patched = patches(
        "gnupg.GPG",
        prefix="tools.gpg.identity")

    with patched as (m_gpg, ):
        assert gpg.gpg == m_gpg.return_value

    assert (
        list(m_gpg.call_args)
        == [(), {}])

    assert "gpg" in gpg.__dict__


def test_identity_gnupg_home(patches):
    gpg = identity.GPGIdentity()
    patched = patches(
        "os",
        ("GPGIdentity.home", dict(new_callable=PropertyMock)),
        prefix="tools.gpg.identity")

    with patched as (m_os, m_home):
        assert gpg.gnupg_home == m_os.path.join.return_value

    assert (
        list(m_os.path.join.call_args)
        == [(m_home.return_value, '.gnupg'), {}])

    assert "gnupg_home" not in gpg.__dict__


@pytest.mark.parametrize("gpg", [None, "GPG"])
@pytest.mark.parametrize("gpg2", [None, "GPG2"])
def test_identity_gpg_bin(patches, gpg, gpg2):
    gpg = identity.GPGIdentity()
    patched = patches(
        "shutil",
        prefix="tools.gpg.identity")

    def _get_bin(_cmd):
        if _cmd == "gpg2" and gpg2:
            return gpg2
        if _cmd == "gpg" and gpg:
            return gpg

    with patched as (m_shutil, ):
        m_shutil.which.side_effect = _get_bin
        assert gpg.gpg_bin == gpg2 or gpg

    if gpg2:
        assert (
            list(list(c) for c in m_shutil.which.call_args_list)
            == [[('gpg2',), {}]])
        return
    assert (
        list(list(c) for c in m_shutil.which.call_args_list)
        == [[('gpg2',), {}], [('gpg',), {}]])


def test_identity_home(patches):
    gpg = identity.GPGIdentity()
    patched = patches(
        "os",
        "pwd",
        prefix="tools.gpg.identity")

    with patched as (m_os, m_pwd):
        assert gpg.home == m_os.environ.__getitem__.return_value

    assert (
        list(m_os.environ.__getitem__.call_args)
        == [('HOME', ), {}])
    assert (
        list(m_os.environ.__setitem__.call_args)
        == [('HOME', m_os.environ.get.return_value), {}])
    assert (
        list(m_os.environ.get.call_args)
        == [('HOME', m_pwd.getpwuid.return_value.pw_dir), {}])
    assert (
        list(m_pwd.getpwuid.call_args)
        == [(m_os.getuid.return_value,), {}])
    assert (
        list(m_os.getuid.call_args)
        == [(), {}])

    assert "home" in gpg.__dict__


@pytest.mark.parametrize("log", ["LOGGER", None])
def test_identity_log(patches, log):
    gpg = identity.GPGIdentity()
    patched = patches(
        "logging",
        prefix="tools.gpg.identity")

    gpg._log = log

    with patched as (m_log, ):
        if log:
            assert gpg.log == log
            assert not m_log.getLogger.called
        else:
            assert gpg.log == m_log.getLogger.return_value
            assert (
                list(m_log.getLogger.call_args)
                == [(gpg.__class__.__name__, ), {}])


@pytest.mark.parametrize("name", ["NAME", None])
@pytest.mark.parametrize("email", ["EMAIL", None])
def test_identity_identity_id(patches, name, email):
    gpg = identity.GPGIdentity()
    patched = patches(
        "formataddr",
        ("GPGIdentity.provided_name", dict(new_callable=PropertyMock)),
        ("GPGIdentity.provided_email", dict(new_callable=PropertyMock)),
        prefix="tools.gpg.identity")

    with patched as (m_format, m_name, m_email):
        m_name.return_value = name
        m_email.return_value = email
        result = gpg.provided_id

    assert "provided_id" in gpg.__dict__

    if name and email:
        assert (
            list(m_format.call_args)
            == [('NAME', 'EMAIL'), {}])
        assert result == m_format.return_value
        return

    assert not m_format.called
    assert result == name or email


def test_identity_name(patches):
    gpg = identity.GPGIdentity()
    patched = patches(
        "parseaddr",
        ("GPGIdentity.uid", dict(new_callable=PropertyMock)),
        prefix="tools.gpg.identity")

    with patched as (m_parse, m_uid):
        assert gpg.name == m_parse.return_value.__getitem__.return_value

    assert (
        list(m_parse.return_value.__getitem__.call_args)
        == [(0,), {}])
    assert (
        list(m_parse.call_args)
        == [(m_uid.return_value,), {}])
    assert "name" in gpg.__dict__


@pytest.mark.parametrize("key", ["KEY1", "KEY2", "KEY5"])
@pytest.mark.parametrize("name", ["NAME", None])
@pytest.mark.parametrize("email", ["EMAIL", None])
def test_identity_signing_key(patches, key, name, email):
    packager = MagicMock()
    gpg = identity.GPGIdentity()
    _keys = ["KEY1", "KEY2", "KEY3"]
    patched = patches(
        "GPGIdentity.match",
        ("GPGIdentity.gpg", dict(new_callable=PropertyMock)),
        ("GPGIdentity.provided_id", dict(new_callable=PropertyMock)),
        prefix="tools.gpg.identity")

    with patched as (m_match, m_gpg, m_id):
        if not name and not email:
            m_id.return_value = None
        m_match.side_effect = lambda k: (k == key and f"MATCH {k}")
        m_gpg.return_value.list_keys.return_value = _keys
        if key in _keys:
            assert gpg.signing_key == f"MATCH {key}"
            _match_attempts = _keys[:_keys.index(key) + 1]
        else:
            with pytest.raises(identity.GPGError) as e:
                gpg.signing_key
            if name or email:
                assert (
                    e.value.args[0]
                    == f"No key found for '{m_id.return_value}'")
            else:
                assert (
                    e.value.args[0]
                    == 'No available key')
            _match_attempts = _keys

    assert (
        list(m_gpg.return_value.list_keys.call_args)
        == [(True, ), dict(keys=m_id.return_value)])
    assert (
        list(list(c) for c in m_match.call_args_list)
        == [[(k,), {}] for k in _match_attempts])


def test_identity_uid(patches):
    gpg = identity.GPGIdentity()
    patched = patches(
        ("GPGIdentity.signing_key", dict(new_callable=PropertyMock)),
        prefix="tools.gpg.identity")

    with patched as (m_key, ):
        assert gpg.uid == m_key.return_value.__getitem__.return_value

    assert (
        list(m_key.return_value.__getitem__.call_args)
        == [('uid',), {}])

    assert "uid" not in gpg.__dict__


@pytest.mark.parametrize("name", ["NAME", None])
@pytest.mark.parametrize("email", ["EMAIL", None])
@pytest.mark.parametrize("match", ["MATCH", None])
@pytest.mark.parametrize("log", [True, False])
def test_identity_match(patches, name, email, match, log):
    gpg = identity.GPGIdentity()
    _keys = ["KEY1", "KEY2", "KEY3"]
    patched = patches(
        "GPGIdentity._match_key",
        ("GPGIdentity.provided_id", dict(new_callable=PropertyMock)),
        ("GPGIdentity.log", dict(new_callable=PropertyMock)),
        prefix="tools.gpg.identity")
    key = dict(uids=["UID1", "UID2"])

    with patched as (m_match, m_id, m_log):
        if not log:
            m_log.return_value = None
        m_match.return_value = match
        m_id.return_value = name or email
        result = gpg.match(key)

    if not name and not email:
        assert not m_match.called
        if log:
            assert (
                list(m_log.return_value.warning.call_args)
                == [('No GPG name/email supplied, signing with first available key',), {}])
        assert (
            result
            == {'uids': ['UID1', 'UID2'], 'uid': 'UID1'})
        return
    assert (
        list(m_match.call_args)
        == [(key["uids"],), {}])
    if log:
        assert not m_log.return_value.warning.called
    if match:
        assert (
            result
            == {'uids': ['UID1', 'UID2'], 'uid': 'MATCH'})
    else:
        assert not result


@pytest.mark.parametrize("uids", [[], ["UID1"], ["UID1", "UID2"]])
@pytest.mark.parametrize("email", [None, "UID1", "UID1", "UID2", "UID3"])
def test_identity__match_email(patches, uids, email):
    gpg = identity.GPGIdentity()
    patched = patches(
        "parseaddr",
        ("GPGIdentity.provided_email", dict(new_callable=PropertyMock)),
        prefix="tools.gpg.identity")

    with patched as (m_parse, m_email):
        m_parse.side_effect = lambda _email: ("NAME", _email)
        m_email.return_value = email
        result = gpg._match_email(uids)

    if email in uids:
        assert result == email
        assert (
            list(list(c) for c in m_parse.call_args_list)
            == [[(uid,), {}] for uid in uids[:uids.index(email) + 1]])
        return

    assert not result
    assert (
        list(list(c) for c in m_parse.call_args_list)
        == [[(uid,), {}] for uid in uids])


@pytest.mark.parametrize("name", ["NAME", None])
@pytest.mark.parametrize("email", ["EMAIL", None])
def test_identity__match_key(patches, name, email):
    gpg = identity.GPGIdentity()
    _keys = ["KEY1", "KEY2", "KEY3"]
    patched = patches(
        "GPGIdentity._match_email",
        "GPGIdentity._match_name",
        "GPGIdentity._match_uid",
        ("GPGIdentity.provided_email", dict(new_callable=PropertyMock)),
        ("GPGIdentity.provided_name", dict(new_callable=PropertyMock)),
        prefix="tools.gpg.identity")
    key = dict(uids=["UID1", "UID2"])

    with patched as (m_email, m_name, m_uid, m_pemail, m_pname):
        m_pemail.return_value = email
        m_pname.return_value = name
        result = gpg._match_key(key)

    if name and email:
        assert (
            list(m_uid.call_args)
            == [(dict(uids=key["uids"]),), {}])
        assert not m_email.called
        assert not m_name.called
        assert result == m_uid.return_value
    elif name:
        assert (
            list(m_name.call_args)
            == [(dict(uids=key["uids"]),), {}])
        assert not m_email.called
        assert not m_uid.called
        assert result == m_name.return_value
    elif email:
        assert (
            list(m_email.call_args)
            == [(dict(uids=key["uids"]),), {}])
        assert not m_name.called
        assert not m_uid.called
        assert result == m_email.return_value


@pytest.mark.parametrize("uids", [[], ["UID1"], ["UID1", "UID2"]])
@pytest.mark.parametrize("name", [None, "UID1", "UID1", "UID2", "UID3"])
def test_identity__match_name(patches, uids, name):
    gpg = identity.GPGIdentity()
    patched = patches(
        "parseaddr",
        ("GPGIdentity.provided_name", dict(new_callable=PropertyMock)),
        prefix="tools.gpg.identity")

    with patched as (m_parse, m_name):
        m_parse.side_effect = lambda _name: (_name, "EMAIL")
        m_name.return_value = name
        result = gpg._match_name(uids)

    if name in uids:
        assert result == name
        assert (
            list(list(c) for c in m_parse.call_args_list)
            == [[(uid,), {}] for uid in uids[:uids.index(name) + 1]])
        return

    assert not result
    assert (
        list(list(c) for c in m_parse.call_args_list)
        == [[(uid,), {}] for uid in uids])


@pytest.mark.parametrize("uid", ["UID1", "UID7"])
def test_identity__match_uid(patches, uid):
    gpg = identity.GPGIdentity()
    uids = [f"UID{i}" for i in range(5)]
    matches = uid in uids
    patched = patches(
        ("GPGIdentity.provided_id", dict(new_callable=PropertyMock)),
        prefix="tools.gpg.identity")

    with patched as (m_id, ):
        m_id.return_value = uid
        if matches:
            assert gpg._match_uid(uids) == uid
        else:
            assert not gpg._match_uid(uids)
