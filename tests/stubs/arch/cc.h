#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#define LWIP_PLATFORM_ASSERT(s) do { fprintf(stderr, "%s\n", s); abort(); } while (0)
#define LWIP_RAND() ((unsigned)rand())
#define PACK_STRUCT_STRUCT __attribute__((packed))
