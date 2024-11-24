VERSIONS = {
    "python": "3.12",
    "envoy": {
        "type": "github_archive",
        "repo": "envoyproxy/envoy",
        "version": "2a83ddce2badd4ab890433df42248cfa78ae6ef0",
        "sha256": "c2b0891316b630ff68af147e3f4c6920d313a427605484293c6f178c05181288",
        "urls": ["https://github.com/{repo}/archive/{version}.tar.gz"],
        "strip_prefix": "envoy-{version}",
    },
}
