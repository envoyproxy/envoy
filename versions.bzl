VERSIONS = {
    "python": "3.10",
    "envoy": {
        "type": "github_archive",
        "repo": "envoyproxy/envoy",
        "version": "d74a81f4e320dc8cadaea9fceba8bb1a8cb36ad9",
        "sha256": "d32433412e465619086b446dcedf172ecdbdb959d6010e67459b8d8d16d37227",
        "urls": ["https://github.com/{repo}/archive/{version}.tar.gz"],
        "strip_prefix": "envoy-{version}",
    },
}
