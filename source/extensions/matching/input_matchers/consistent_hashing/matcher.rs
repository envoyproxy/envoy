#[cxx::bridge(namespace = "Envoy::Extensions::Matching::InputMatchers::ConsistentHashing")]
mod ffi {
    extern "Rust" {
        type RustMatcher;

        fn newMatcher(threshold: u32, modulo: u32, seed: u64) -> Box<RustMatcher>;

        fn doMatch(matcher: &RustMatcher, value: &str) -> bool;
    }

    unsafe extern "C++" {
        include!("source/extensions/matching/input_matchers/consistent_hashing/hash.h");

        // Bridging function to call out to xxHash.
        fn hash(value: &str, seed: u64) -> u64;
    }
}

struct RustMatcher {
    threshold: u32,
    modulo: u32,
    seed: u64,
}

#[allow(non_snake_case)]
fn newMatcher(threshold: u32, modulo: u32, seed: u64) -> Box<RustMatcher> {
    Box::new(RustMatcher {
        threshold,
        modulo,
        seed,
    })
}

#[allow(non_snake_case)]
fn doMatch(matcher: &RustMatcher, value: &str) -> bool {
    ffi::hash(value, matcher.seed).rem_euclid(matcher.modulo.into()) >= matcher.threshold.into()
}
