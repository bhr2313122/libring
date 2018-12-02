#ifndef _RING_H
#define _RING_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RING_SZ_MASK  (unsigned)(0x0fffffff) //ring size mask
#define RING_NAMESIZE 32
#define CACHE_LINE    64
#define __cached_aligned __attribute__((__aligned__(CACHE_LINE)))
#define RING_F_SP_ENQ 0x0001 /**< The default enqueue is "single-producer". */
#define RING_F_SC_DEQ 0x0002 /**< The default dequeue is "single-consumer". */

#define POWEROF2(x) ((((x)-1))&(x) == 0)
/*Macro align*/
#define ALIGN_FLOOR(val, align) \
   (typeof(val))((val) & (~((typeof(val))((align)-1))))

#define SIZE_ALIGN(val, align) \
    ALIGN_FLOOR(((val) + ((typeof(val)) (align)-1)), align)

#define MEM_BARRIER() \
do { asm volatile("" ::: "memory"); } while(0)
	
#define PAUSE() _mm_pause()
	
struct Ring{
   char name[RING_NAMESIZE];//Name of the ring
   int flags;               //flags supplied at creation
   /*productor status*/
   struct prod{
      uint32_t watermark;   //Maximum items before EDQUOT.
      uint32_t sp_enqueue;  //True, if single producer.
      uint32_t size;        //size of ring
      uint32_t mask;        //Mask(size-1) of ring
      volatile uint32_t head;//producter head
      volatile uint32_t tail;//producter tail	  
   }cons __cached_aligned;
   /*consumer status*/
   struct cons{
      uint32_t sc_dequeue; //True if single consumer
      uint32_t size;
      uint32_t mask;
      volatile uint32_t head;
      volatile uint32_t tail;
   }cons __cached_aligned;
   
   void* ring[0] __cached_aligned; //Memory space of ring starts here.
};

/**
 * Calculate the memory size needed for a ring
 */
ssize_t get_ring_memsize(unsigned count);
/**
*  Initialize a ring structure
*/
int ring_init(struct Ring* r, const char *name, unsigned count, unsigned flags)
/**
   create a new named Ring
*/
struct Ring* ring_create(const char* name, unsigned count, unsigned flags);
/**
*   free a ring structure
*/
void ring_free(struct Ring* r);
/**
*   change the high water mark
*/
int ring_set_water_mark(struct Ring* r, unsigned count);

#define ENQUEUE_PTRS() do{ \
   const uint32_t size = r->prod.size; \
   uint32_t idx = prod_head & mask; \
   if(likely(idx + n < size)) \
   {\
      for (i = 0; i < (n & ((~(unsigned)0x3))); i+=4, idx+=4)\
	  {\
         r->ring[idx] = obj_table[i]; \
         r->ring[idx+1] = obj_table[i+1]; \
         r->ring[idx+2] = obj_table[i+2]; \
         r->ring[idx+3] = obj_table[i+3] \
	  }\
      switch(n&0x3){ \
         case 3: r->ring[idx++] = obj_table[i++]; \
         case 2: r->ring[idx++] = obj_table[i++]; \
         case 1: r->ring[idx++] = obj_table[i++]; \
      }\
   } \
   else \
   {\
      for(i = 0; idx < size; i++, idx++){ \
         r->ring[idx] = obj_table[i];\    
	  }\
	  for(idx = 0; i < n; idx++){    \
         r->ring[idx] = obj_table[i]; \
      } \
   }\
}while(0)

static inline int __attribute__((always_inline))
__ring_sp_do_enqueue(struct Ring* r, void * const *obj_table, unsigned n)
{
   uint32_t prod_head, cons_tail;
   uint32_t prod_next, free_entries;
   unsigned i;
   uint32_t mask = r->prod.mask;
   
   prod_head = r->prod.head;
   cons_tail = r->cons.tail;
   free_entries = mask + cons.tail - prod.head;
   /* check that we have enough room in ring */
   if(n > free_entries){
      printf("Not enough space of the ring\n");
      return -1;
   }
   prod_next = prod_head + n;
   r->prod.head = prod_next;
   /*write entries in ring*/
   ENQUEUE_PTRS();
   MEM_BARRIER();
   
   r->prod.tail = prod_next;
   return 0;
}

static inline int __attribute__((always_inline))
ring_sp_enqueue_bulk(struct Ring* r, void * const *obj_table, unsigned n)
{
   return __ring_sp_do_enqueue(r, obj_table, n);
}

static inline int __attribute__((always_inline))
ring_sp_enqueue(struct Ring* r, void* obj)
{
   return ring_sp_enqueue_bulk(r, &obj, 1);
}

static inline int __attribute__((always_inline))
__ring_mp_do_enqueue(struct Ring* r, void* const *obj_table, unsigned n)
{
   uint32_t prod_head, prod_next;
   uint32_t cons_tail, free_entries;
   uint32_t mask = r->prod.mask;
   unsigned i;
   const unsigned max = n;
   int success;
   
   if(n == 0)
      return 0;
   do{
      n = max;
      prod_head = r->prod.head;
      cons_tail = r->cons.tail;
      free_entries = (mask + cons_tail - prod_head);
	  /*Check if we have enough space*/
      if(unlikely(n > free_entries)){
         return -1;
	  }
      prod_next =prod_head + n;
      success = atomic_cmpset(&r->prod.head,prod_head,prod_next);
   }while(unlikely(success == 0));
   /*Write entries in ring*/
   ENQUEUE_PTRS();
   MEM_BARRIER();
   while(unlikely(r->cons.tail != cons_head))
      PAUSE();
   
   r->prod.tail = prod_next;
   return 0;
}

static inline int __attribute__((always_inline))
ring_mp_enqueue_bulk(struct Ring* r, void * const *obj_table, unsigned n)
{
   return __ring_mp_do_enqueue(r, obj_table, n);
}

static inline int __attribute__((always_inline))
ring_mp_enqueue(struct Ring* r, void* obj)
{
   return ring_mp_enqueue_bulk(r, &obj, 1);
}

/*Enqueue one object on aring*/
   if(r->prod.sp_enqueue)
      return ring_sp_enqueue(r,obj);
   else
      return ring_mp_enqueue(r,obj);
}
#endif