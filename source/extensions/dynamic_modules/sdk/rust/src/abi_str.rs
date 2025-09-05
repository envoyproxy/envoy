use std::borrow::Borrow;
use std::convert::Infallible;
use std::fmt::Display;
use std::hash::{Hash, Hasher};
use std::str::FromStr;

/// A str struct that represents a string slice owned by the dynamic module.
///
/// This is used to pass strings between the dynamic module and Envoy.
#[repr(C)]
#[derive(Debug, Clone, Copy, Eq)]
pub struct ModuleStr<'a> {
  ptr: *const u8,
  len: usize,
  _marker: std::marker::PhantomData<&'a ()>,
}

impl Default for ModuleStr<'_> {
  fn default() -> Self {
    Self::new("")
  }
}

impl ModuleStr<'_> {
  pub fn new(s: &str) -> Self {
    Self {
      ptr: s.as_ptr(),
      len: s.len(),
      _marker: std::marker::PhantomData,
    }
  }
}

impl PartialEq for ModuleStr<'_> {
  fn eq(&self, other: &Self) -> bool {
    Into::<&str>::into(*self) == Into::<&str>::into(*other)
  }
}

impl Ord for ModuleStr<'_> {
  fn cmp(&self, other: &Self) -> std::cmp::Ordering {
    Into::<&str>::into(*self).cmp(Into::<&str>::into(*other))
  }
}

impl PartialOrd for ModuleStr<'_> {
  fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
    Some(self.cmp(other))
  }
}

impl Hash for ModuleStr<'_> {
  fn hash<H: Hasher>(&self, state: &mut H) {
    Into::<&str>::into(*self).hash(state)
  }
}

impl From<&str> for ModuleStr<'_> {
  fn from(s: &str) -> Self {
    Self::new(s)
  }
}

impl From<ModuleStr<'_>> for &'_ str {
  fn from(s: ModuleStr<'_>) -> Self {
    if s.ptr.is_null() {
      ""
    } else {
      unsafe { std::str::from_utf8_unchecked(std::slice::from_raw_parts(s.ptr, s.len)) }
    }
  }
}

impl Borrow<str> for ModuleStr<'_> {
  fn borrow(&self) -> &str {
    (*self).into()
  }
}

impl AsRef<str> for ModuleStr<'_> {
  fn as_ref(&self) -> &str {
    (*self).into()
  }
}

impl Display for ModuleStr<'_> {
  fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
    Into::<&str>::into(*self).fmt(f)
  }
}

impl FromStr for ModuleStr<'_> {
  type Err = Infallible;

  fn from_str(s: &str) -> Result<Self, Self::Err> {
    Ok(Self::new(s))
  }
}

#[cfg(test)]
mod tests {
  use super::*;
  use crate::abi;
  use std::collections::HashMap;
  use std::mem::offset_of;

  #[test]
  fn test_module_str_conversion() {
    let s = ModuleStr::new("test");
    assert_eq!(s.to_string(), "test");

    let s = ModuleStr::default();
    assert_eq!(s.to_string(), "");

    let s = ModuleStr::from("");
    assert_eq!(s.to_string(), "");
    assert_eq!(ModuleStr::default(), ModuleStr::from(""));

    // Test hash map
    let mut str_map = HashMap::new();
    str_map.insert(ModuleStr::new("test"), "value");
    assert_eq!(str_map.get(&ModuleStr::new("test")), Some(&"value"));
    assert_eq!(str_map.get("test"), Some(&"value"));

    // Test ordering
    let a = ModuleStr::new("a");
    let b = ModuleStr::new("b");
    assert!(a < b);
    assert!(a <= b);
    assert!(a <= a);
    assert!(b > a);
    assert!(b >= a);
    assert!(b >= b);
  }

  #[test]
  fn test_module_str_equivalent_layout() {
    // TODO: Create/find a proc macro to do this automatically.
    assert!(
      offset_of!(ModuleStr<'_>, ptr) == offset_of!(abi::envoy_dynamic_module_type_module_str, ptr)
    );
    assert!(
      offset_of!(ModuleStr<'_>, len)
        == offset_of!(abi::envoy_dynamic_module_type_module_str, length)
    );
    assert!(
      std::mem::size_of::<ModuleStr<'_>>()
        == std::mem::size_of::<abi::envoy_dynamic_module_type_module_str>()
    );
  }
}
