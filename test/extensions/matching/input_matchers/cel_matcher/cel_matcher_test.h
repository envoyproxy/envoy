#pragma once

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace CelMatcher {

// Compiled CEL expression string: request.headers['authenticated_user'] == 'staging'
inline constexpr char RequestHeaderCelExprString[] = R"pb(
  expr {
    id: 8
    call_expr {
      function: "_==_"
      args {
        id: 6
        call_expr {
          function: "_[_]"
          args {
            id: 5
            select_expr {
              operand {
                id: 4
                ident_expr {name: "request"}
              }
              field: "headers"
            }
          }
          args {
            id: 7
            const_expr {
              string_value: "authenticated_user"
            }
          }
        }
      }
      args {
        id: 9
        const_expr { string_value: "staging" }
      }
    }
  }
)pb";

// Compiled CEL expression string: request.path == '/foo'
inline constexpr char RequestPathCelExprString[] = R"pb(
  expr {
    id: 3
    call_expr {
      function: "_==_"
      args {
        id: 2
        select_expr {
          operand {
            id: 1
            ident_expr {
              name: "request"
            }
          }
          field: "path"
        }
      }
      args {
        id: 4
        const_expr {
          string_value: "/foo"
        }
      }
    }
  }
)pb";

// Compiled CEL expression string: response.headers['content-type'] == 'text/plain'
inline constexpr char ReponseHeaderCelExprString[] = R"pb(
  expr {
      id: 8
      call_expr {
        function: "_==_"
        args {
          id: 6
          call_expr {
            function: "_[_]"
            args {
              id: 5
              select_expr {
                operand {
                  id: 4
                  ident_expr {name: "response"}
                }
                field: "headers"
              }
            }
            args {
              id: 7
              const_expr {
                string_value: "content-type"
              }
            }
          }
        }
        args {
          id: 9
          const_expr { string_value: "text/plain" }
        }
      }
    }
)pb";

// Compiled CEL expression string: request.path == '/foo' && request.headers['user'] ==
// 'staging'.
// Note, source_info is not required for evaluation process that happens in data plane, which is
// confirmed by test cases above. However, it will be included as part of result from parsing and
// checking process passed to data plane. Thus, the entire compiled CEL result is also tested.
inline constexpr char RequestHeaderAndPathCelString[] = R"pb(
    expr {
      id: 11
      call_expr {
        function: "_&&_"
        args {
          id: 3
          call_expr {
            function: "_==_"
            args {
              id: 2
              select_expr {
                operand {
                  id: 1
                  ident_expr {
                    name: "request"
                  }
                }
                field: "path"
              }
            }
            args {
              id: 4
              const_expr {
                string_value: "/foo"
              }
            }
          }
        }
        args {
          id: 9
          call_expr {
            function: "_==_"
            args {
              id: 7
              call_expr {
                function: "_[_]"
                args {
                  id: 6
                  select_expr {
                    operand {
                      id: 5
                      ident_expr {
                        name: "request"
                      }
                    }
                    field: "headers"
                  }
                }
                args {
                  id: 8
                  const_expr {
                    string_value: "user"
                  }
                }
              }
            }
            args {
              id: 10
              const_expr {
                string_value: "staging"
              }
            }
          }
        }
      }
    }
    source_info {
      location: "<input>"
      line_offsets: 57
      positions {
        key: 1
        value: 0
      }
      positions {
        key: 2
        value: 7
      }
      positions {
        key: 3
        value: 12
      }
      positions {
        key: 4
        value: 14
      }
      positions {
        key: 5
        value: 22
      }
      positions {
        key: 6
        value: 29
      }
      positions {
        key: 7
        value: 37
      }
      positions {
        key: 8
        value: 38
      }
      positions {
        key: 9
        value: 45
      }
      positions {
        key: 10
        value: 47
      }
      positions {
        key: 11
        value: 20
      }
    }
)pb";

// Compiled CEL expression string: response.trailers['transfer-encoding']=='chunked'
inline constexpr char ReponseTrailerCelExprString[] = R"pb(
  expr {
    id: 5
    call_expr {
      function: "_==_"
      args {
        id: 3
        call_expr {
          function: "_[_]"
          args {
            id: 2
            select_expr {
              operand {
                id: 1
                ident_expr {
                  name: "response"
                }
              }
              field: "trailers"
            }
          }
          args {
            id: 4
            const_expr {
              string_value: "transfer-encoding"
            }
          }
        }
      }
      args {
        id: 6
        const_expr {
          string_value: "chunked"
        }
      }
    }
  }
)pb";

// Compiled CEL expression string: request.path == '/foo' || request.headers['user'] ==
// 'staging'.
inline constexpr char RequestHeaderOrPathCelString[] = R"pb(
  expr {
    id: 11
    call_expr {
      function: "_||_"
      args {
        id: 3
        call_expr {
          function: "_==_"
          args {
            id: 2
            select_expr {
              operand {
                id: 1
                ident_expr {
                  name: "request"
                }
              }
              field: "path"
            }
          }
          args {
            id: 4
            const_expr {
              string_value: "/foo"
            }
          }
        }
      }
      args {
        id: 9
        call_expr {
          function: "_==_"
          args {
            id: 7
            call_expr {
              function: "_[_]"
              args {
                id: 6
                select_expr {
                  operand {
                    id: 5
                    ident_expr {
                      name: "request"
                    }
                  }
                  field: "headers"
                }
              }
              args {
                id: 8
                const_expr {
                  string_value: "user"
                }
              }
            }
          }
          args {
            id: 10
            const_expr {
              string_value: "staging"
            }
          }
        }
      }
    }
  }
)pb";

// request.headers['user']=='staging'&&response.headers['content-type']=='text/plain'&&response.trailers['transfer-encoding']=='chunked'
inline constexpr char RequestAndResponseCelString[] = R"pb(
  expr {
    id: 20
    call_expr {
      function: "_&&_"
      args {
        id: 13
        call_expr {
          function: "_&&_"
          args {
            id: 5
            call_expr {
              function: "_==_"
              args {
                id: 3
                call_expr {
                  function: "_[_]"
                  args {
                    id: 2
                    select_expr {
                      operand {
                        id: 1
                        ident_expr {
                          name: "request"
                        }
                      }
                      field: "headers"
                    }
                  }
                  args {
                    id: 4
                    const_expr {
                      string_value: "user"
                    }
                  }
                }
              }
              args {
                id: 6
                const_expr {
                  string_value: "staging"
                }
              }
            }
          }
          args {
            id: 11
            call_expr {
              function: "_==_"
              args {
                id: 9
                call_expr {
                  function: "_[_]"
                  args {
                    id: 8
                    select_expr {
                      operand {
                        id: 7
                        ident_expr {
                          name: "response"
                        }
                      }
                      field: "headers"
                    }
                  }
                  args {
                    id: 10
                    const_expr {
                      string_value: "content-type"
                    }
                  }
                }
              }
              args {
                id: 12
                const_expr {
                  string_value: "text/plain"
                }
              }
            }
          }
        }
      }
      args {
        id: 18
        call_expr {
          function: "_==_"
          args {
            id: 16
            call_expr {
              function: "_[_]"
              args {
                id: 15
                select_expr {
                  operand {
                    id: 14
                    ident_expr {
                      name: "response"
                    }
                  }
                  field: "trailers"
                }
              }
              args {
                id: 17
                const_expr {
                  string_value: "transfer-encoding"
                }
              }
            }
          }
          args {
            id: 19
            const_expr {
              string_value: "chunked"
            }
          }
        }
      }
    }
  }
  source_info {
    location: "<input>"
    line_offsets: 134
    positions {
      key: 1
      value: 0
    }
    positions {
      key: 2
      value: 7
    }
    positions {
      key: 3
      value: 15
    }
    positions {
      key: 4
      value: 16
    }
    positions {
      key: 5
      value: 23
    }
    positions {
      key: 6
      value: 25
    }
    positions {
      key: 7
      value: 36
    }
    positions {
      key: 8
      value: 44
    }
    positions {
      key: 9
      value: 52
    }
    positions {
      key: 10
      value: 53
    }
    positions {
      key: 11
      value: 68
    }
    positions {
      key: 12
      value: 70
    }
    positions {
      key: 13
      value: 34
    }
    positions {
      key: 14
      value: 84
    }
    positions {
      key: 15
      value: 92
    }
    positions {
      key: 16
      value: 101
    }
    positions {
      key: 17
      value: 102
    }
    positions {
      key: 18
      value: 122
    }
    positions {
      key: 19
      value: 124
    }
    positions {
      key: 20
      value: 82
    }
  }
)pb";

// xds.cluster_metadata.filter_metadata['cel_matcher']['service_name'] == 'test_service'
inline constexpr absl::string_view UpstreamClusterMetadataCelString = R"pb(
  expr {
    id: 8
    call_expr {
      function: "_==_"
      args {
        id: 6
        call_expr {
          function: "_[_]"
          args {
            id: 4
            call_expr {
              function: "_[_]"
              args {
                id: 3
                select_expr {
                  operand {
                    id: 2
                    select_expr {
                      operand {
                        id: 1
                        ident_expr {
                          name: "xds"
                        }
                      }
                      field: "cluster_metadata"
                    }
                  }
                  field: "filter_metadata"
                }
              }
              args {
                id: 5
                const_expr {
                  string_value: "%s"
                }
              }
            }
          }
          args {
            id: 7
            const_expr {
              string_value: "%s"
            }
          }
        }
      }
      args {
        id: 9
        const_expr {
          string_value: "%s"
        }
      }
    }
  }
)pb";

// xds.route_metadata.filter_metadata['cel_matcher']['service_name'] == 'test_service'
inline constexpr absl::string_view UpstreamRouteMetadataCelString = R"pb(
  expr {
    id: 8
    call_expr {
      function: "_==_"
      args {
        id: 6
        call_expr {
          function: "_[_]"
          args {
            id: 4
            call_expr {
              function: "_[_]"
              args {
                id: 3
                select_expr {
                  operand {
                    id: 2
                    select_expr {
                      operand {
                        id: 1
                        ident_expr {
                          name: "xds"
                        }
                      }
                      field: "route_metadata"
                    }
                  }
                  field: "filter_metadata"
                }
              }
              args {
                id: 5
                const_expr {
                  string_value: "%s"
                }
              }
            }
          }
          args {
            id: 7
            const_expr {
              string_value: "%s"
            }
          }
        }
      }
      args {
        id: 9
        const_expr {
          string_value: "%s"
        }
      }
    }
  }
)pb";

// metadata.filter_metadata['kFilterNamespace']['kMetadataKey'] == 'kMetadataValue'
inline constexpr absl::string_view DynamicMetadataCelString = R"pb(
  expr {
    id: 7
    call_expr {
      function: "_==_"
      args {
        id: 5
        call_expr {
          function: "_[_]"
          args {
            id: 3
            call_expr {
              function: "_[_]"
              args {
                id: 2
                select_expr {
                  operand {
                    id: 1
                    ident_expr {
                      name: "metadata"
                    }
                  }
                  field: "filter_metadata"
                }
              }
              args {
                id: 4
                const_expr {
                  string_value: "%s"
                }
              }
            }
          }
          args {
            id: 6
            const_expr {
              string_value: "%s"
            }
          }
        }
      }
      args {
        id: 8
        const_expr {
          string_value: "%s"
        }
      }
    }
  }
)pb";

} // namespace CelMatcher
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
