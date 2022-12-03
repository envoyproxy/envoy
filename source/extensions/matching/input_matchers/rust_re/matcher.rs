#[cxx::bridge(namespace = "Envoy::Extensions::Matching::InputMatchers::RustRe")]
mod ffi {
    extern "Rust" {
        type ReMatcher;

        fn newReMatcher(regex: &str) -> Box<ReMatcher>;

        fn doMatch(matcher: &ReMatcher, value: &str) -> bool;
    }
}

struct ReMatcher {
    r: regex::Regex,
}

#[allow(non_snake_case)]
fn newReMatcher(regex_str: &str) -> Box<ReMatcher> {
    Box::new(ReMatcher {
        r: regex::Regex::new(regex_str).unwrap(),
    })
}

#[allow(non_snake_case)]
fn doMatch(matcher: &ReMatcher, value: &str) -> bool {
    matcher.r.is_match(value)
}
