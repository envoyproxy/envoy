import tempfile
from contextlib import contextmanager
from typing import Iterator

from git import Repo


@contextmanager
def git_repo(uri: str) -> Iterator[Repo]:
    with tempfile.TemporaryDirectory() as tmpdir:
        yield Repo.clone_from(uri, tmpdir)
