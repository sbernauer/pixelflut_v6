#ifndef _FRAMEBUFFER_H_
#define _FRAMEBUFFER_H_

#include <stdint.h>

#include "llist.h"

#define COLORDEPTH 24

struct fb_size {
	unsigned int width;
	unsigned int height;
};

struct fb {
	struct fb_size size;
	uint32_t* pixels;
	unsigned numa_node;
	struct llist_entry list;

	unsigned long long pixelCounter;
};

#define likely(x)	__builtin_expect((x),1)
#define unlikely(x)	__builtin_expect((x),0)

// Management
int fb_alloc(struct fb** framebuffer, unsigned int width, unsigned int height);
void fb_free(struct fb* fb);
void fb_free_all(struct llist* fbs);
struct fb* fb_get_fb_on_node(struct llist* fbs, unsigned numa_node);

// Manipulation
void fb_set_pixel(struct fb* fb, unsigned int x, unsigned int y, uint32_t pixel);
void fb_set_pixel_rgb(struct fb* fb, unsigned int x, unsigned int y, uint8_t red, uint8_t green, uint8_t blue);
uint32_t fb_get_pixel(struct fb* fb, unsigned int x, unsigned int y);
int fb_resize(struct fb* fb, unsigned int width, unsigned int height);

// Info
inline struct fb_size* fb_get_size(struct fb* fb) {
	return &fb->size;
}

#endif
