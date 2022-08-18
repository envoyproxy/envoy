{%- if json_values %}
{%- for key, value in json_values %}
{%- if loop.index == 1 %}
.. code-block:: json
  :force:

  {
{%- endif %}
    "{{ key }}": {{ value }}{% if not loop.last %},{% endif %}
{%- if loop.last %}
  }
{%- endif -%}
{% endfor %}
{%- endif %}
{% for msg in msgs %}
{{ msg.anchor | rst_anchor }}

{{ msg.field_name }}
  ({{ pretty_label_names[msg.field.label] }}{{ msg.comment }}{% if msg.field_annotations %}, {{ msg.field_annotations }}{% endif %}) {{ msg.formatted_leading_comment | indent(2)}}
{%- if msg.formatted_oneof_comment %}
  {{ msg.formatted_oneof_comment | indent(2) }}
{%- endif %}

{%- if msg.security_options %}
{%- for section in msg.security_options %}
{%- if loop.index0 == 0 %}
  .. attention::

{%- endif -%}
    {{ section | indent(4) }}
{% endfor -%}
{%- endif -%}
{% endfor -%}
{% for message in nested_msgs %}
{{ message }}
{% endfor -%}
{%- for enum in nested_enums %}
{{ enum }}
{% endfor -%}
