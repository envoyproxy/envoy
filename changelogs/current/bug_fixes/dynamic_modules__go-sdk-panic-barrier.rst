Fixed a crash in the dynamic modules Go SDK where a panic raised inside a module hook crossed the
cgo export boundary and aborted the whole process. Every Go SDK export now recovers a panic at the
ABI boundary, logs it at error level, and returns a fail-closed value, mirroring the Rust and C++
SDK panic barriers.
