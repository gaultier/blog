#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define cstring_len(s) (sizeof(s) - 1)

uint32_t wayland_current_id = 1;
uint32_t wayland_display_object_id = 1;

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

    char msg[4096] = "";
    int64_t msg_len = read(fd, msg, sizeof(msg));
    if (msg_len == -1)
      exit(errno);
  }
}
