# On-Demand RDS/VHDS
Currently, in RDS, all routes for the cluster are sent to every Envoy instance in the mesh. This causes scaling issues as the size of the cluster grows. The majority of this complexity can be found in the virtual host configurations, of which most are not needed by any individual sidecar. With a goal of being able to scale to one million vhosts in the future, the capability to filter a sidecar to only contain the required virtual hosts is a necessity.  

In order to fix this issue, we are implementing on-demand RDS. On-demand RDS, which uses the delta xDS protocol, is an independent protocol ([DeltaAggregatedResources](https://github.com/envoyproxy/envoy/blob/master/api/envoy/service/discovery/v2/ads.proto#L35)) from normal xDS. Instead of always sending the full route config, Envoy will be able to subscribe/unsubscribe from a list of resources stored internally in Istio. Istio will monitor this list and use it to filter the configuration sent to an individual Envoy instance to only contain the subscribed resources. Initially, Istio will only support delta RDS since that’s where the bulk of the configuration is coming from. 

## Joining/Reconnecting
When an Envoy instance joins the cluster for the first time, it will receive a base configuration filtered down to a subset of routes that are likely useful to the Envoy instance. For example, these might be filtered down by routes that exist in the current namespace. Pilot will send this same collection of resources on reconnect.  

# BAVERY_ADD_IMAGE

## Requesting Additional Resources
In order to request additional resources, Envoy will send a [DeltaDiscoveryRequest](https://github.com/envoyproxy/data-plane-api/blob/master/envoy/api/v2/discovery.proto#L137) to Pilot. Pilot will then respond with an [DeltaDiscoveryResponse](https://github.com/envoyproxy/data-plane-api/blob/master/envoy/api/v2/discovery.proto#L193) which is followed up by an Ack/Nack [DeltaDiscoveryRequest](https://github.com/envoyproxy/data-plane-api/blob/master/envoy/api/v2/discovery.proto#L137) from Envoy. 
    
# BAVERY_ADD_IMAGE

## Virtual Host Discovery Service
In order to support on-demand, an additional protocol, VHDS, will be added. This protocol allows a separation of concerns with RDS where RDS is in charge of maintaining route configs while VHDS is in charge of communicating virtual hosts. In VHDS, Envoy will send an [DeltaDiscoveryRequest](https://github.com/envoyproxy/data-plane-api/blob/master/envoy/api/v2/discovery.proto#L137) with type_url set to type.googleapis.com/envoy.api.v2.route.VirtualHost and resource_names set to a list of routeconfig names + domains for which it would like configuration. The management server will respond with an [DeltaDiscoveryResponse](https://github.com/envoyproxy/data-plane-api/blob/master/envoy/api/v2/discovery.proto#L193) with the resources field populated with the specified virtual hosts, the name field populated with the virtual host name, and the alias field populated with the explicit (no wildcard) domains of the virtual host. Future updates to these virtual hosts will be sent via spontaneous updates.

## Unsubscribing from Virtual Hosts
Virtual hosts can be unsubscribed to via a [DeltaDiscoveryRequest](https://github.com/envoyproxy/data-plane-api/blob/master/envoy/api/v2/discovery.proto#L137) with their route config names + domains provided in the resource_names_unsubscribe field. Envoy will remove any route config names + domains that it finds in the [DeltaDiscoveryRequest](https://github.com/envoyproxy/data-plane-api/blob/master/envoy/api/v2/discovery.proto#L137) removed_resources field. 

## Subscribing to Virtual Hosts
Envoy will send a [DeltaDiscoveryRequest](https://github.com/envoyproxy/data-plane-api/blob/master/envoy/api/v2/discovery.proto#L137) with the resource_names_subscribe field populated with the route config names + domains of each of the resources that it would like to subscribe to. Each of the virtual hosts contained in the [DeltaDiscoveryRequest's](https://github.com/envoyproxy/data-plane-api/blob/master/envoy/api/v2/discovery.proto#L137) resources field will be added to the route configuration maintained by Envoy. If Envoy's route configuration already contains a given virtual host, it will be overwritten by data received in the [DeltaDiscoveryResponse](https://github.com/envoyproxy/data-plane-api/blob/master/envoy/api/v2/discovery.proto#L193). During spontaneous updates configuration server will only send updates for virtual hosts that Envoy is aware of -- the configuration server needs to keep track of virtual hosts known to Envoy.

## Updates of non-virtual host fields
It might be required to update non-virtual host fields of route configuration while keeping virtual hosts as is. This would probably happen during spontaneous updates only. In such a case a [DeltaDiscoveryResponse](https://github.com/envoyproxy/data-plane-api/blob/master/envoy/api/v2/discovery.proto#L193) containing a route configuration with an empty virtual hosts list can be used.

## Compatibility with Scoped RDS
Both approaches appear to be compatible with scoped RDS proposal: route_configuration name can still be used for vhost matching, but with scoped RDS configured it would point to a scoped route configuration.

Pilot will also support the ability of Envoy to tell it when a resource hasn’t been used and is safe to stop monitoring. The resources that can be removed include the base resources that Pilot initially sent Envoy. 

Any future updates to this resource will be detected by Pilot as part of the normal xDS flow and sent along with normal cluster updates. Istio will continue to use plain xDS for CDS, LDS, EDS, and the other protocols. 

## Full flow example:

# BAVERY_ADD_IMAGE

## Delta RDS Full State Machine

# BAVERY_ADD_IMAGE

## Example pilot-generated RDS:
```json 
{
   "2001": {
     "name": "2001",
     "virtual_hosts": [
         "name": "s1http.none:2001",
         "domains": [
           "s1http.none",
           "s1http.none:2001"
         ],
         "routes": [
           
             "match": {
               "PathSpecifier": {
                 "Prefix": "/"
               }
             },
             "Action": {
               "Route": {
                 "ClusterSpecifier": {
                   "Cluster": "outbound|2001||s1http.none"
                 },
                 "HostRewriteSpecifier": null,
                 "timeout": 0,
                 "retry_policy": {
                   "retry_on": "connect-failure,refused-stream,unavailable,cancelled,resource-exhausted",
                   "num_retries": {
                     "value": 10
                   },
                   "retry_host_predicate": [
                     
                       "name": "envoy.retry_host_predicates.previous_hosts",
                       "ConfigType": null
                     }
                   ],
                   "host_selection_retry_max_attempts": 3,
                   "retriable_status_codes": [
                     503
                   ]
                 },
                 "max_grpc_timeout": 0
               }
             },
             "decorator": {
               "operation": "s1http.none:2001/*"
             },
             "per_filter_config": {
               "mixer": {
                 "fields": {
                   "disable_check_calls": {
                     "Kind": {
                       "BoolValue": true
                     }
                   },
                   "mixer_attributes": {
                     "Kind": {
                       "StructValue": {
                         "fields": {
                           "attributes": {
                             "Kind": {
                               "StructValue": {
                                 "fields": {
                                   "destination.service.host": {
                                     "Kind": {
                                       "StructValue": {
                                         "fields": {
                                           "string_value": {
                                             "Kind": {
                                               "StringValue": "s1http.none"
                                             }
                                           }
                                         }
                                       }
                                     }
                                   },
                                   "destination.service.name": {
                                     "Kind": {
                                       "StructValue": {
                                         "fields": {
                                           "string_value": {
                                             "Kind": {
                                               "StringValue": "s1http.none"
                                             }
                                           }
                                         }
                                       }
                                     }
                                   },
                                   "destination.service.namespace": {
                                     "Kind": {
                                       "StructValue": {
                                         "fields": {
                                           "string_value": {
                                             "Kind": {
                                               "StringValue": "none"
                                              }
                                           }
                                         }
                                       }
                                     }
                                   }
                                 }
                               }
                             }
                           }
                         }
                       }
                     }
                   }
                 }
               }
             }
           }
          ]
       }
     ],
     "validate_clusters": {}
   },


```
