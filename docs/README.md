# Developer-local docs build

```bash
./docs/build.sh
```

The output can be found in `generated/docs`.

# How the Envoy website and docs are updated

The Envoy website, and docs are automatically built, and pushed on every commit 
to master. This process is handled by Travis CI with the 
[`publish.sh`](https://github.com/envoyproxy/envoy/blob/master/docs/publish.sh) script.

In order to have this automatic process there is an encrypted ssh key at the root 
of the envoy repo (`.publishdocskey.enc`). This key was encrypted with Travis CLI 
and can only be decrypted by commits initiated in the Envoy repo, not PRs that are
submitted from forks. This is the case because only PRs initiated in the Envoy 
repo have access to the secure environment variables (`encrypted_b1a4cc52fa4a_iv`, 
`encrypted_b1a4cc52fa4a_key`) [used to decrypt the key.](https://docs.travis-ci.com/user/pull-requests#Pull-Requests-and-Security-Restrictions)

The key only has write access to the Envoy repo. If the key, or the variables 
used to decrypt it are ever compromised, delete the key immediately from the 
Envoy repo in `Settings > Deploy keys`.
