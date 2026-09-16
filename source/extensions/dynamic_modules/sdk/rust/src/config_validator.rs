//! xDS config validator support for dynamic modules.
//!
//! Config validators run before Envoy accepts an xDS update. Returning `Err(reason)` from a
//! validator rejects the update and Envoy sends a normal xDS NACK containing the reason. During a
//! validation the module can read current server state through the [`ConfigValidatorContext`].

#[cfg(not(test))]
use crate::bytes_to_module_buffer;
use crate::{abi, drop_wrapped_c_void_ptr, wrap_into_c_void_ptr};
use std::borrow::Cow;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::time::Duration;

/// A decoded xDS resource passed to a config validator.
///
/// All borrowed data is valid only during the current validation callback. Copy anything that must
/// outlive the callback.
pub struct ConfigValidatorResource<'a> {
  /// The resource name.
  pub name: Cow<'a, str>,
  /// The resource version.
  pub version: Cow<'a, str>,
  /// The resource aliases. Empty when the resource has no aliases.
  pub aliases: Vec<Cow<'a, str>>,
  /// The resource TTL, or `None` when the resource envelope carries no TTL.
  pub ttl: Option<Duration>,
  /// The serialized protobuf resource bytes, or `None` when the xDS resource envelope has no
  /// payload. `Some(&[])` represents a present protobuf message whose serialization is empty.
  pub resource: Option<&'a [u8]>,
}

/// Handle to the per-call validation context, giving read access to current server state.
///
/// It is valid only during the validation callback that received it.
pub struct ConfigValidatorContext {
  raw: abi::envoy_dynamic_module_type_config_validator_context_envoy_ptr,
}

impl ConfigValidatorContext {
  /// Returns the number of clusters currently added via the xDS API (dynamic clusters).
  pub fn dynamic_cluster_count(&self) -> u64 {
    #[cfg(not(test))]
    {
      unsafe {
        abi::envoy_dynamic_module_callback_config_validator_get_dynamic_cluster_count(self.raw)
      }
    }
    #[cfg(test)]
    {
      let _ = self.raw;
      0
    }
  }
}

/// Trait implemented by dynamic-module xDS config validators.
pub trait ConfigValidatorConfig: Send + Sync {
  /// Validates a State-of-the-World update.
  fn validate(
    &self,
    context: &ConfigValidatorContext,
    type_url: &str,
    resources: &[ConfigValidatorResource],
  ) -> Result<(), String>;

  /// Validates a delta xDS update.
  fn validate_delta(
    &self,
    context: &ConfigValidatorContext,
    type_url: &str,
    added_resources: &[ConfigValidatorResource],
    removed_resources: &[&str],
  ) -> Result<(), String>;
}

fn resources_from_abi<'a>(
  abi_resources: &'a [abi::envoy_dynamic_module_type_config_validator_resource],
) -> Vec<ConfigValidatorResource<'a>> {
  abi_resources
    .iter()
    .map(|resource| {
      let abi_aliases = unsafe {
        crate::ffi_helpers::slice_from_raw_or_empty(resource.aliases, resource.aliases_count)
      };
      let aliases = abi_aliases
        .iter()
        .map(|alias| unsafe {
          crate::ffi_helpers::str_lossy_from_raw(alias.ptr as *const u8, alias.length)
        })
        .collect();
      ConfigValidatorResource {
        name: unsafe {
          crate::ffi_helpers::str_lossy_from_raw(
            resource.name.ptr as *const u8,
            resource.name.length,
          )
        },
        version: unsafe {
          crate::ffi_helpers::str_lossy_from_raw(
            resource.version.ptr as *const u8,
            resource.version.length,
          )
        },
        aliases,
        ttl: resource
          .has_ttl
          .then_some(Duration::from_millis(resource.ttl_milliseconds)),
        resource: resource.has_resource.then(|| unsafe {
          crate::ffi_helpers::slice_from_raw_or_empty(
            resource.serialized_resource.ptr as *const u8,
            resource.serialized_resource.length,
          )
        }),
      }
    })
    .collect()
}

fn removed_resources_from_abi<'a>(
  abi_removed: &'a [abi::envoy_dynamic_module_type_envoy_buffer],
) -> Vec<Cow<'a, str>> {
  abi_removed
    .iter()
    .map(|removed_resource| unsafe {
      crate::ffi_helpers::str_lossy_from_raw(
        removed_resource.ptr as *const u8,
        removed_resource.length,
      )
    })
    .collect()
}

fn set_rejection_message(
  context_envoy_ptr: abi::envoy_dynamic_module_type_config_validator_context_envoy_ptr,
  message: &str,
) {
  #[cfg(not(test))]
  unsafe {
    abi::envoy_dynamic_module_callback_config_validator_set_rejection_message(
      context_envoy_ptr,
      bytes_to_module_buffer(message.as_bytes()),
    );
  }

  // Unit tests assert only the ABI return value, so they do not have an Envoy-side context object.
  // The C++ bridge tests cover rejection-message propagation end to end.
  #[cfg(test)]
  {
    let _ = context_envoy_ptr;
    let _ = message;
  }
}

/// # Safety
///
/// `config_module_ptr` must be a non-null pointer returned by [`init_config_validator_config`] and
/// must remain valid for `'a`.
unsafe fn config_from_ptr<'a>(
  config_module_ptr: abi::envoy_dynamic_module_type_config_validator_config_module_ptr,
) -> &'a dyn ConfigValidatorConfig {
  let raw = config_module_ptr as *const *const dyn ConfigValidatorConfig;
  unsafe { &**raw }
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed by
/// the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_config_validator_config_new(
  _config_envoy_ptr: abi::envoy_dynamic_module_type_config_validator_config_envoy_ptr,
  name: abi::envoy_dynamic_module_type_envoy_buffer,
  config: abi::envoy_dynamic_module_type_envoy_buffer,
) -> abi::envoy_dynamic_module_type_config_validator_config_module_ptr {
  catch_unwind(AssertUnwindSafe(|| {
    let name_str =
      unsafe { crate::ffi_helpers::str_lossy_from_raw(name.ptr as *const u8, name.length) };
    let config_slice = unsafe {
      crate::ffi_helpers::slice_from_raw_or_empty(config.ptr as *const u8, config.length)
    };
    init_config_validator_config(
      name_str.as_ref(),
      config_slice,
      crate::NEW_CONFIG_VALIDATOR_CONFIG_FUNCTION
        .get()
        .expect("NEW_CONFIG_VALIDATOR_CONFIG_FUNCTION must be set"),
    )
  }))
  .unwrap_or_else(|panic| {
    crate::log_ffi_panic("envoy_dynamic_module_on_config_validator_config_new", panic);
    std::ptr::null()
  })
}

/// Testable wrapper for [`envoy_dynamic_module_on_config_validator_config_new`].
pub(crate) fn init_config_validator_config(
  name: &str,
  config: &[u8],
  new_config_fn: &crate::NewConfigValidatorConfigFunction,
) -> abi::envoy_dynamic_module_type_config_validator_config_module_ptr {
  match new_config_fn(name, config) {
    Some(config) => wrap_into_c_void_ptr!(config),
    None => std::ptr::null(),
  }
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed by
/// the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_config_validator_config_destroy(
  config_module_ptr: abi::envoy_dynamic_module_type_config_validator_config_module_ptr,
) {
  let _ = catch_unwind(AssertUnwindSafe(|| {
    drop_wrapped_c_void_ptr!(config_module_ptr, ConfigValidatorConfig);
  }))
  .map_err(|panic| {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_config_validator_config_destroy",
      panic,
    );
  });
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed by
/// the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_config_validator_validate(
  context_envoy_ptr: abi::envoy_dynamic_module_type_config_validator_context_envoy_ptr,
  config_module_ptr: abi::envoy_dynamic_module_type_config_validator_config_module_ptr,
  type_url: abi::envoy_dynamic_module_type_envoy_buffer,
  resources: *const abi::envoy_dynamic_module_type_config_validator_resource,
  resources_count: usize,
) -> bool {
  catch_unwind(AssertUnwindSafe(|| {
    let config = unsafe { config_from_ptr(config_module_ptr) };
    let context = ConfigValidatorContext {
      raw: context_envoy_ptr,
    };
    let type_url_str =
      unsafe { crate::ffi_helpers::str_lossy_from_raw(type_url.ptr as *const u8, type_url.length) };
    let abi_resources =
      unsafe { crate::ffi_helpers::slice_from_raw_or_empty(resources, resources_count) };
    let resources = resources_from_abi(abi_resources);
    match config.validate(&context, type_url_str.as_ref(), &resources) {
      Ok(()) => true,
      Err(reason) => {
        set_rejection_message(context_envoy_ptr, &reason);
        false
      },
    }
  }))
  .unwrap_or_else(|panic| {
    crate::log_ffi_panic("envoy_dynamic_module_on_config_validator_validate", panic);
    set_rejection_message(context_envoy_ptr, "the module panicked during validation");
    false
  })
}

/// # Safety
///
/// This is an FFI function called by Envoy. All pointer arguments must be valid as guaranteed by
/// the Envoy dynamic module ABI.
#[no_mangle]
pub unsafe extern "C" fn envoy_dynamic_module_on_config_validator_validate_delta(
  context_envoy_ptr: abi::envoy_dynamic_module_type_config_validator_context_envoy_ptr,
  config_module_ptr: abi::envoy_dynamic_module_type_config_validator_config_module_ptr,
  type_url: abi::envoy_dynamic_module_type_envoy_buffer,
  added_resources: *const abi::envoy_dynamic_module_type_config_validator_resource,
  added_resources_count: usize,
  removed_resources: *const abi::envoy_dynamic_module_type_envoy_buffer,
  removed_resources_count: usize,
) -> bool {
  catch_unwind(AssertUnwindSafe(|| {
    let config = unsafe { config_from_ptr(config_module_ptr) };
    let context = ConfigValidatorContext {
      raw: context_envoy_ptr,
    };
    let type_url_str =
      unsafe { crate::ffi_helpers::str_lossy_from_raw(type_url.ptr as *const u8, type_url.length) };
    let abi_added_resources = unsafe {
      crate::ffi_helpers::slice_from_raw_or_empty(added_resources, added_resources_count)
    };
    let added_resources = resources_from_abi(abi_added_resources);
    let abi_removed_resources = unsafe {
      crate::ffi_helpers::slice_from_raw_or_empty(removed_resources, removed_resources_count)
    };
    let removed_resource_names = removed_resources_from_abi(abi_removed_resources);
    let removed_resource_refs: Vec<&str> = removed_resource_names
      .iter()
      .map(|name| name.as_ref())
      .collect();
    match config.validate_delta(
      &context,
      type_url_str.as_ref(),
      &added_resources,
      &removed_resource_refs,
    ) {
      Ok(()) => true,
      Err(reason) => {
        set_rejection_message(context_envoy_ptr, &reason);
        false
      },
    }
  }))
  .unwrap_or_else(|panic| {
    crate::log_ffi_panic(
      "envoy_dynamic_module_on_config_validator_validate_delta",
      panic,
    );
    set_rejection_message(context_envoy_ptr, "the module panicked during validation");
    false
  })
}

#[cfg(test)]
mod tests {
  use super::*;
  use std::sync::atomic::{AtomicBool, Ordering};

  struct TestConfigValidator {
    reject: bool,
    panic: bool,
  }

  impl ConfigValidatorConfig for TestConfigValidator {
    fn validate(
      &self,
      context: &ConfigValidatorContext,
      type_url: &str,
      resources: &[ConfigValidatorResource],
    ) -> Result<(), String> {
      if self.panic {
        panic!("sotw panic");
      }
      assert_eq!(context.dynamic_cluster_count(), 0);
      assert_eq!(
        type_url,
        "type.googleapis.com/envoy.config.cluster.v3.Cluster"
      );
      assert_eq!(resources.len(), 1);
      assert_eq!(resources[0].name.as_ref(), "cluster_0");
      assert_eq!(resources[0].version.as_ref(), "version_0");
      assert_eq!(resources[0].resource, Some(&b"serialized_cluster"[..]));
      if self.reject {
        Err("sotw rejected".to_string())
      } else {
        Ok(())
      }
    }

    fn validate_delta(
      &self,
      _context: &ConfigValidatorContext,
      type_url: &str,
      added_resources: &[ConfigValidatorResource],
      removed_resources: &[&str],
    ) -> Result<(), String> {
      if self.panic {
        panic!("delta panic");
      }
      assert_eq!(
        type_url,
        "type.googleapis.com/envoy.config.cluster.v3.Cluster"
      );
      assert_eq!(added_resources.len(), 1);
      assert_eq!(added_resources[0].name.as_ref(), "cluster_0");
      assert_eq!(removed_resources, ["cluster_1"]);
      if self.reject {
        Err("delta rejected".to_string())
      } else {
        Ok(())
      }
    }
  }

  fn make_config(reject: bool, panic: bool) -> Box<dyn ConfigValidatorConfig> {
    Box::new(TestConfigValidator { reject, panic })
  }

  fn make_resource(
    name: &str,
    version: &str,
    resource: &[u8],
    has_resource: bool,
  ) -> abi::envoy_dynamic_module_type_config_validator_resource {
    abi::envoy_dynamic_module_type_config_validator_resource {
      name: abi::envoy_dynamic_module_type_envoy_buffer {
        ptr: name.as_ptr() as *const _,
        length: name.len(),
      },
      version: abi::envoy_dynamic_module_type_envoy_buffer {
        ptr: version.as_ptr() as *const _,
        length: version.len(),
      },
      aliases: std::ptr::null(),
      aliases_count: 0,
      has_ttl: false,
      ttl_milliseconds: 0,
      has_resource,
      serialized_resource: abi::envoy_dynamic_module_type_envoy_buffer {
        ptr: resource.as_ptr() as *const _,
        length: resource.len(),
      },
    }
  }

  fn type_url_buffer() -> abi::envoy_dynamic_module_type_envoy_buffer {
    let type_url = "type.googleapis.com/envoy.config.cluster.v3.Cluster";
    abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: type_url.as_ptr() as *const _,
      length: type_url.len(),
    }
  }

  #[test]
  fn resource_payload_presence_is_preserved() {
    let present_empty = make_resource("present", "version_0", b"", true);
    let absent = make_resource("", "version_1", b"", false);
    let abi_resources = [present_empty, absent];

    let resources = resources_from_abi(&abi_resources);

    assert!(matches!(resources[0].resource, Some(resource) if resource.is_empty()));
    assert_eq!(resources[1].resource, None);
    assert_eq!(resources[1].name.as_ref(), "");
  }

  #[test]
  fn resource_aliases_and_ttl_are_parsed() {
    let alias_0 = "alias_0";
    let alias_1 = "alias_1";
    let abi_aliases = [
      abi::envoy_dynamic_module_type_envoy_buffer {
        ptr: alias_0.as_ptr() as *const _,
        length: alias_0.len(),
      },
      abi::envoy_dynamic_module_type_envoy_buffer {
        ptr: alias_1.as_ptr() as *const _,
        length: alias_1.len(),
      },
    ];
    let mut resource = make_resource("cluster_0", "version_0", b"", false);
    resource.aliases = abi_aliases.as_ptr();
    resource.aliases_count = abi_aliases.len();
    resource.has_ttl = true;
    resource.ttl_milliseconds = 5000;
    let abi_resources = [resource];

    let resources = resources_from_abi(&abi_resources);

    assert_eq!(resources[0].aliases.len(), 2);
    assert_eq!(resources[0].aliases[0].as_ref(), "alias_0");
    assert_eq!(resources[0].aliases[1].as_ref(), "alias_1");
    assert_eq!(resources[0].ttl, Some(Duration::from_millis(5000)));
  }

  #[test]
  fn context_reader_returns_default_in_tests() {
    let context = ConfigValidatorContext {
      raw: std::ptr::null_mut(),
    };
    assert_eq!(context.dynamic_cluster_count(), 0);
  }

  #[test]
  fn config_new_some_and_none() {
    let new_fn: crate::NewConfigValidatorConfigFunction =
      |_name, _config| Some(make_config(false, false));
    let config_ptr = init_config_validator_config("validator", b"config", &new_fn);
    assert!(!config_ptr.is_null());
    unsafe {
      envoy_dynamic_module_on_config_validator_config_destroy(config_ptr);
    }

    let new_fn: crate::NewConfigValidatorConfigFunction = |_name, _config| None;
    let config_ptr = init_config_validator_config("validator", b"config", &new_fn);
    assert!(config_ptr.is_null());
  }

  #[test]
  fn config_destroy_drops_inner_config() {
    static DROPPED: AtomicBool = AtomicBool::new(false);

    struct DroppingConfig;
    impl Drop for DroppingConfig {
      fn drop(&mut self) {
        DROPPED.store(true, Ordering::SeqCst);
      }
    }
    impl ConfigValidatorConfig for DroppingConfig {
      fn validate(
        &self,
        _context: &ConfigValidatorContext,
        _type_url: &str,
        _resources: &[ConfigValidatorResource],
      ) -> Result<(), String> {
        Ok(())
      }

      fn validate_delta(
        &self,
        _context: &ConfigValidatorContext,
        _type_url: &str,
        _added_resources: &[ConfigValidatorResource],
        _removed_resources: &[&str],
      ) -> Result<(), String> {
        Ok(())
      }
    }

    let new_fn: crate::NewConfigValidatorConfigFunction =
      |_name, _config| Some(Box::new(DroppingConfig));
    let config_ptr = init_config_validator_config("validator", b"config", &new_fn);
    unsafe {
      envoy_dynamic_module_on_config_validator_config_destroy(config_ptr);
    }
    assert!(DROPPED.load(Ordering::SeqCst));
  }

  #[test]
  fn validate_accept_reject_and_panic() {
    let resource = make_resource("cluster_0", "version_0", b"serialized_cluster", true);

    for (reject, panic, expected) in [
      (false, false, true),
      (true, false, false),
      (false, true, false),
    ] {
      let config = make_config(reject, panic);
      let config_ptr = wrap_into_c_void_ptr!(config);
      let accepted = unsafe {
        envoy_dynamic_module_on_config_validator_validate(
          std::ptr::null_mut(),
          config_ptr,
          type_url_buffer(),
          &resource,
          1,
        )
      };
      assert_eq!(accepted, expected);
      unsafe {
        envoy_dynamic_module_on_config_validator_config_destroy(config_ptr);
      }
    }
  }

  #[test]
  fn validate_delta_accept_reject_and_panic() {
    let resource = make_resource("cluster_0", "version_0", b"serialized_cluster", true);
    let removed = "cluster_1";
    let removed_buffer = abi::envoy_dynamic_module_type_envoy_buffer {
      ptr: removed.as_ptr() as *const _,
      length: removed.len(),
    };

    for (reject, panic, expected) in [
      (false, false, true),
      (true, false, false),
      (false, true, false),
    ] {
      let config = make_config(reject, panic);
      let config_ptr = wrap_into_c_void_ptr!(config);
      let accepted = unsafe {
        envoy_dynamic_module_on_config_validator_validate_delta(
          std::ptr::null_mut(),
          config_ptr,
          type_url_buffer(),
          &resource,
          1,
          &removed_buffer,
          1,
        )
      };
      assert_eq!(accepted, expected);
      unsafe {
        envoy_dynamic_module_on_config_validator_config_destroy(config_ptr);
      }
    }
  }
}
