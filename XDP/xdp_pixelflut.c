/* Copyright (c) 2019
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#define KBUILD_MODNAME "fooPixelflut"
#include <uapi/linux/bpf.h>
#include <linux/in.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <linux/if_vlan.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include "bpf_helpers.h"

#define WIDTH 1280
#define HEIGHT 720

struct bpf_map_def SEC("maps") framebuffer = {
	.type = BPF_MAP_TYPE_ARRAY,
	.key_size = sizeof(u32), // MUST be 4 bytes (see 'man bpf')
	.value_size = sizeof(u32), // 4 bytes:        Color: <rr><gg><bb><padding with 8 x 0>
                               //                 Coordinate: <padding with 16 x 0><u16>
	.max_entries = WIDTH * HEIGHT,
};

SEC("xdp_pixelflut_main")
int xdp_pixelflut(struct xdp_md *ctx)
{
    //return XDP_DROP;
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
        u32 index = x + y * WIDTH;
        //return XDP_DROP;


        bpf_map_update_elem(&framebuffer, &index, &rgb, BPF_ANY);

        // FOR DEBUGING
        // u32 write = 0;
        // bpf_map_update_elem(&framebuffer, &write, &x, BPF_ANY);

        // write = 1;
        // bpf_map_update_elem(&framebuffer, &write, &y, BPF_ANY);

        // write = 2;
        // bpf_map_update_elem(&framebuffer, &write, &rgb, BPF_ANY);

        // write = 3;
        // bpf_map_update_elem(&framebuffer, &write, &index, BPF_ANY);


        // u32 index = 0;
        // bpf_map_update_elem(&framebuffer, &index, &src, BPF_ANY);
        // index = 0;
        // bpf_map_update_elem(&framebuffer, &index, &dst, BPF_ANY);
    }

    return XDP_DROP; // TODO Change to XDP_DROP to ignore all traffic on the port to get max processing speed.
                     // Keep it for now for debugging purpose 
}

char _license[] SEC("license") = "GPL";
