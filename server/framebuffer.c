#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <string.h>

#include "util.h"
#include "framebuffer.h"

int fb_alloc(struct fb** framebuffer, unsigned int width, unsigned int height) {
	int err = 0;

	struct fb* fb = malloc(sizeof(struct fb));
	if(!fb) {
		err = -ENOMEM;
		goto fail;
	}

	fb->size.width = width;
	fb->size.height = height;

	fb->pixels = calloc(width * height, sizeof(uint32_t));
	if(!fb->pixels) {
		err = -ENOMEM;
		goto fail_fb;
	}

	fb->numa_node = get_numa_node();
	fb->list = LLIST_ENTRY_INIT;
	fb->pixelCounter = 0;

	*framebuffer = fb;
	return 0;

fail_fb:
	free(fb);
fail:
	return err;
}

struct fb* fb_get_fb_on_node(struct llist* fbs, unsigned numa_node) {
	struct llist_entry* cursor;
	struct fb* fb;
	llist_for_each(fbs, cursor) {
		fb = llist_entry_get_value(cursor, struct fb, list);
		if(fb->numa_node == numa_node) {
			return fb;
		}
	}
	return NULL;
}

void fb_free(struct fb* fb) {
	free(fb->pixels);
	free(fb);
}

void fb_free_all(struct llist* fbs) {
	struct llist_entry* cursor;
	llist_for_each(fbs, cursor) {
		free(llist_entry_get_value(cursor, struct fb, list));
	}
}

inline void fb_set_pixel(struct fb* fb, unsigned int x, unsigned int y, uint32_t pixel) {
	if (unlikely(x >= fb->size.width) || unlikely(y >= fb->size.height)) {
		//printf("Dropping invalid command with x: %d y: %d\n", x, y);
		return;
	}

	uint32_t* target;
	target = &(fb->pixels[y * fb->size.width + x]);
	*target = pixel;
	fb->pixelCounter++;
}

// It might be a good idea to offer a variant returning a pointer to avoid unnecessary copies
uint32_t fb_get_pixel(struct fb* fb, unsigned int x, unsigned int y) {
	assert(x < fb->size.width);
	assert(y < fb->size.height);

	return fb->pixels[y * fb->size.width + x];
}

static void fb_set_size(struct fb* fb, unsigned int width, unsigned int height) {
	fb->size.width = width;
	fb->size.height = height;
}

int fb_resize(struct fb* fb, unsigned int width, unsigned int height) {
	int err = 0;
	uint32_t* fbmem, *oldmem;
	struct fb_size oldsize = *fb_get_size(fb);
	size_t memsize = width * height * sizeof(uint32_t);
	size_t oldmemsize = oldsize.width * oldsize.height * sizeof(uint32_t);
	fbmem = malloc(memsize);
	if(!fbmem) {
		err = -ENOMEM;
		goto fail;
	}
	memset(fbmem, 0, memsize);

	oldmem = fb->pixels;
	// Try to prevent oob writes
	if(oldmemsize > memsize) {
		fb_set_size(fb, width, height);
		fb->pixels = fbmem;
	} else {
		fb->pixels = fbmem;
		fb_set_size(fb, width, height);
	}
	free(oldmem);
fail:
	return err;
}
