/* Copyright (c) 2019
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#include <linux/unistd.h>
#include <linux/bpf.h>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>

#include <bpf/bpf.h>
#include "bpf_util.h"

#define WIDTH 1280
#define HEIGHT 720

int main(int argc, char **argv)
{
    printf("Starting user program\n");
    
    const char *map_filename = "/sys/fs/bpf/xdp/globals/xdp_pixelflut";
    int framebuffer = bpf_obj_get(map_filename);

    if (framebuffer <= 0) {
        printf("Cannot open map at %s. Check, that you run this program as root.\n", map_filename);
        return 1;
    }

    //assert(map_fd > 0);

    printf("Map id: %d\n", framebuffer);

    int index = 0;
    int rgb = 0;

    int x = 0;
    int y = 0;

    

    for (x = 0; x < WIDTH; x++) {
        for (y = 0; y < HEIGHT; y++) {
            index = x + y * WIDTH;
            // bpf_map_lookup_elem(framebuffer, &index, &rgb);
            if (rgb != 0) {

                printf("Result: rgb: %x\n", rgb);
            }   
        }
    }

    //int result = bpf_map_lookup_elem(&framebuffer, &index);

    return 0;
}
