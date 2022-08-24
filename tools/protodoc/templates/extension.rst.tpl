.. _extension_{{extension}}:

This extension has the qualified name ``{{extension}}``

{{contrib}}
.. note::
  {{status | indent(2) }}

  {{security_posture | indent(2) }}

.. tip::
  This extension extends and can be used with the following extension {% if categories|length > 1 %}categories{% else %}category{% endif %}:

{% for cat in categories %}
  - :ref:`{{cat}} <extension_category_{{cat}}>`
{% endfor %}

{% if type_urls|length > 0 %}
  This extension must be configured with one of the following type URLs:

{% for type_url in type_urls %}
{% if type_url.startswith('envoy.') %}
  - :ref:`type.googleapis.com/{{type_url}} <envoy_v3_api_msg_{{type_url[6:]}}>`
{% else %}
  - ``type.googleapis.com/{{type_url}}``
{% endif -%}
{% endfor -%}
{% endif -%}
