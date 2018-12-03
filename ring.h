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
/*likely*/
#define likely(x) __builtin_expect((x),1)
/*unlikely*/
#define unlikely(x) __builtin_expect((x),0)

#define PAUSE() _mm_pause()

static inline int __attribute__((always_inline))
atomic_cmpset(volatile uint32_t *dst, uint32_t exp, uint32_t src)
{
   return __sync_bool_compare_and_swap(dst, exp, src);
}

enum ring_queue_behavior{
   RING_QUEUE_FIXED = 0,
   RING_QUEUE_VARIABLE
};

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
      for(i = 0; idx < size; i++, idx++) \
         r->ring[idx] = obj_table[i];\
      for(idx = 0; i < n; idx++) \
         r->ring[idx] = obj_table[i]; \
   } \
}while(0)

#define DEQUEUE_PTRS() do{ \
   const uint32_t size = r->cons.size; \
   uint32_t idx = cons_head & mask; \
   if(likely(idx + n < size)) \
   {\
      for(i=0;i<(n & (~(unsigned)0x3));i+=4, idx+=4){\
         obj_table[i] = r->ring[idx]; \
         obj_table[i+1] = r->ring[idx+1]; \
         obj_table[i+2] = r->ring[idx+2]; \
         obj_table[i+3] = r->ring[idx+3]; \
      }\
      switch(n & 0x3){\
         case 3: obj_table[i++] = r->ring[idx++]; \
         case 2: obj_table[i++] = r->ring[idx++]; \
         case 1: obj_table[i++] = r->ring[idx++]; \
      }\
   }\
   else{\
      for(i = 0; idx < size; i++, idx++)\
         obj_table[i] = r->ring[idx]; \
      for(idx = 0; i < n; i++, idx++) \
         obj_table[i] = r->ring[idx]; \
   }\
}while(0)

static inline int __attribute__((always_inline))
__ring_sp_do_enqueue(struct Ring* r, void * const *obj_table, unsigned n, enum ring_queue_behavior behavior)
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
      if(behavior == RING_QUEUE_FIXED){
         printf("Not enough space of the ring\n");
         return -1;
      }
      else{
         if(unlikely(free_entries == 0)){
            return 0;
         }
         n = free_entries;
      }
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
   return __ring_sp_do_enqueue(r, obj_table, n, RTE_RING_QUEUE_FIXED);
}

static inline int __attribute__((always_inline))
ring_sp_enqueue(struct Ring* r, void* obj)
{
   return ring_sp_enqueue_bulk(r, &obj, 1);
}

static inline int __attribute__((always_inline))
__ring_mp_do_enqueue(struct Ring* r, void* const *obj_table, unsigned n, enum ring_queue_behavior behavior)
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
         if(behavior == RING_QUEUE_FIXED){
            return -1;
         }
         else{
            if(unlikely(free_entries == 0)){
               return 0;    
	    }
            n = free_entries;
         }
      }
      prod_next =prod_head + n;
      success = atomic_cmpset(&r->prod.head,prod_head,prod_next);
   }while(unlikely(success == 0));
   /*Write entries in ring*/
   ENQUEUE_PTRS();
   MEM_BARRIER();
   /*if there are other enqueues in progress that preceded us*/
   while(unlikely(r->cons.tail != cons_head))
      PAUSE();
   
   r->prod.tail = prod_next;
   return 0;
}

static inline int __attribute__((always_inline))
ring_mp_enqueue_bulk(struct Ring* r, void * const *obj_table, unsigned n)
{
   return __ring_mp_do_enqueue(r, obj_table, n, RING_QUEUE_FIXED);
}

static inline int __attribute__((always_inline))
ring_mp_enqueue(struct Ring* r, void* obj)
{
   return ring_mp_enqueue_bulk(r, &obj, 1);
}

/*Enqueue one object on aring*/
static inline int __attribute__((always_inline))
ring_enqueue(struct Ring* r, void *obj)
{
   if(r->prod.sp_enqueue)
      return ring_sp_enqueue(r,obj);
   else
      return ring_mp_enqueue(r,obj);
}

static int __attribute__((always_inline))
__ring_sc_do_dequeue(struct Ring* r, void* const *obj_table, unsigned n, enum ring_mp_enqueue behavior)
{
   uint32_t cons_head, prod_tail;
   uint32_t cons_next, entries;
   unsigned i;
   uint32_t mask = r->prod.mask;

   cons_head = r->cond.head;
   prod_tail = r->prod.tail;
   entries = prod_tail - cons_head;
   /*check if we have enough obj to consume*/
   if(n > entries){
      if(behavior == RING_QUEUE_FIXED){
         return -1;
      }
      else{
         if(unlikely(entries == 0)){
            return 0;
         }
         n = entries;
      }
   }
   cons_next = cons_head + n;
   r->cons.head = cons_next;
   /*write in obj_table*/
   DEQUEUE_PTRS();
   MEM_BARRIER();

   r->cons.tail = cons.tail;
   return 0;
}

static int __attribute__((always_inline))
ring_sc_dequeue_bulk(struct Ring* r, void** obj_p, unsigned n)
{
   return __ring_sc_do_dequeue(r, obj_table, n, RING_QUEUE_FIXED);
}

static inline int __attribute__((always_inline))
ring_sc_dequeue(struct Ring *r, void **obj_p)
{
   return ring_sc_dequeue_bulk(r, obj_p, 1);
}

static inline int __attribute__((always_inline))
__ring_mc_do_dequeue(struct Ring* r, void* const *obj_table, unsigned n)
{
   uint32_t cons_head, prod_tail;
   uint32_t cons_next, entries;
   const unsigned max = n;
   unsigned i;
   int success;
   uint32_t mask = r->prod.mask;

   if(n == 0) 
      return 0;
   do{
      n = max;
      cons_head = r->cons.head;
      prod_tail = r->prod.tail;
      entries = prod_tail - cons_head;
      if(n > entries){
         if(behavior == RING_QUEUE_FIXED)
            return -1;
         else{
            if(unlikely(entries == 0)){
               return 0;    
	    } 
            n = entries;
	 }
      }
      cons_next = cons_head + n;
      success = atomic_cmpset(&r->cons.head, cons_head, cons_next);
   }while(unlikely(success == 0));
   /* copy in table */
   DEQUEUE_PTRS();
   MEM_BARRIER();

   while(unlikely(r->cons.tail != cons_head))
      PAUSE();
   r->cons.tail = cons_next;

   return 0;
}

static inline int __attribute__((always_inline))
ring_mc_dequeue_bulk(struct Ring* r, void **obj_p, unsigned n)
{
   return __ring_mc_do_dequeue(r, obj_p, n, RING_QUEUE_FIXED);
}

static inline int __attribute__((always_inline))
ring_mc_dequeue(struct Ring* r, void **obj_p)
{
   return ring_mc_dequeue_bulk(r, obj_p, 1);
}

/*Dequeue one object on a ring*/
static inline int __attribute__((always_inline))
ring_dequeue(struct Ring* r, void **obj_p)
{
   if(r->cons.sc_dequeue)
      return ring_sc_dequeue(r, obj_p);
   else
      return ring_mc_dequeue(r, obj_p);
}
#endif
