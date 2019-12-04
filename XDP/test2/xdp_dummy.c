// SPDX-License-Identifier: GPL-2.0

#define KBUILD_MODNAME "xdp_dummy"
#include <uapi/linux/bpf.h>

// #include <linux/bpf.h>
// #include <linux/in.h>
// #include <linux/if_ether.h>
// #include <linux/if_packet.h>
// #include <linux/if_vlan.h>
// #include <linux/ip.h>
// #include <linux/ipv6.h>

#include "bpf_helpers.h"

#define WIDTH 1280
#define HEIGHT 720

struct vertical_screen_line {
    char line[4 * WIDTH]; // 4 bytes per pixel for a complete vertical line
};

// This map stores an vertical line per entry
struct bpf_map_def SEC("maps") framebuffer = {
    .type = BPF_MAP_TYPE_ARRAY,
    .key_size = sizeof(u32), // MUST be 4 bytes (see 'man bpf')
    .value_size = sizeof(struct vertical_screen_line),
    .max_entries = HEIGHT,
};

SEC("prog")
int xdp_dummy(struct xdp_md *ctx)
{
    u32 x = 0;
    u32 y = 5000;
    u32 rgb = 0xaabbccdd;

    // bpf_map_update_elem(&testMap, &index, &insert, BPF_ANY);

    struct vertical_screen_line *vertical_screen_line;
    vertical_screen_line = bpf_map_lookup_elem(&framebuffer, &x);

    if (vertical_screen_line) {
        void* pointer = (void*)&vertical_screen_line->line + y;
        *((u32*) pointer) = 0xff;
        // *(&vertical_screen_line->line) = rgb;
    }
    //u32 foo = *value;

    return XDP_PASS; // TODO Change to XDP_DROP to ignore all traffic on the port to get max processing speed.
                     // Keep it for now for debugging purpose 
}

char _license[] SEC("license") = "GPL";

