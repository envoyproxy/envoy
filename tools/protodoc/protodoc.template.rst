{% macro header(title, underline="=") -%}
{{title}}
{{underline * title|length}}

{% endmacro -%}

{%- macro field_label(type, label) -%}
({% if label["repeated"] %}**repeated** {% endif %}{{rstlink(type)}}{% if label["required"] %}, *REQUIRED*{% endif %})
{%- endmacro -%}

{% macro wip_warning() -%}

.. warning::
  This API is work-in-progress and is subject to breaking changes.

{%- endmacro -%}

{% macro v2_link(link) -%}
This documentation is for the Envoy v3 API.

As of Envoy v1.18 the v2 API has been removed and is no longer supported.

If you are upgrading from v2 API config you may wish to view the v2 API documentation:

    :ref:`{{link["text"]}} <{{link["url"]}}>`


{% endmacro -%}

{% macro extension(ext) %}
.. _extension_{{ext["name"]}}:

This extension may be referenced by the qualified name ``{{ext["name"]}}``

.. note::
  {{ext["status"]}}

  {{ext["security_posture"]}}

.. tip::
  This extension extends and can be used with the following extension {% if ext["categories"]|length > 1 %}categories{% else %}category{% endif %}:

{% for cat in ext["categories"] %}
  - :ref:`{{cat}} <extension_category_{{cat}}>`
{% endfor %}
{% endmacro -%}

{% macro enum(_enum) -%}
.. _envoy_api_enum_{{_enum["proto"]}}:

{{header("Enum " + _enum["proto"], "-")}}
{%- if enum["proto_link"] -%}
`{{_enum["proto_link"]["text"]}} <{{_enum["proto_link"]["url"]}}>`_
{% endif -%}

{% for enum_value in _enum["values"] %}
.. _envoy_api_enum_value_{{enum_value.anchor}}:

{{enum_value.name}}
{{enum_value.comment}}

{% endfor %}
{% endmacro -%}

{% macro rstblock(name, body, title="") %}
.. {{name}}::{{" " + title if title else ""}}

{{body|indent(2, indentfirst=True)}}
{% endmacro %}

{% macro field_security(security) %}
{%- if security["options"].configure_for_untrusted_downstream %}
This field should be configured in the presence of untrusted *downstreams*.
{%- endif -%}
{%- if security["options"].configure_for_untrusted_upstream %}
This field should be configured in the presence of untrusted *upstreams*.
{%- endif -%}
{%- if security["edge_config"] %}
{{security["edge_config"]}}
{% endif -%}
Example configuration for untrusted environments:

{{rstblock("code-block", yaml.dump(security["example"]), "yaml")}}
{% endmacro %}

{%- macro field_body(msg_field) -%}
{{field_label(msg_field["type"], msg_field["label"])}} {{msg_field["info"]["comment"]}}

{%- if msg_field["oneof"] -%}
{%- if msg_field["oneof"]["required"] %}

Precisely one of XX must be set.
{% else %}

Only one of XX may be set.
{%- endif -%}
{%- endif -%}
{%- if msg_field["security_options"] %}
{{rstblock("attention", field_security(msg_field["security_options"]))}}
{%- endif -%}
{%- endmacro -%}

{%- macro rstlink(data) -%}
{%- if data["type"] == "external" -%}
`{{data["text"]}} <{{data["url"]}}>`_
{%- elif data["type"] == "api_enum" -%}
:ref:`{{data["text"]}} <envoy_api_enum_{{data["url"]}}>`
{%- elif data["type"] == "api_msg" -%}
:ref:`{{data["text"]}} <envoy_api_msg_{{data["url"]}}>`
{%- elif data["type"] == "api_field" -%}
:ref:`{{data["text"]}} <envoy_api_field_{{data["url"]}}>`
{%- else -%}
{{data["text"]}}
{%- endif -%}
{%- endmacro -%}

{%- macro message(msg) -%}
.. _envoy_api_msg_{{msg["proto"]}}:

{{header(msg["proto"], "-")}}
{%- if msg["proto_link"] -%}
`{{msg["proto_link"]["text"]}} <{{msg["proto_link"]["url"]}}>`_

{% endif -%}
{%- if msg["comment"] -%}
{{msg["comment"]}}
{%- endif -%}
{%- if msg["config"] -%}
{{rstblock("code-block", json.dumps(msg["config"], indent=2), "json")}}
{%- endif -%}
{%- for msg_field in msg["fields"] -%}
{%- if msg_field %}
.. _envoy_api_field_{{msg_field["proto"]}}:

{{msg_field["name"]}}
{{field_body(msg_field)|indent(2, indentfirst=True)}}
{%- endif %}
{%- endfor -%}
{%- for nested_msg in msg["messages"] -%}
{{message(nested_msg)}}
{%- endfor -%}
{%- for nested_enum in msg["enums"] -%}
{{enum(nested_enum)}}
{%- endfor -%}
{%- endmacro -%}

{% if not has_messages -%}
:orphan:

{% endif -%}
.. _envoy_v3_api_file_{{file["proto"]}}:

{{header(file["title"] or file["proto"])}}
{% if file["v2_link"] -%}
{{v2_link(file["v2_link"])}}
{%- endif -%}
{%- if file["extension"] -%}
{{extension(file["extension"])}}
{%- endif %}
{%- if file["comment"] -%}
{{file["comment"]}}
{%- endif -%}
{%- if file["work_in_progress"] -%}
{{wip_warning()}}
{%- endif -%}
{%- if has_messages -%}
{% for msg in msgs %}
{{message(msg)}}
{%- endfor %}
{% for _enum in enums %}
{{enum(_enum)}}
{% endfor %}
{% else %}

{% endif %}
