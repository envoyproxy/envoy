local ffi         = require("ffi")
local ffi_cdef    = ffi.cdef
local C           = ffi.C


local _M = {version = 0.1}


ffi_cdef[[
  int envoy_time();
]]


function _M.time()
  return C.envoy_time()
end


return _M
