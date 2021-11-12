
from tools.git import utils


def test_util_git_repo(patches):
    patched = patches(
        "tempfile.TemporaryDirectory",
        "Repo",
        prefix="tools.git.utils")

    with patched as (m_tmp, m_repo):
        with utils.git_repo("URI") as tmpdir:
            assert tmpdir == m_repo.clone_from.return_value

    assert (
        list(m_tmp.call_args)
        == [(), {}])
    assert (
        list(m_repo.clone_from.call_args)
        == [('URI', m_tmp.return_value.__enter__.return_value), {}])
