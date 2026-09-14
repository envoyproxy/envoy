The certificate and key in this directory (servercert.pem, serverkey.pem) are
generated, publicly-known TEST credentials.

They are generated at build time from //test/config/integration/certs (see
@envoy_toolshed//certs) and are used only by the envoy-google-vrp Docker image
to configure a TLS listener for fuzzing.

They are not secret, and they are not part of the signed release artifacts
(release.signed.tar.zst) or any other distribution channel -- they are only
present in the internal, docker-only release tarball consumed by the
envoy-google-vrp image build.

Do not treat these files as sensitive, and do not report them as leaked
credentials.
