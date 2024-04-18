{%- for item in enum_items %}
{{ item.anchor | rst_anchor }}

{{ item.value.name }}
  {{ item.comment | indent(2) }}
{%- endfor -%}
