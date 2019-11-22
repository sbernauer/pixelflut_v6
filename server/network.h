#ifndef _NETWORK_H_
#define _NETWORK_H_

#include <stdint.h>
#include <pthread.h>

#include "framebuffer.h"

#define likely(x)	__builtin_expect((x),1)
#define unlikely(x)	__builtin_expect((x),0)

int net_listen(int argc, char** argv, struct fb* fb, bool force_quit);

#endif
