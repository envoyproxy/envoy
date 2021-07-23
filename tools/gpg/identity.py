import logging
import os
import pwd
import shutil
from functools import cached_property
from email.utils import formataddr, parseaddr
from typing import Optional

import gnupg


class GPGError(Exception):
    pass


class GPGIdentity(object):
    """A GPG identity with a signing key

    The signing key is found either by matching provided name/email,
    or by retrieving the first private key.
    """

    def __init__(
            self,
            name: Optional[str] = None,
            email: Optional[str] = None,
            log: Optional[logging.Logger] = None):
        self._provided_name = name
        self._provided_email = email
        self._log = log

    def __str__(self) -> str:
        return self.uid

    @cached_property
    def email(self) -> str:
        """Email parsed from the signing key"""
        return parseaddr(self.uid)[1]

    @property
    def fingerprint(self) -> str:
        """GPG key fingerprint"""
        return self.signing_key["fingerprint"]

    @cached_property
    def gpg(self) -> gnupg.GPG:
        return gnupg.GPG()

    @cached_property
    def gpg_bin(self) -> str:
        return shutil.which("gpg2") or shutil.which("gpg")

    @property
    def gnupg_home(self) -> str:
        return os.path.join(self.home, ".gnupg")

    @cached_property
    def home(self) -> str:
        """Gets *and sets if required* the `HOME` env var"""
        os.environ["HOME"] = os.environ.get("HOME", pwd.getpwuid(os.getuid()).pw_dir)
        return os.environ["HOME"]

    @cached_property
    def log(self) -> logging.Logger:
        return self._log or logging.getLogger(self.__class__.__name__)

    @property
    def provided_email(self) -> Optional[str]:
        """Provided email for the identity"""
        return self._provided_email

    @cached_property
    def provided_id(self) -> Optional[str]:
        """Provided name and/or email for the identity"""
        if not (self.provided_name or self.provided_email):
            return
        return (
            formataddr(self.provided_name, self.provided_email) if
            (self.provided_name and self.provided_email) else
            (self.provided_name or self.provided_email))

    @property
    def provided_name(self) -> Optional[str]:
        """Provided name for the identity"""
        return self._provided_name

    @cached_property
    def name(self) -> str:
        """Name parsed from the signing key"""
        return parseaddr(self.uid)[0]

    @cached_property
    def signing_key(self) -> dict:
        """A `dict` representing the GPG key to sign with"""
        # if name and/or email are provided the list of keys is pre-filtered
        # but we still need to figure out which uid matched for the found key
        for key in self.gpg.list_keys(True, keys=self.provided_id):
            key = self.match(key)
            if key:
                return key
        raise GPGError(
            f"No key found for '{self.provided_id}'" if self.provided_id else "No available key")

    @property
    def uid(self) -> str:
        """UID of the identity's signing key"""
        return self.signing_key["uid"]

    def match(self, key: dict) -> Optional[dict]:
        """Match a signing key

        The key is found either by matching provided name/email
        or the first available private key

        the matching `uid` (or first) is added as `uid` to the dict
        """
        if self.provided_id:
            key["uid"] = self._match_key(key["uids"])
            return key if key["uid"] else None
        if self.log:
            self.log.warning("No GPG name/email supplied, signing with first available key")
        key["uid"] = key["uids"][0]
        return key

    def _match_email(self, uids: list) -> Optional[str]:
        """Match only the email"""
        for uid in uids:
            if parseaddr(uid)[1] == self.provided_email:
                return uid

    def _match_key(self, uids: dict) -> Optional[str]:
        """If either/both name or email are supplied it tries to match either/both"""
        if self.provided_name and self.provided_email:
            return self._match_uid(uids)
        elif self.provided_name:
            return self._match_name(uids)
        elif self.provided_email:
            return self._match_email(uids)

    def _match_name(self, uids: list) -> Optional[str]:
        """Match only the name"""
        for uid in uids:
            if parseaddr(uid)[0] == self.provided_name:
                return uid

    def _match_uid(self, uids: list) -> Optional[str]:
        """Match the whole uid - ie `Name <ema.il>`"""
        return self.provided_id if self.provided_id in uids else None
