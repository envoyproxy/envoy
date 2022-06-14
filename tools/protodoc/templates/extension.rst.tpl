.. _extension_{{extension}}:

This extension may be referenced by the qualified name ``{{extension}}``
{{contrib}}
.. note::
  {{status | indent(2) }}

  {{security_posture | indent(2) }}

.. tip::
  This extension extends and can be used with the following extension {% if categories|length > 1 %}categories{% else %}category{% endif %}:

{% for cat in categories %}
  - :ref:`{{cat}} <extension_category_{{cat}}>`
{% endfor %}
