#pragma once

#include <sys/ioctl.h>
#include <sys/mman.h>   // for mode_t
#include <sys/socket.h> // for sockaddr
#include <sys/stat.h>
#include <sys/uio.h> // for iovec

#include <memory>
#include <string>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Api {

/**
 * SysCallResult holds the rc and errno values resulting from a system call.
 */
template <typename T> struct SysCallResult {

  /**
   * The return code from the system call.
   */
  T rc_;

  /**
   * The errno value as captured after the system call.
   */
  int errno_;
};

typedef SysCallResult<int> SysCallIntResult;
typedef SysCallResult<ssize_t> SysCallSizeResult;
typedef SysCallResult<void*> SysCallPtrResult;

class OsSysCalls {
public:
  virtual ~OsSysCalls() {}

  /**
   * @see bind (man 2 bind)
   */
  virtual SysCallIntResult bind(int sockfd, const sockaddr* addr, socklen_t addrlen) PURE;

  /**
   * @see ioctl (man 2 ioctl)
   */
  virtual SysCallIntResult ioctl(int sockfd, unsigned long int request, void* argp) PURE;

  /**
   * Open file by full_path with given flags and mode.
   * @return file descriptor.
   */
  virtual SysCallIntResult open(const std::string& full_path, int flags, int mode) PURE;

  /**
   * Write num_bytes to fd from buffer.
   * @return number of bytes written if non negative, otherwise error code.
   */
  virtual SysCallSizeResult write(int fd, const void* buffer, size_t num_bytes) PURE;

  /**
   * @see writev (man 2 writev)
   */
  virtual SysCallSizeResult writev(int fd, const iovec* iovec, int num_iovec) PURE;

  /**
   * @see readv (man 2 readv)
   */
  virtual SysCallSizeResult readv(int fd, const iovec* iovec, int num_iovec) PURE;

  /**
   * @see recv (man 2 recv)
   */
  virtual SysCallSizeResult recv(int socket, void* buffer, size_t length, int flags) PURE;

  /**
   * Release all resources allocated for fd.
   * @return zero on success, -1 returned otherwise.
   */
  virtual SysCallIntResult close(int fd) PURE;

  /**
   * @see shm_open (man 3 shm_open)
   */
  virtual SysCallIntResult shmOpen(const char* name, int oflag, mode_t mode) PURE;

  /**
   * @see shm_unlink (man 3 shm_unlink)
   */
  virtual SysCallIntResult shmUnlink(const char* name) PURE;

  /**
   * @see man 2 ftruncate
   */
  virtual SysCallIntResult ftruncate(int fd, off_t length) PURE;

  /**
   * @see man 2 mmap
   */
  virtual SysCallPtrResult mmap(void* addr, size_t length, int prot, int flags, int fd,
                                off_t offset) PURE;

  /**
   * @see man 2 stat
   */
  virtual SysCallIntResult stat(const char* pathname, struct stat* buf) PURE;

  /**
   * @see man 2 setsockopt
   */
  virtual SysCallIntResult setsockopt(int sockfd, int level, int optname, const void* optval,
                                      socklen_t optlen) PURE;

  /**
   * @see man 2 getsockopt
   */
  virtual SysCallIntResult getsockopt(int sockfd, int level, int optname, void* optval,
                                      socklen_t* optlen) PURE;

  /**
   * @see man 2 socket
   */
  virtual SysCallIntResult socket(int domain, int type, int protocol) PURE;
};

typedef std::unique_ptr<OsSysCalls> OsSysCallsPtr;

} // namespace Api
} // namespace Envoy
