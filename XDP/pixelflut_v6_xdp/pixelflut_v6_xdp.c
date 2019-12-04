// SPDX-License-Identifier: GPL-2.0

// Working around "error: 'asm goto' constructs are not supported yet" in clang 6 https://github.com/iovisor/bcc/issues/2119.
#define asm_volatile_goto(x...)

#define KBUILD_MODNAME "pixelflut_v6_xdp"
//#include <uapi/linux/bpf.h>

#include <linux/bpf.h>
#include <linux/in.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <linux/if_vlan.h>
#include <linux/ip.h>
#include <linux/ipv6.h>

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
int pixelflut_v6_xdp(struct xdp_md *ctx)
{
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;
    struct ethhdr *eth = data;
    struct ipv6hdr *ip6h;
    u64 nh_off;
    u32 x, y, rgb;

    nh_off = sizeof(*eth);
    if (unlikely(data + nh_off > data_end)) // Is needed because of verifier
        return XDP_DROP;

    if (eth->h_proto == htons(ETH_P_IPV6)) {
        ip6h = data + nh_off;
        if (ip6h + 1 > data_end) // Is needed because of verifier
            return XDP_DROP;

        // if (ip6h->hop_limit <= 1)
        //     return XDP_PASS;

        // struct in6_addr src = ip6h->saddr;
        struct in6_addr dst = ip6h->daddr;
        void* pointToStartOfIp6 = &dst;

        x = (*((u8 *)(pointToStartOfIp6 + 8)) << 8) | (*((u8 *)(pointToStartOfIp6 + 9))); // Flip the first to byte, format is now left to right 00 00 ll hh, attention: byte order is opposite direction whe looking with 'bpftool map dump'
        y = (*((u8 *)(pointToStartOfIp6 + 10)) << 8) | (*((u8 *)(pointToStartOfIp6 + 11))); // Same for y
        rgb = (*((u8 *)(pointToStartOfIp6 + 12)) << 24) | (*((u8 *)(pointToStartOfIp6 + 13)) << 16) | (*((u8 *)(pointToStartOfIp6 + 14)) << 8); // Ignore last part of rrggbb>>>padding<<< | (*((u8 *)(pointToStartOfIp6 + 15)));

        if ( x >= WIDTH || y >= HEIGHT) {
            return XDP_DROP;
        }

        struct vertical_screen_line *vertical_screen_line;
        vertical_screen_line = bpf_map_lookup_elem(&framebuffer, &x);

        if (vertical_screen_line) {
            void* pointer = (void*)&vertical_screen_line->line + 4 * y;
            *((u32*) pointer) = 0xff;
            // *(&vertical_screen_line->line) = rgb;
        }
    }


    // // bpf_map_update_elem(&testMap, &index, &insert, BPF_ANY);

    // struct vertical_screen_line *vertical_screen_line;
    // vertical_screen_line = bpf_map_lookup_elem(&framebuffer, &x);

    // if (vertical_screen_line) {
    //     void* pointer = (void*)&vertical_screen_line->line + y;
    //     *((u32*) pointer) = 0xff;
    //     // *(&vertical_screen_line->line) = rgb;
    // }
    // //u32 foo = *value;

    return XDP_PASS; // TODO Change to XDP_DROP to ignore all traffic on the port to get max processing speed.
                     // Keep it for now for debugging purpose 
}

char _license[] SEC("license") = "GPL";

