#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define cstring_len(s) (sizeof(s) - 1)

#define roundup_32(n) (((n) + 3) & -4)

uint32_t wayland_current_id = 1;
uint32_t wayland_display_object_id = 1;

uint16_t wayland_registry_event_global = 0;

static void set_socket_non_blocking(int fd) {
  int flags = fcntl(fd, F_GETFD, 0);
  if (flags == -1)
    exit(errno);

  flags |= O_NONBLOCK;

  if (fcntl(fd, F_SETFD, flags) == -1)
    exit(errno);
}

static int wayland_display_connect() {
  char *xdg_runtime_dir = getenv("XDG_RUNTIME_DIR");
  if (xdg_runtime_dir == NULL)
    return EINVAL;

  uint64_t xdg_runtime_dir_len = strlen(xdg_runtime_dir);

  char socket_path[4096] = "";
  uint64_t socket_path_len = 0;
  memcpy(socket_path, xdg_runtime_dir, xdg_runtime_dir_len);
  socket_path_len += xdg_runtime_dir_len;

  char *wayland_display = getenv("WAYLAND_DISPLAY");
  if (wayland_display == NULL) {
    char wayland_display_default[] = "wayland-0";
    uint64_t wayland_display_default_len = cstring_len(wayland_display_default);

    memcpy(socket_path + socket_path_len, wayland_display_default,
           wayland_display_default_len);
    socket_path_len += wayland_display_default_len;
  } else {
    uint64_t wayland_display_len = strlen(wayland_display);
    memcpy(socket_path + socket_path_len, wayland_display, wayland_display_len);
    socket_path_len += wayland_display_len;
  }

  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd == -1)
    exit(errno);

  set_socket_non_blocking(fd);

  struct sockaddr_un addr = {.sun_family = AF_UNIX,
                             .sun_path = "/run/user/1000/wayland-1"};
  if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1)
    exit(errno);

  return fd;
}

static void buf_write_u32(char *buf, uint64_t *buf_size, uint64_t buf_cap,
                          uint32_t x) {
  assert(*buf_size + sizeof(x) <= buf_cap);

  *(uint32_t *)(buf + *buf_size) = x;
  *buf_size += sizeof(x);
}

static void buf_write_u16(char *buf, uint64_t *buf_size, uint64_t buf_cap,
                          uint16_t x) {
  assert(*buf_size + sizeof(x) <= buf_cap);

  *(uint16_t *)(buf + *buf_size) = x;
  *buf_size += sizeof(x);
}

static uint32_t buf_read_u32(char **buf, uint64_t *buf_size) {
  assert(*buf_size >= sizeof(uint32_t));

  uint32_t res = *(uint32_t *)(*buf);
  *buf += sizeof(res);
  *buf_size -= sizeof(res);

  return res;
}

static uint16_t buf_read_u16(char **buf, uint64_t *buf_size) {
  assert(*buf_size >= sizeof(uint16_t));

  uint16_t res = *(uint16_t *)(*buf);
  *buf += sizeof(res);
  *buf_size -= sizeof(res);

  return res;
}

static void buf_read_n(char **buf, uint64_t *buf_size, char *dst, uint64_t n) {
  assert(*buf_size >= n);

  memcpy(dst, *buf, n);

  *buf += n;
  *buf_size -= n;
}

static void wayland_send_get_registry(int fd) {
  uint64_t msg_size = 0;
  char msg[128] = "";
  buf_write_u32(msg, &msg_size, sizeof(msg), wayland_display_object_id);

  uint16_t display_get_registry_opcode = 1;
  buf_write_u16(msg, &msg_size, sizeof(msg), display_get_registry_opcode);

  uint16_t msg_announced_size = 12;
  buf_write_u16(msg, &msg_size, sizeof(msg), msg_announced_size);

  wayland_current_id++;
  buf_write_u32(msg, &msg_size, sizeof(msg), wayland_current_id);

  if ((int64_t)msg_size != write(fd, msg, msg_size))
    exit(errno);
}

static void wayland_handle_message(char **msg, uint64_t *msg_len) {
  assert(*msg_len >= 8);

  uint32_t object_id = buf_read_u32(msg, msg_len);
  assert(object_id <= wayland_current_id);

  uint16_t opcode = buf_read_u16(msg, msg_len);

  uint16_t announced_size = buf_read_u16(msg, msg_len);
  assert(roundup_32(announced_size) <= announced_size);

  uint32_t header_size = sizeof(object_id)+sizeof(opcode)+sizeof(announced_size);
  assert(announced_size <= header_size+ *msg_len);
  printf("[D001] msg_len=%lu announced_size=%u\n", *msg_len, announced_size);

  if (object_id == 2 && opcode == wayland_registry_event_global) {
    uint32_t name = buf_read_u32(msg, msg_len);

    uint32_t interface_len = buf_read_u32(msg, msg_len);
    uint32_t padded_interface_len = roundup_32(interface_len);

    char interface[512] = "";
    assert(padded_interface_len <= cstring_len(interface));

    buf_read_n(msg, msg_len, interface, padded_interface_len);
    assert(interface[interface_len] == 0);

    uint32_t version = buf_read_u32(msg, msg_len);

    printf("wl_registry: name=%u interface=%.*s version=%u\n", name,
           interface_len, interface, version);

    assert(announced_size == sizeof(object_id) + sizeof(announced_size) +
                                 sizeof(opcode) + sizeof(name) +
                                 sizeof(interface_len) + padded_interface_len +
                                 sizeof(version));

    return;
  }
  assert(0 && "todo");
}

int main() {
  int fd = wayland_display_connect();

  wayland_send_get_registry(fd);

  while (1) {
    struct pollfd poll_fd = {.fd = fd, .events = POLLIN};
    int res = poll(&poll_fd, 1, -1);
    if (res == -1)
      exit(errno);

    assert(res == 1);
    assert(poll_fd.revents & POLLIN);

    char read_buf[4096] = "";
    int64_t read_bytes = recv(fd, read_buf, sizeof(read_buf), MSG_DONTWAIT);
    if (read_bytes == -1)
      exit(errno);

    char *msg = read_buf;
    uint64_t msg_len = (uint64_t)read_bytes;

    while (msg_len > 0)
      wayland_handle_message(&msg, &msg_len);
  }
}
