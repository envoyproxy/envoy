import tempfile
from contextlib import contextmanager
from typing import Iterator

from git import Repo


@contextmanager
def git_repo(uri: str) -> Iterator[Repo]:
    """Check out a git repository to a temporary directory

    for example to add a file, commit and push it, it can be used as so:

    ```python
    import os

    from tooling.git.utils import git_repo


    with git_repo("git@github.com/envoyproxy/envoy") as repo:
        filename = "foo.txt"

        with open(os.path.join(repo.working_dir, filename), "w") as f:
            f.write("bar")
        repo.index.add([filename])
        repo.index.commit(f"Added {filename}")
        repo.remotes.origin.push()
    ```

    the temporary directory used to checkout the repo will be cleaned
    up on in exiting the contextmanager.

    """
    with tempfile.TemporaryDirectory() as tmpdir:
        yield Repo.clone_from(uri, tmpdir)
