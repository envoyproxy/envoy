VERSIONS = {
    "python": "3.10",
    "envoy": {
        "type": "github_archive",
        "repo": "envoyproxy/envoy",
        "version": "c4fe01c44cb75bceb0342a79e20375674557563c",
        "sha256": "98055fa14a425cc8816cc32d3199fedb38ead7f1a6033f398a0e5fe8a3c47d3a",
        "urls": ["https://github.com/{repo}/archive/{version}.tar.gz"],
        "strip_prefix": "envoy-{version}",
    },
}
