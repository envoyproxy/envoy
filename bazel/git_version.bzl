def _impl(ctx):
    ctx.symlink(Label("//tools:gen_git_sha.sh"), "gen_git_sha.sh")
    result = ctx.execute(
        ["./gen_git_sha.sh"]
    )
    if result.return_code != 0:
        fail("External dep build failed")

git_version_rule = repository_rule(
    implementation = _impl,
    environ = ["GIT_BUILD_SHA"]
    )

def git_version():
    git_version_rule(name="git_version")
