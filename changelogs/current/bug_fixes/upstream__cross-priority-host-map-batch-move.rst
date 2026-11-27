Fixed ``MainPrioritySetImpl::updateCrossPriorityHostMap`` evicting a replacement host when an
endpoint is replaced and moved across priorities within a single ``batchHostUpdate`` in an
order that adds the new host before removing the previous priority's host.
