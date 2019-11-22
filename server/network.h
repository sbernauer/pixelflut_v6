#ifndef _NETWORK_H_
#define _NETWORK_H_

#include <stdint.h>
#include <pthread.h>

#include "framebuffer.h"

#define likely(x)	__builtin_expect((x),1)
#define unlikely(x)	__builtin_expect((x),0)

struct worker_thread_args
{
	// Network specific
    int port_id;
    int start_queue_id;

    // Framebuffer specific
    struct llist* fb_list;
    pthread_mutex_t fb_lock;
    struct fb_size* fb_size;
};

int net_listen(int argc, char** argv, struct llist* fb_list, pthread_mutex_t fb_lock, struct fb_size* fb_size, bool force_quit);

#endif
