VERSIONS = {
    "python": "3.10",
    "envoy": {
        "type": "github_archive",
        "repo": "envoyproxy/envoy",
        "version": "81d66f1d8247c2282351e42becfa7777fc359d61",
        "sha256": "6f8b2bc0e86b4b10c9f9e0559d563576e1bdd2ebbc5cbc012fc029d108b97fe3",
        "urls": ["https://github.com/{repo}/archive/{version}.tar.gz"],
        "strip_prefix": "envoy-{version}",
    },
}
