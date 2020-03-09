Envoy hooks:

- [ ] core: logging
- [ ] core: stats
- [ ] core: http callouts
- [ ] core: gRPC callouts
- [ ] http filters: accept/reject request(?)
- [ ] http filters: get/set header
- [ ] http filters: get all headers
- [ ] http filters: get/set body
- [ ] http filters: get/set metadata
- [ ] http filters: access log
- [ ] network filters: accept/reject connection
- [ ] network filters: mutate payload
- [ ] network filters: get/set metadata
- [ ] network filters: access log
- [ ] listener filters: accept/reject connection
- [ ] listener filters: peak at payload
- [ ] listener filters: get/set metadata
- [ ] listener filters: consume payload
- [ ] transport sockets

Features:

- [ ] shared key/value store (per filter by default)

Supported programming languages:

- [ ] C and C++ (using [Emscripten])
- [ ] Go (using [TinyGo])
- [ ] Rust

Future additions:

- [ ] Go (using `syscall/js`)
- [ ] Java (using [TeaVM])
- [ ] TypeScript (using [AssemblyScript])

[AssemblyScript]: https://assemblyscript.org/
[Emscripten]: https://kripken.github.io/emscripten-site/
[TeaVM]: http://teavm.org/
[TinyGo]: https://tinygo.readthedocs.io/en/latest/
