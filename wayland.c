#define _POSIX_C_SOURCE 200112L
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#define cstring_len(s) (sizeof(s) - 1)

#define roundup_4(n) (((n) + 3) & -4)

static uint32_t wayland_current_id = 1;

static const uint32_t wayland_display_object_id = 1;
static const uint16_t wayland_wl_registry_event_global = 0;
static const uint16_t wayland_shm_pool_event_format = 0;
static const uint16_t wayland_wl_buffer_event_release = 0;
static const uint16_t wayland_xdg_wm_base_event_ping = 0;
static const uint16_t wayland_xdg_toplevel_event_configure = 0;
static const uint16_t wayland_xdg_toplevel_event_close = 1;
static const uint16_t wayland_xdg_surface_event_configure = 0;
static const uint16_t wayland_wl_seat_event_capabilities = 0;
static const uint16_t wayland_wl_seat_event_capabilities_pointer = 1;
static const uint16_t wayland_wl_seat_event_capabilities_keyboard = 2;
static const uint16_t wayland_wl_seat_event_name = 1;
static const uint16_t wayland_wl_pointer_event_enter = 0;
static const uint16_t wayland_wl_pointer_event_leave = 1;
static const uint16_t wayland_wl_pointer_event_motion = 2;
static const uint16_t wayland_wl_pointer_event_button = 3;
static const uint16_t wayland_wl_pointer_event_frame = 5;
static const uint16_t wayland_wl_seat_get_pointer_opcode = 0;
static const uint16_t wayland_wl_display_get_registry_opcode = 1;
static const uint16_t wayland_wl_registry_bind_opcode = 0;
static const uint16_t wayland_wl_compositor_create_surface_opcode = 0;
static const uint16_t wayland_xdg_wm_base_pong_opcode = 3;
static const uint16_t wayland_xdg_surface_ack_configure_opcode = 4;
static const uint16_t wayland_wl_shm_create_pool_opcode = 0;
static const uint16_t wayland_xdg_wm_base_get_xdg_surface_opcode = 2;
static const uint16_t wayland_wl_shm_pool_create_buffer_opcode = 0;
static const uint16_t wayland_wl_buffer_destroy_opcode = 0;
static const uint16_t wayland_wl_surface_attach_opcode = 1;
static const uint16_t wayland_xdg_surface_get_toplevel_opcode = 1;
static const uint16_t wayland_wl_surface_commit_opcode = 6;
static const uint16_t wayland_wl_display_error_event = 0;
static const uint16_t wayland_wl_display_delete_id_event = 1;
static const uint32_t wayland_format_xrgb8888 = 1;
static const uint32_t wayland_header_size = 8;
static const uint32_t color_channels = 4;

typedef enum state_state_t state_state_t;
enum state_state_t {
  STATE_NONE,
  STATE_SURFACE_ACKED_CONFIGURE,
  STATE_SURFACE_ATTACHED,
};

typedef struct state_t state_t;
struct state_t {
  uint32_t wl_registry;
  uint32_t wl_shm;
  uint32_t wl_shm_pool;
  uint32_t old_wl_buffers[2];
  uint32_t xdg_wm_base;
  uint32_t xdg_surface;
  uint32_t wl_compositor;
  uint32_t wl_seat;
  uint32_t wl_pointer;
  uint32_t wl_surface;
  uint32_t xdg_toplevel;
  uint32_t stride;
  uint32_t w;
  uint32_t h;
  uint32_t shm_pool_size;
  int shm_fd;
  uint8_t *shm_pool_data;
  uint8_t current_buffer_idx;

  state_state_t state;
};

static int wayland_display_connect() {
  char *xdg_runtime_dir = getenv("XDG_RUNTIME_DIR");
  if (xdg_runtime_dir == NULL)
    return EINVAL;

  uint64_t xdg_runtime_dir_len = strlen(xdg_runtime_dir);

  struct sockaddr_un addr = {.sun_family = AF_UNIX};
  assert(xdg_runtime_dir_len <= cstring_len(addr.sun_path));
  uint64_t socket_path_len = 0;

  memcpy(addr.sun_path, xdg_runtime_dir, xdg_runtime_dir_len);
  socket_path_len += xdg_runtime_dir_len;

  addr.sun_path[socket_path_len++] = '/';

  char *wayland_display = getenv("WAYLAND_DISPLAY");
  if (wayland_display == NULL) {
    char wayland_display_default[] = "wayland-0";
    uint64_t wayland_display_default_len = cstring_len(wayland_display_default);

    memcpy(addr.sun_path + socket_path_len, wayland_display_default,
           wayland_display_default_len);
    socket_path_len += wayland_display_default_len;
  } else {
    uint64_t wayland_display_len = strlen(wayland_display);
    memcpy(addr.sun_path + socket_path_len, wayland_display,
           wayland_display_len);
    socket_path_len += wayland_display_len;
  }

  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd == -1)
    exit(errno);

  if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1)
    exit(errno);

  return fd;
}

static void buf_write_u32(char *buf, uint64_t *buf_size, uint64_t buf_cap,
                          uint32_t x) {
  assert(*buf_size + sizeof(x) <= buf_cap);
  assert(((size_t)buf + *buf_size) % sizeof(x) == 0);

  *(uint32_t *)(buf + *buf_size) = x;
  *buf_size += sizeof(x);
}

static void buf_write_u16(char *buf, uint64_t *buf_size, uint64_t buf_cap,
                          uint16_t x) {
  assert(*buf_size + sizeof(x) <= buf_cap);
  assert(((size_t)buf + *buf_size) % sizeof(x) == 0);

  *(uint16_t *)(buf + *buf_size) = x;
  *buf_size += sizeof(x);
}

static void buf_write_string(char *buf, uint64_t *buf_size, uint64_t buf_cap,
                             char *src, uint32_t src_len) {
  assert(*buf_size + src_len <= buf_cap);

  buf_write_u32(buf, buf_size, buf_cap, src_len);
  memcpy(buf + *buf_size, src, roundup_4(src_len));
  *buf_size += roundup_4(src_len);
}

static uint32_t buf_read_u32(char **buf, uint64_t *buf_size) {
  assert(*buf_size >= sizeof(uint32_t));
  assert((size_t)*buf % sizeof(uint32_t) == 0);

  uint32_t res = *(uint32_t *)(*buf);
  *buf += sizeof(res);
  *buf_size -= sizeof(res);

  return res;
}

static uint16_t buf_read_u16(char **buf, uint64_t *buf_size) {
  assert(*buf_size >= sizeof(uint16_t));
  assert((size_t)*buf % sizeof(uint16_t) == 0);

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

static uint32_t wayland_wl_display_get_registry(int fd) {
  uint64_t msg_size = 0;
  char msg[128] = "";
  buf_write_u32(msg, &msg_size, sizeof(msg), wayland_display_object_id);

  buf_write_u16(msg, &msg_size, sizeof(msg),
                wayland_wl_display_get_registry_opcode);

  uint16_t msg_announced_size =
      wayland_header_size + sizeof(wayland_current_id);
  assert(roundup_4(msg_announced_size) == msg_announced_size);
  buf_write_u16(msg, &msg_size, sizeof(msg), msg_announced_size);

  wayland_current_id++;
  buf_write_u32(msg, &msg_size, sizeof(msg), wayland_current_id);

  if ((int64_t)msg_size != send(fd, msg, msg_size, 0))
    exit(errno);

  fprintf(stderr, "-> wl_display@%u.get_registry: wl_registry=%u\n",
          wayland_display_object_id, wayland_current_id);

  return wayland_current_id;
}

static uint32_t wayland_wl_registry_bind(int fd, uint32_t registry,
                                         uint32_t name, char *interface,
                                         uint32_t interface_len,
                                         uint32_t version) {
  uint64_t msg_size = 0;
  char msg[512] = "";
  buf_write_u32(msg, &msg_size, sizeof(msg), registry);

  buf_write_u16(msg, &msg_size, sizeof(msg), wayland_wl_registry_bind_opcode);

  uint16_t msg_announced_size =
      wayland_header_size + sizeof(name) + sizeof(interface_len) +
      roundup_4(interface_len) + sizeof(version) + sizeof(wayland_current_id);
  assert(roundup_4(msg_announced_size) == msg_announced_size);
  buf_write_u16(msg, &msg_size, sizeof(msg), msg_announced_size);

  buf_write_u32(msg, &msg_size, sizeof(msg), name);
  buf_write_string(msg, &msg_size, sizeof(msg), interface, interface_len);
  buf_write_u32(msg, &msg_size, sizeof(msg), version);

  wayland_current_id++;
  buf_write_u32(msg, &msg_size, sizeof(msg), wayland_current_id);

  assert(msg_size == roundup_4(msg_size));

  if ((int64_t)msg_size != send(fd, msg, msg_size, 0))
    exit(errno);

  fprintf(stderr,
          "-> wl_registry@%u.bind: name=%u interface=%.*s version=%u "
          "wayland_current_id=%u\n",
          registry, name, interface_len, interface, version,
          wayland_current_id);

  return wayland_current_id;
}

static uint32_t wayland_wl_compositor_create_surface(int fd, state_t *state) {
  assert(state->wl_compositor > 0);

  uint64_t msg_size = 0;
  char msg[128] = "";
  buf_write_u32(msg, &msg_size, sizeof(msg), state->wl_compositor);

  buf_write_u16(msg, &msg_size, sizeof(msg),
                wayland_wl_compositor_create_surface_opcode);

  uint16_t msg_announced_size =
      wayland_header_size + sizeof(wayland_current_id);
  assert(roundup_4(msg_announced_size) == msg_announced_size);
  buf_write_u16(msg, &msg_size, sizeof(msg), msg_announced_size);

  wayland_current_id++;
  buf_write_u32(msg, &msg_size, sizeof(msg), wayland_current_id);

  if ((int64_t)msg_size != send(fd, msg, msg_size, 0))
    exit(errno);

  fprintf(stderr, "-> wl_compositor@%u.create_surface: wl_surface=%u\n",
          state->wl_compositor, wayland_current_id);

  return wayland_current_id;
}

static void create_shared_memory_file(uint64_t size, state_t *state) {
  char name[255] = "/";
  for (uint64_t i = 1; i < cstring_len(name); i++) {
    name[i] = ((double)rand()) / (double)RAND_MAX * 26 + 'a';
  }

  int fd = shm_open(name, O_RDWR | O_EXCL | O_CREAT, 0600);
  if (fd == -1)
    exit(errno);

  assert(shm_unlink(name) != -1);

  if (ftruncate(fd, size) == -1)
    exit(errno);

  state->shm_pool_data =
      mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  assert(state->shm_pool_data != NULL);
  state->shm_fd = fd;
}

static void wayland_xdg_wm_base_pong(int fd, state_t *state, uint32_t ping) {
  assert(state->xdg_wm_base > 0);
  assert(state->wl_surface > 0);

  uint64_t msg_size = 0;
  char msg[128] = "";
  buf_write_u32(msg, &msg_size, sizeof(msg), state->xdg_wm_base);

  buf_write_u16(msg, &msg_size, sizeof(msg), wayland_xdg_wm_base_pong_opcode);

  uint16_t msg_announced_size = wayland_header_size + sizeof(ping);
  assert(roundup_4(msg_announced_size) == msg_announced_size);
  buf_write_u16(msg, &msg_size, sizeof(msg), msg_announced_size);

  buf_write_u32(msg, &msg_size, sizeof(msg), ping);

  if ((int64_t)msg_size != send(fd, msg, msg_size, 0))
    exit(errno);

  fprintf(stderr, "-> xdg_wm_base@%u.pong: ping=%u\n", state->xdg_wm_base,
          ping);
}

static void wayland_xdg_surface_ack_configure(int fd, state_t *state,
                                              uint32_t configure) {
  assert(state->xdg_surface > 0);

  uint64_t msg_size = 0;
  char msg[128] = "";
  buf_write_u32(msg, &msg_size, sizeof(msg), state->xdg_surface);

  buf_write_u16(msg, &msg_size, sizeof(msg),
                wayland_xdg_surface_ack_configure_opcode);

  uint16_t msg_announced_size = wayland_header_size + sizeof(configure);
  assert(roundup_4(msg_announced_size) == msg_announced_size);
  buf_write_u16(msg, &msg_size, sizeof(msg), msg_announced_size);

  buf_write_u32(msg, &msg_size, sizeof(msg), configure);

  if ((int64_t)msg_size != send(fd, msg, msg_size, 0))
    exit(errno);

  fprintf(stderr, "-> xdg_surface@%u.ack_configure: configure=%u\n",
          state->xdg_surface, configure);
}

static uint32_t wayland_wl_shm_create_pool(int fd, state_t *state) {
  assert(state->shm_pool_size > 0);

  uint64_t msg_size = 0;
  char msg[128] = "";
  buf_write_u32(msg, &msg_size, sizeof(msg), state->wl_shm);

  buf_write_u16(msg, &msg_size, sizeof(msg), wayland_wl_shm_create_pool_opcode);

  uint16_t msg_announced_size = wayland_header_size +
                                sizeof(wayland_current_id) +
                                sizeof(state->shm_pool_size);

  assert(roundup_4(msg_announced_size) == msg_announced_size);
  buf_write_u16(msg, &msg_size, sizeof(msg), msg_announced_size);

  wayland_current_id++;
  buf_write_u32(msg, &msg_size, sizeof(msg), wayland_current_id);

  buf_write_u32(msg, &msg_size, sizeof(msg), state->shm_pool_size);

  assert(roundup_4(msg_size) == msg_size);

  // Send the file descriptor as ancillary data.
  // UNIX/Macros monstrosities ahead.
  char buf[CMSG_SPACE(sizeof(state->shm_fd))] = "";

  struct iovec io = {.iov_base = msg, .iov_len = msg_size};
  struct msghdr socket_msg = {
      .msg_iov = &io,
      .msg_iovlen = 1,
      .msg_control = buf,
      .msg_controllen = sizeof(buf),
  };

  struct cmsghdr *cmsg = CMSG_FIRSTHDR(&socket_msg);
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = SCM_RIGHTS;
  cmsg->cmsg_len = CMSG_LEN(sizeof(state->shm_fd));

  *((int *)CMSG_DATA(cmsg)) = state->shm_fd;
  socket_msg.msg_controllen = CMSG_SPACE(sizeof(state->shm_fd));

  if (sendmsg(fd, &socket_msg, 0) == -1)
    exit(errno);

  fprintf(stderr, "-> wl_shm@%u.create_pool: wl_shm_pool=%u\n", state->wl_shm,
          wayland_current_id);

  return wayland_current_id;
}

static uint32_t wayland_xdg_wm_base_get_xdg_surface(int fd, state_t *state) {
  assert(state->xdg_wm_base > 0);
  assert(state->wl_surface > 0);

  uint64_t msg_size = 0;
  char msg[128] = "";
  buf_write_u32(msg, &msg_size, sizeof(msg), state->xdg_wm_base);

  buf_write_u16(msg, &msg_size, sizeof(msg),
                wayland_xdg_wm_base_get_xdg_surface_opcode);

  uint16_t msg_announced_size = wayland_header_size +
                                sizeof(wayland_current_id) +
                                sizeof(state->wl_surface);
  assert(roundup_4(msg_announced_size) == msg_announced_size);
  buf_write_u16(msg, &msg_size, sizeof(msg), msg_announced_size);

  wayland_current_id++;
  buf_write_u32(msg, &msg_size, sizeof(msg), wayland_current_id);

  buf_write_u32(msg, &msg_size, sizeof(msg), state->wl_surface);

  if ((int64_t)msg_size != send(fd, msg, msg_size, 0))
    exit(errno);

  fprintf(stderr,
          "-> xdg_wm_base@%u.get_xdg_surface: xdg_surface=%u wl_surface=%u\n",
          state->xdg_wm_base, wayland_current_id, state->wl_surface);

  return wayland_current_id;
}

static uint32_t wayland_wl_shm_pool_create_buffer(int fd, state_t *state,
                                                  uint32_t offset) {
  assert(state->wl_shm_pool > 0);

  uint64_t msg_size = 0;
  char msg[128] = "";
  buf_write_u32(msg, &msg_size, sizeof(msg), state->wl_shm_pool);

  buf_write_u16(msg, &msg_size, sizeof(msg),
                wayland_wl_shm_pool_create_buffer_opcode);

  uint16_t msg_announced_size =
      wayland_header_size + sizeof(wayland_current_id) + sizeof(uint32_t) * 5;
  assert(roundup_4(msg_announced_size) == msg_announced_size);
  buf_write_u16(msg, &msg_size, sizeof(msg), msg_announced_size);

  wayland_current_id++;
  buf_write_u32(msg, &msg_size, sizeof(msg), wayland_current_id);

  buf_write_u32(msg, &msg_size, sizeof(msg), offset);

  buf_write_u32(msg, &msg_size, sizeof(msg), state->w);

  buf_write_u32(msg, &msg_size, sizeof(msg), state->h);

  buf_write_u32(msg, &msg_size, sizeof(msg), state->stride);

  uint32_t format = wayland_format_xrgb8888;
  buf_write_u32(msg, &msg_size, sizeof(msg), format);

  if ((int64_t)msg_size != send(fd, msg, msg_size, 0))
    exit(errno);

  fprintf(stderr, "-> wl_shm_pool@%u.create_buffer: wl_buffer=%u\n",
          state->wl_shm_pool, wayland_current_id);

  return wayland_current_id;
}

static void wayland_wl_buffer_destroy(int fd, uint32_t wl_buffer) {
  assert(wl_buffer > 0);

  uint64_t msg_size = 0;
  char msg[128] = "";
  buf_write_u32(msg, &msg_size, sizeof(msg), wl_buffer);

  buf_write_u16(msg, &msg_size, sizeof(msg), wayland_wl_buffer_destroy_opcode);

  uint16_t msg_announced_size = wayland_header_size;
  assert(roundup_4(msg_announced_size) == msg_announced_size);
  buf_write_u16(msg, &msg_size, sizeof(msg), msg_announced_size);

  if ((int64_t)msg_size != send(fd, msg, msg_size, 0))
    exit(errno);

  fprintf(stderr, "-> wl_buffer@%u.destroy\n", wl_buffer);
}

static void wayland_wl_surface_attach(int fd, uint32_t wl_surface,
                                      uint32_t wl_buffer, uint32_t x,
                                      uint32_t y) {

  uint64_t msg_size = 0;
  char msg[128] = "";
  buf_write_u32(msg, &msg_size, sizeof(msg), wl_surface);

  buf_write_u16(msg, &msg_size, sizeof(msg), wayland_wl_surface_attach_opcode);

  uint16_t msg_announced_size =
      wayland_header_size + sizeof(wl_buffer) + sizeof(uint32_t) * 2;
  assert(roundup_4(msg_announced_size) == msg_announced_size);
  buf_write_u16(msg, &msg_size, sizeof(msg), msg_announced_size);

  buf_write_u32(msg, &msg_size, sizeof(msg), wl_buffer);

  buf_write_u32(msg, &msg_size, sizeof(msg), x);
  buf_write_u32(msg, &msg_size, sizeof(msg), y);

  if ((int64_t)msg_size != send(fd, msg, msg_size, 0))
    exit(errno);

  fprintf(stderr, "-> wl_surface@%u.attach: wl_buffer=%u\n", wl_surface,
          wl_buffer);
}

static uint32_t wayland_xdg_surface_get_toplevel(int fd, state_t *state) {
  assert(state->xdg_surface > 0);

  uint64_t msg_size = 0;
  char msg[128] = "";
  buf_write_u32(msg, &msg_size, sizeof(msg), state->xdg_surface);

  buf_write_u16(msg, &msg_size, sizeof(msg),
                wayland_xdg_surface_get_toplevel_opcode);

  uint16_t msg_announced_size =
      wayland_header_size + sizeof(wayland_current_id);
  assert(roundup_4(msg_announced_size) == msg_announced_size);
  buf_write_u16(msg, &msg_size, sizeof(msg), msg_announced_size);

  wayland_current_id++;
  buf_write_u32(msg, &msg_size, sizeof(msg), wayland_current_id);

  if ((int64_t)msg_size != send(fd, msg, msg_size, 0))
    exit(errno);

  fprintf(stderr, "-> xdg_surface@%u.get_toplevel: xdg_toplevel=%u\n",
          state->xdg_surface, wayland_current_id);

  return wayland_current_id;
}

static void wayland_wl_surface_commit(int fd, state_t *state) {
  assert(state->wl_surface > 0);

  uint64_t msg_size = 0;
  char msg[128] = "";
  buf_write_u32(msg, &msg_size, sizeof(msg), state->wl_surface);

  buf_write_u16(msg, &msg_size, sizeof(msg), wayland_wl_surface_commit_opcode);

  uint16_t msg_announced_size = wayland_header_size;
  assert(roundup_4(msg_announced_size) == msg_announced_size);
  buf_write_u16(msg, &msg_size, sizeof(msg), msg_announced_size);

  if ((int64_t)msg_size != send(fd, msg, msg_size, 0))
    exit(errno);

  fprintf(stderr, "-> wl_surface@%u.commit: \n", state->wl_surface);
}

static uint32_t wayland_wl_seat_get_pointer(int fd, uint32_t wl_seat) {
  assert(wl_seat > 0);

  uint64_t msg_size = 0;
  char msg[128] = "";
  buf_write_u32(msg, &msg_size, sizeof(msg), wl_seat);

  buf_write_u16(msg, &msg_size, sizeof(msg),
                wayland_wl_seat_get_pointer_opcode);

  uint16_t msg_announced_size =
      wayland_header_size + sizeof(wayland_current_id);
  assert(roundup_4(msg_announced_size) == msg_announced_size);
  buf_write_u16(msg, &msg_size, sizeof(msg), msg_announced_size);

  wayland_current_id++;
  buf_write_u32(msg, &msg_size, sizeof(msg), wayland_current_id);

  if ((int64_t)msg_size != send(fd, msg, msg_size, 0))
    exit(errno);

  fprintf(stderr, "-> wl_seat@%u.get_pointer: %u\n", wl_seat,
          wayland_current_id);

  return wayland_current_id;
}

static void wayland_handle_message(int fd, state_t *state, char **msg,
                                   uint64_t *msg_len) {
  assert(*msg_len >= 8);
  uint64_t start_msg_len = *msg_len;

  uint32_t object_id = buf_read_u32(msg, msg_len);
  assert(object_id <= wayland_current_id);

  uint16_t opcode = buf_read_u16(msg, msg_len);

  uint16_t announced_size = buf_read_u16(msg, msg_len);
  assert(roundup_4(announced_size) <= announced_size);

  uint32_t header_size =
      sizeof(object_id) + sizeof(opcode) + sizeof(announced_size);
  assert(announced_size <= header_size + *msg_len);

  if (object_id == state->wl_registry &&
      opcode == wayland_wl_registry_event_global) {
    uint32_t name = buf_read_u32(msg, msg_len);

    uint32_t interface_len = buf_read_u32(msg, msg_len);
    uint32_t padded_interface_len = roundup_4(interface_len);

    char interface[512] = "";
    assert(padded_interface_len <= cstring_len(interface));

    buf_read_n(msg, msg_len, interface, padded_interface_len);
    assert(interface[interface_len] == 0);

    uint32_t version = buf_read_u32(msg, msg_len);

    fprintf(stderr,
            "<- wl_registry@%u.global: name=%u interface=%.*s version=%u\n",
            state->wl_registry, name, interface_len, interface, version);

    assert(announced_size == sizeof(object_id) + sizeof(announced_size) +
                                 sizeof(opcode) + sizeof(name) +
                                 sizeof(interface_len) + padded_interface_len +
                                 sizeof(version));

    char wl_shm_interface[] = "wl_shm";
    if (strcmp(wl_shm_interface, interface) == 0) {
      assert(state->wl_shm == 0);

      state->wl_shm = wayland_wl_registry_bind(
          fd, state->wl_registry, name, interface, interface_len, version);
    }

    char xdg_wm_base_interface[] = "xdg_wm_base";
    if (strcmp(xdg_wm_base_interface, interface) == 0) {
      assert(state->xdg_wm_base == 0);

      state->xdg_wm_base = wayland_wl_registry_bind(
          fd, state->wl_registry, name, interface, interface_len, version);
    }

    char wl_compositor_interface[] = "wl_compositor";
    if (strcmp(wl_compositor_interface, interface) == 0) {
      assert(state->wl_compositor == 0);

      state->wl_compositor = wayland_wl_registry_bind(
          fd, state->wl_registry, name, interface, interface_len, version);
    }

    char wl_seat_interface[] = "wl_seat";
    if (strcmp(wl_seat_interface, interface) == 0) {
      assert(state->wl_seat == 0);

      state->wl_seat = wayland_wl_registry_bind(
          fd, state->wl_registry, name, interface, interface_len, version);
    }

  } else if (object_id == wayland_display_object_id &&
             opcode == wayland_wl_display_error_event) {
    uint32_t target_object_id = buf_read_u32(msg, msg_len);
    uint32_t code = buf_read_u32(msg, msg_len);
    char error[512] = "";
    uint32_t error_len = buf_read_u32(msg, msg_len);
    buf_read_n(msg, msg_len, error, roundup_4(error_len));

    fprintf(stderr, "fatal error: target_object_id=%u code=%u error=%s\n",
            target_object_id, code, error);
    exit(EINVAL);
  } else if (object_id == wayland_display_object_id &&
             opcode == wayland_wl_display_delete_id_event) {
    uint32_t id = buf_read_u32(msg, msg_len);
    fprintf(stderr, "<- wl_display@1:delete_id: id=%u\n", id);

  } else if (object_id == state->wl_shm &&
             opcode == wayland_shm_pool_event_format) {

    uint32_t format = buf_read_u32(msg, msg_len);
    fprintf(stderr, "<- wl_shm@%u: format=%#x\n", state->wl_shm, format);
  } else if ((object_id == state->old_wl_buffers[0] ||
              object_id == state->old_wl_buffers[1]) &&
             opcode == wayland_wl_buffer_event_release) {

    fprintf(stderr, "<- xdg_wl_buffer@%u.release\n", object_id);
  } else if (object_id == state->xdg_wm_base &&
             opcode == wayland_xdg_wm_base_event_ping) {
    uint32_t ping = buf_read_u32(msg, msg_len);
    fprintf(stderr, "<- xdg_wm_base@%u.ping: ping=%u\n", state->xdg_wm_base,
            ping);
    wayland_xdg_wm_base_pong(fd, state, ping);

  } else if (object_id == state->xdg_toplevel &&
             opcode == wayland_xdg_toplevel_event_configure) {
    uint32_t w = buf_read_u32(msg, msg_len);
    uint32_t h = buf_read_u32(msg, msg_len);
    uint32_t len = buf_read_u32(msg, msg_len);
    char buf[256] = "";
    assert(len <= sizeof(buf));
    buf_read_n(msg, msg_len, buf, len);

    fprintf(stderr, "<- xdg_toplevel@%u.configure: w=%u h=%u states[%u]\n",
            state->xdg_toplevel, w, h, len);

    if (w && h && (w != state->w || h != state->h)) { // Resize.
      state->w = w;
      state->h = h;
      state->stride = w * color_channels;

      assert(state->h * state->stride <= state->shm_pool_size);
    }
  } else if (object_id == state->xdg_surface &&
             opcode == wayland_xdg_surface_event_configure) {
    uint32_t configure = buf_read_u32(msg, msg_len);
    fprintf(stderr, "<- xdg_surface@%u.configure: configure=%u\n",
            state->xdg_surface, configure);
    wayland_xdg_surface_ack_configure(fd, state, configure);
    state->state = STATE_SURFACE_ACKED_CONFIGURE;

  } else if (object_id == state->xdg_toplevel &&
             opcode == wayland_xdg_toplevel_event_close) {
    fprintf(stderr, "<- xdg_toplevel@%u.close\n", state->xdg_toplevel);
    exit(0);
  } else if (object_id == state->wl_seat &&
             opcode == wayland_wl_seat_event_name) {
    uint32_t buf_len = buf_read_u32(msg, msg_len);
    char buf[256] = "";
    assert(buf_len <= sizeof(buf));
    buf_read_n(msg, msg_len, buf, roundup_4(buf_len));

    fprintf(stderr, "<- wl_seat@%u.name: name=%.*s\n", state->wl_seat, buf_len,
            buf);
  } else if (object_id == state->wl_seat &&
             opcode == wayland_wl_seat_event_capabilities) {
    uint32_t capabilities = buf_read_u32(msg, msg_len);
    fprintf(stderr, "<- wl_seat@%u.capabilities: capabilities=%u\n",
            state->wl_seat, capabilities);
    assert(capabilities == (wayland_wl_seat_event_capabilities_pointer |
                            wayland_wl_seat_event_capabilities_keyboard));

    state->wl_pointer = wayland_wl_seat_get_pointer(fd, state->wl_seat);
  } else if (object_id == state->wl_pointer &&
             opcode == wayland_wl_pointer_event_enter) {
    uint32_t serial = buf_read_u32(msg, msg_len);
    uint32_t surface = buf_read_u32(msg, msg_len);
    uint32_t x = buf_read_u32(msg, msg_len);
    uint32_t y = buf_read_u32(msg, msg_len);
    uint32_t id = 0;

    uint64_t parsed_bytes = start_msg_len - *msg_len;
    uint64_t remaining = announced_size - parsed_bytes;
    if (remaining > 0)
      id = buf_read_u32(msg, msg_len);

    fprintf(stderr,
            "<- wl_pointer@%u.enter: serial=%u surface=%u x=%u y=%u id=%u\n",
            state->wl_seat, serial, surface, x, y, id);
  } else if (object_id == state->wl_pointer &&
             opcode == wayland_wl_pointer_event_leave) {
    uint32_t serial = buf_read_u32(msg, msg_len);
    uint32_t surface = buf_read_u32(msg, msg_len);

    fprintf(stderr, "<- wl_pointer@%u.leave: serial=%u surface=%u\n",
            state->wl_seat, serial, surface);
  } else if (object_id == state->wl_pointer &&
             opcode == wayland_wl_pointer_event_button) {
    uint32_t serial = buf_read_u32(msg, msg_len);
    uint32_t time = buf_read_u32(msg, msg_len);
    uint32_t button = buf_read_u32(msg, msg_len);
    uint32_t button_state = buf_read_u32(msg, msg_len);

    fprintf(stderr,
            "<- wl_pointer@%u.button: serial=%u time=%u button=%u state=%u\n",
            state->wl_seat, serial, time, button, button_state);
  } else if (object_id == state->wl_pointer &&
             opcode == wayland_wl_pointer_event_motion) {
    uint32_t time = buf_read_u32(msg, msg_len);
    uint32_t surface_x = buf_read_u32(msg, msg_len);
    uint32_t surface_y = buf_read_u32(msg, msg_len);

    fprintf(stderr,
            "<- wl_pointer@%u.motion: time=%u surface_x=%u surface_y=%u\n",
            state->wl_seat, time, surface_x, surface_y);

  } else if (object_id == state->wl_pointer &&
             opcode == wayland_wl_pointer_event_frame) {

    fprintf(stderr, "<- wl_pointer@%u.frame\n", state->wl_seat);
  } else {

    fprintf(stderr, "object_id=%u opcode=%u msg_len=%lu\n", object_id, opcode,
            *msg_len);
    assert(0 && "todo");
  }

  uint64_t parsed_bytes = start_msg_len - *msg_len;
  uint64_t remaining = announced_size - parsed_bytes;
  assert(remaining == 0);
}

static void renderer_clear(uint32_t *pixels, uint64_t size,
                           uint32_t color_rgb) {
  for (uint64_t i = 0; i < size; i++)
    pixels[i] = color_rgb;
}

static void renderer_draw_rect(uint32_t *dst, uint64_t window_width,
                               uint64_t dst_x, uint64_t dst_y, uint64_t rect_w,
                               uint64_t rect_h, uint32_t color_rgb) {

  dst += window_width * dst_y + dst_x;

  for (uint64_t src_y = 0; src_y < rect_h; src_y++) {
    for (uint64_t src_x = 0; src_x < rect_w; src_x++) {
      dst[window_width * src_y + src_x] = color_rgb;
    }
  }
}

static uint8_t get_next_buffer_index(state_t *state) {
  return (state->current_buffer_idx + 1) % 2;
}

static uint32_t *get_render_buffer(state_t *state) {
  return (uint32_t *)&state->shm_pool_data[state->current_buffer_idx];
}

int main() {
  struct timeval tv = {0};
  assert(gettimeofday(&tv, NULL) != -1);
  srand(tv.tv_sec * 1000 * 1000 + tv.tv_usec);

  int fd = wayland_display_connect();

  state_t state = {
      .wl_registry = wayland_wl_display_get_registry(fd),
      .w = 800,
      .h = 600,
      .stride = 800 * color_channels,
  };

  state.shm_pool_size = 1 << 26;
  assert(state.h * state.stride <= state.shm_pool_size);
  create_shared_memory_file(state.shm_pool_size, &state);

  while (1) {
    char read_buf[4096] = "";
    int64_t read_bytes = recv(fd, read_buf, sizeof(read_buf), 0);
    if (read_bytes == -1)
      exit(errno);

    char *msg = read_buf;
    uint64_t msg_len = (uint64_t)read_bytes;

    while (msg_len > 0)
      wayland_handle_message(fd, &state, &msg, &msg_len);

    if (state.wl_compositor != 0 && state.wl_shm != 0 &&
        state.xdg_wm_base != 0 &&
        state.wl_surface == 0) { // Bind phase complete, need to create surface.
      assert(state.state == STATE_NONE);

      state.wl_surface = wayland_wl_compositor_create_surface(fd, &state);
      state.xdg_surface = wayland_xdg_wm_base_get_xdg_surface(fd, &state);
      state.xdg_toplevel = wayland_xdg_surface_get_toplevel(fd, &state);
      wayland_wl_surface_commit(fd, &state);
    }

    if (state.state == STATE_SURFACE_ACKED_CONFIGURE) {
      // Render a frame.
      assert(state.wl_surface != 0);
      assert(state.xdg_surface != 0);
      assert(state.xdg_toplevel != 0);

      if (state.wl_shm_pool == 0)
        state.wl_shm_pool = wayland_wl_shm_create_pool(fd, &state);

      assert(state.shm_pool_data != 0);
      assert(state.shm_pool_size != 0);

      state.current_buffer_idx = get_next_buffer_index(&state);
      uint32_t offset = state.stride * state.h * state.current_buffer_idx;
      uint32_t wl_buffer =
          wayland_wl_shm_pool_create_buffer(fd, &state, offset);

      renderer_clear(get_render_buffer(&state),
                     (uint64_t)state.w * (uint64_t)state.h, 0x000000);

renderer_draw_rect(get_render_buffer(&state),state.w, state.w/2,state.h/2,60, 60, 0xfefefe);

      wayland_wl_surface_attach(fd, state.wl_surface, wl_buffer, 0, 0);
      wayland_wl_surface_commit(fd, &state);

      wayland_wl_buffer_destroy(fd, wl_buffer);
      state.old_wl_buffers[state.current_buffer_idx] = wl_buffer;

      state.state = STATE_SURFACE_ATTACHED;
    }
  }
}
