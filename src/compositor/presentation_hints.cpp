// wp_fifo_v1 and wp_commit_timing_v1: two small "how should my frames be paced" protocols that KWin and Mutter offer and wlroots (0.20) does not yet.
// NVIDIA's Wayland EGL/Vulkan stack looks for them. They are accepted and answered but not enforced: a barrier or a timestamp does not delay a commit,
// the compositor presents the newest buffer at the next refresh as it always did. That is enough for a client to proceed.
#include "presentation_hints.hpp"

#include <wayland-server-core.h>

extern "C" {
#include "commit-timing-v1-protocol.h"
#include "fifo-v1-protocol.h"
}

namespace fleetwm {

namespace {

void resource_destroy(wl_client*, wl_resource* resource) { wl_resource_destroy(resource); }
void noop(wl_client*, wl_resource*) {}

const struct wp_fifo_v1_interface fifo_impl = {
    .set_barrier = noop,
    .wait_barrier = noop,
    .destroy = resource_destroy,
};

void get_fifo(wl_client* client, wl_resource* manager, uint32_t id, wl_resource*) {
  wl_resource* r = wl_resource_create(client, &wp_fifo_v1_interface, wl_resource_get_version(manager), id);
  if (!r) {
    wl_client_post_no_memory(client);
    return;
  }
  wl_resource_set_implementation(r, &fifo_impl, nullptr, nullptr);
}

const struct wp_fifo_manager_v1_interface fifo_manager_impl = {
    .destroy = resource_destroy,
    .get_fifo = get_fifo,
};

void fifo_bind(wl_client* client, void*, uint32_t version, uint32_t id) {
  wl_resource* r = wl_resource_create(client, &wp_fifo_manager_v1_interface, static_cast<int>(version), id);
  if (!r) {
    wl_client_post_no_memory(client);
    return;
  }
  wl_resource_set_implementation(r, &fifo_manager_impl, nullptr, nullptr);
}

void set_timestamp(wl_client*, wl_resource*, uint32_t, uint32_t, uint32_t) {}

const struct wp_commit_timer_v1_interface timer_impl = {
    .set_timestamp = set_timestamp,
    .destroy = resource_destroy,
};

void get_timer(wl_client* client, wl_resource* manager, uint32_t id, wl_resource*) {
  wl_resource* r = wl_resource_create(client, &wp_commit_timer_v1_interface, wl_resource_get_version(manager), id);
  if (!r) {
    wl_client_post_no_memory(client);
    return;
  }
  wl_resource_set_implementation(r, &timer_impl, nullptr, nullptr);
}

const struct wp_commit_timing_manager_v1_interface timing_manager_impl = {
    .destroy = resource_destroy,
    .get_timer = get_timer,
};

void timing_bind(wl_client* client, void*, uint32_t version, uint32_t id) {
  wl_resource* r = wl_resource_create(client, &wp_commit_timing_manager_v1_interface, static_cast<int>(version), id);
  if (!r) {
    wl_client_post_no_memory(client);
    return;
  }
  wl_resource_set_implementation(r, &timing_manager_impl, nullptr, nullptr);
}

}  // namespace

void presentation_hints_init(wl_display* display) {
  wl_global_create(display, &wp_fifo_manager_v1_interface, 1, nullptr, fifo_bind);
  wl_global_create(display, &wp_commit_timing_manager_v1_interface, 1, nullptr, timing_bind);
}

}  // namespace fleetwm
