#pragma once

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {

// true
inline constexpr char TrueCelString[] = R"pb(
  expr: {
    id: 1
    const_expr: {
      bool_value: true
    }
  }
)pb";

// false
inline constexpr char FalseCelString[] = R"pb(
  expr: {
    id: 1
    const_expr: {
      bool_value: false
    }
  }
)pb";

// context.sample(0.5, 1)
inline constexpr char SampleHalfCelString[] = R"pb(
  expr: {
    id: 2
    call_expr: {
      target: {
        id: 1
        ident_expr: {
          name: "context"
        }
      }
      function: "sample"
      args: {
        id: 3
        const_expr: {
          double_value: 0.5
        }
      }
      args: {
        id: 4
        const_expr: {
          int64_value: 1
        }
      }
    }
  }
)pb";

// context.sample(0.0, 1) || context.sample(0.0, 2) || context.sample(0.0, 3)
inline constexpr char SampleNeverCelString[] = R"pb(
  expr:  {
    id:  14
    call_expr:  {
      function:  "_||_"
      args:  {
        id:  9
        call_expr:  {
          function:  "_||_"
          args:  {
            id:  2
            call_expr:  {
              target:  {
                id:  1
                ident_expr:  {
                  name:  "context"
                }
              }
              function:  "sample"
              args:  {
                id:  3
                const_expr:  {
                  double_value:  0
                }
              }
              args:  {
                id:  4
                const_expr:  {
                  int64_value:  1
                }
              }
            }
          }
          args:  {
            id:  6
            call_expr:  {
              target:  {
                id:  5
                ident_expr:  {
                  name:  "context"
                }
              }
              function:  "sample"
              args:  {
                id:  7
                const_expr:  {
                  double_value:  0
                }
              }
              args:  {
                id:  8
                const_expr:  {
                  int64_value:  2
                }
              }
            }
          }
        }
      }
      args:  {
        id:  11
        call_expr:  {
          target:  {
            id:  10
            ident_expr:  {
              name:  "context"
            }
          }
          function:  "sample"
          args:  {
            id:  12
            const_expr:  {
              double_value:  0
            }
          }
          args:  {
            id:  13
            const_expr:  {
              int64_value:  3
            }
          }
        }
      }
    }
  }
)pb";

// context.sample(1.0, 1) && context.sample(1.0, 2) && context.sample(1.0, 3)
inline constexpr char SampleAlwaysCelString[] = R"pb(
  expr:  {
    id:  14
    call_expr:  {
      function:  "_&&_"
      args:  {
        id:  9
        call_expr:  {
          function:  "_&&_"
          args:  {
            id:  2
            call_expr:  {
              target:  {
                id:  1
                ident_expr:  {
                  name:  "context"
                }
              }
              function:  "sample"
              args:  {
                id:  3
                const_expr:  {
                  double_value:  1
                }
              }
              args:  {
                id:  4
                const_expr:  {
                  int64_value:  1
                }
              }
            }
          }
          args:  {
            id:  6
            call_expr:  {
              target:  {
                id:  5
                ident_expr:  {
                  name:  "context"
                }
              }
              function:  "sample"
              args:  {
                id:  7
                const_expr:  {
                  double_value:  1
                }
              }
              args:  {
                id:  8
                const_expr:  {
                  int64_value:  2
                }
              }
            }
          }
        }
      }
      args:  {
        id:  11
        call_expr:  {
          target:  {
            id:  10
            ident_expr:  {
              name:  "context"
            }
          }
          function:  "sample"
          args:  {
            id:  12
            const_expr:  {
              double_value:  1
            }
          }
          args:  {
            id:  13
            const_expr:  {
              int64_value:  3
            }
          }
        }
      }
    }
  }
)pb";

inline constexpr char SampleInvalidMap[] = R"pb(
  expr:  {
    id:  2
    call_expr:  {
      target:  {
        id:  1
        ident_expr:  {
          name:  "xds"
        }
      }
      function:  "sample"
      args:  {
        id:  3
        const_expr:  {
          double_value:  0.5
        }
      }
      args:  {
        id:  4
        const_expr:  {
          int64_value:  1
        }
      }
    }
  }
)pb";

} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
