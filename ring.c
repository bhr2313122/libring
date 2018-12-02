#include "ring.h"

ssize_t get_ring_memsize(unsigned count)
{
   ssize_t sz;
   /*count must be a power of 2*/
   if((!POWEROF2(count)) || (count > RING_SZ_MASK))
   {
	  printf("Ruqested size is invalid, must be power of 2");
      return -1;
   }
   sz = sizeof(struct Ring) + count * sizeof(void *);
   sz = SIZE_ALIGN(sz, CACHE_LINE);
   return sz;
}

int ring_init(struct Ring* r, const char *name, unsigned count, unsigned flags)
{
   int ret;
   memset(r, 0, sizeof(*r));
   /*name of ring*/
   ret = snprintf(r->name, sizeof(r->name), "%s", name);
   if (ret < 0 || ret >= (int)sizeof(r->name))
      return -1;
   r->flags = flags;
   r->prod.watermark = count;
   r->prod.sp_enqueue = !!(flags & RING_F_SP_ENQ);
   r->prod.sc_dequeue = !!(flags & RING_F_SC_DEQ);
   r->prod.size = r->cons.size = count;
   r->prod.mask = r->cons.mask = count -1;
   r->prod.head = r->cons.head = 0;
   r->prod.head = r->cons.tail = 0;
   
   return 0;
}

struct Ring* ring_create(const char* name, unsigned count,unsigned flags)
{
   int ret;
   struct Ring* r;
   ssize_t ring_size;
   ring_size = get_ring_memsize(count);
   if(ring_size == -1)
      return NULL;
   /*malloc space for ring*/
   r = (struct Ring*)malloc(ring_size);
   ret = ring_init(r, name, count, );
}
