Added a dynamic modules string data input extension
(``envoy.matching.inputs.dynamic_module_string_data_input``) that lets a dynamic module extract a
string value from an HTTP request or response during match evaluation. The value is a standard
string input, so map matchers such as an exact match map can dispatch on it, which lets a module
select one of many matches with a single evaluation and without clearing the route cache. The Rust
SDK exposes this through the ``matcher_data_input`` module and the ``declare_matcher_data_input!``
macro. See
:ref:`DynamicModuleDataInput <envoy_v3_api_msg_extensions.matching.http.dynamic_modules.v3.DynamicModuleDataInput>`
for configuration details.
