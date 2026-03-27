
# Dynamic Modules ABI Style Guide

## Overview

This document defines the naming conventions and style guidelines for the Dynamic Modules ABI header (`abi.h`). All ABI definitions must follow these conventions to maintain consistency and clarity across the codebase.

## Function Signature Style

Dynamic module mainly has two types of functions: event hooks and callbacks. The event hooks are implemented by the dynamic module and called by Envoy, while the callbacks are implemented by Envoy and called by the dynamic module.

### Return Values

For all event hooks, the return type could be any reasonable type that fits the purpose of the event hook.

For callbacks, the return type should be one of the following:
- For functions that will return single simple values (`number`, `bool`, `enum`, `pointer`), and function calling never fails or zero has no difference as calling failure, return the simple type directly (e.g., `size_t`, `bool`, enum type, pointer type).
- For functions that will return complex values (structs, arrays), or return multiple values, or need to indicate calling failure, return the value via output parameters and use a `bool` or `enum` as the return type to indicate success or failure.


And for both event hooks and callbacks, follow these guidelines:

- Always explicitly document the return type and its meaning
- For pointer types, document ownership semantics
- For null returns, document what it signifies (success, failure, etc.)

**Example:**
```c
/**
 * @return envoy_dynamic_module_type_http_filter_module_ptr is the pointer to the in-module HTTP
 * filter. Returning nullptr indicates a failure to initialize the module.
 */
```

### Parameters

- List parameters in logical order: context/config first, then data
- Always include the full type name
- Document what each parameter represents
- For buffers, document lifetime and ownership
- Use consistent phrasing for similar parameters

**Example:**
```c
/**
 * @param filter_config_ptr is the pointer to the in-module HTTP filter configuration.
 * @param filter_envoy_ptr is the pointer to the DynamicModuleHttpFilter object.
 * @param end_of_stream is true if this is the final data frame.
 */
```

## Naming Conventions

### Type Names

All ABI types are prefixed with `envoy_dynamic_module_type_` to avoid naming conflicts.

**Format:**
```
envoy_dynamic_module_type_<name>
```

**Examples:**
- `envoy_dynamic_module_type_http_filter_module_ptr`
- `envoy_dynamic_module_type_envoy_buffer`
- `envoy_dynamic_module_type_http_header_type`

### Ownership Suffixes

Types use suffixes to indicate memory ownership:

- `_module_ptr`: Memory allocated and owned by the dynamic module
- `_envoy_ptr`: Memory allocated and owned by Envoy
- `_envoy_buffer`: Buffer owned by Envoy
- `_module_buffer`: Buffer owned by the dynamic module
- `_envoy_http_header`: HTTP header owned by Envoy
- `_module_http_header`: HTTP header owned by the dynamic module
- No suffix: Ownership specified in documentation

### Event Hook Names

All event hooks are prefixed with `envoy_dynamic_module_on_` and use snake_case for the remainder.

**Format:**
```
envoy_dynamic_module_on_<component>_<event>
```

**Examples:**
- `envoy_dynamic_module_on_program_init`
- `envoy_dynamic_module_on_http_filter_request_headers`
- `envoy_dynamic_module_on_http_filter_config_destroy`

### Callback Names

All callbacks are prefixed with `envoy_dynamic_module_callback_` and use snake_case.

**Format:**
```
envoy_dynamic_module_callback_<component>_<action>
```

**Examples:**
- `envoy_dynamic_module_callback_log`
- `envoy_dynamic_module_callback_http_get_metadata_value`

## Documentation Style

### Function Documentation

All functions must have a documentation block with:

1. **Description**: Brief explanation of when/why the function is called
2. **Parameters**: Document each parameter with `@param`
3. **Return Value**: Document with `@return`
4. **Additional Notes**: Ownership, threading, error conditions

**Template:**
```c
/**
 * envoy_dynamic_module_on_<name> <brief description>.
 *
 * <Detailed explanation of when/why this is called.>
 *
 * @param <name> <description>.
 * @return <type> <description of return values and their meanings>.
 */
```

### Type Documentation

Struct and enum documentation must include:

1. **Purpose**: What the type represents
2. **Ownership**: Who owns the memory (where applicable)
3. **Correspondence**: Related types in Envoy or the module
4. **Lifetime**: When the memory is valid

**Template:**
```c
/**
 * envoy_dynamic_module_type_<name> <brief description>.
 *
 * <Detailed explanation of the type's purpose and usage.>
 *
 * OWNERSHIP: <Module/Envoy> owns the pointer.
 */
```

## Enum Style

- Use descriptive names with enum prefix
- Group related values together
- Use snake_case with uppercase for enum value names
- Document the correspondence to C++ enums where applicable

**Example:**
```c
typedef enum envoy_dynamic_module_type_http_header_type {
    envoy_dynamic_module_type_http_header_type_RequestHeader,
    envoy_dynamic_module_type_http_header_type_RequestTrailer,
    envoy_dynamic_module_type_http_header_type_ResponseHeader,
    envoy_dynamic_module_type_http_header_type_ResponseTrailer,
} envoy_dynamic_module_type_http_header_type;
```

## Struct Style

- Use descriptive field names in snake_case
- Include size information for variable-length data
- Document field purposes in comments

**Example:**
```c
typedef struct envoy_dynamic_module_type_module_buffer {
    envoy_dynamic_module_type_buffer_module_ptr ptr;
    size_t length;
} envoy_dynamic_module_type_module_buffer;
```
