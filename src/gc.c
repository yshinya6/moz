/****************************************************************************
 * Copyright (c) 2012-2013, the Konoha project authors. All rights reserved.
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 ***************************************************************************/

/* ************************************************************************ */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

// #define GCDEBUG 1
// #define GCSTAT 1

#ifdef __cplusplus
extern "C" {
#endif

#if defined(GCDEBUG) && !defined(GCSTAT)
#define GCSTAT 1
#endif

//#define MEMORY_DEBUG 1

/* memory config */

#define GC_USE_DEFERREDSWEEP 1
// #define USE_SAFEPOINT_POLICY 1
#define SUBHEAP_DEFAULT_SEGPOOL_SIZE (128)/* 128 * SEGMENT_SIZE(128k) = 16MB*/
#define SUBHEAP_KLASS_MIN  5 /* 1 <<  5 == 32 */
#define SUBHEAP_KLASS_MAX 12 /* 1 << 12 == 4096 */
#define SEGMENT_LEVEL 3
#define MIN_ALIGN (1UL << SUBHEAP_KLASS_MIN)

#define KB_   (1024)
#define MB_   (KB_*1024)
#define K_PAGESIZE        4096

#define SEGMENT_SIZE (128 * KB_)
#define PTR_SIZE (sizeof(void *))
#define BITS     (PTR_SIZE * 8)
#define PowerOf2(N) (1UL << N)
#define ALIGN(X,N)  (((X)+((N)-1))&(~((N)-1)))
#define CEIL(F)     (F-(int)(F) > 0 ? (int)(F+1) : (int)(F))

#ifdef USE_GENERATIONAL_GC
#define MINOR_COUNT 16
#endif

#define BITMAP_FULL ((uintptr_t)(-1))
#define SUBHEAP_KLASS_SIZE_MIN PowerOf2(SUBHEAP_KLASS_MIN)
#define SUBHEAP_KLASS_SIZE_MAX PowerOf2(SUBHEAP_KLASS_MAX)
#define BM_SET(m, mask)  (m |= mask)
#define BM_TEST(m, mask) (m  & mask)

#ifndef unlikely
#define unlikely(x)   __builtin_expect(!!(x), 0)
#endif

#ifndef likely
#define likely(x)     __builtin_expect(!!(x), 1)
#endif

#ifdef __GNUC__
#define prefetch_(addr, rw, locality) __builtin_prefetch(addr, rw, locality)
#else
#define prefetch_(addr, rw, locality)
#endif

#if SIZEOF_VOIDP*8 == 64
#define PREFIX_x "lx"
#define PREFIX_d "ld"
#else
#define PREFIX_x "x"
#define PREFIX_d "d"
#endif

#if _WIN64
#define FFS(n) __builtin_ffsll(n)
#define CLZ(n) __builtin_clzll(n)
#define CTZ(n) __builtin_ctzll(n)
#else
#define FFS(n) __builtin_ffsl(n)
#define CLZ(n) __builtin_clzl(n)
#define CTZ(n) __builtin_ctzl(n)
#endif
#define BSR(n) CLZ(n)

static inline void *do_malloc(size_t size);
static inline void *do_calloc(size_t count, size_t size);
static inline void *do_realloc(void *ptr, size_t oldSize, size_t newSize);
static inline void  do_free(void *ptr, size_t size);
static inline void  do_bzero(void *ptr, size_t size);

/* [bitmap ops] */
typedef uintptr_t bitmap_t;

static inline void bitmap_reset(bitmap_t *bm, bitmap_t val)
{
    *bm = val;
}

static inline int bitmap_get(bitmap_t *bm, unsigned index)
{
    bitmap_t mask = 1UL << (index % BITS);
    return (*bm & mask) != 0;
}

static inline void bitmap_set(bitmap_t *bm, unsigned index, unsigned val)
{
    bitmap_t mask = (uintptr_t)val << (index % BITS);
    *bm |= mask;
}

static inline void bitmap_flip(bitmap_t *bm, unsigned index)
{
    bitmap_t mask = 1UL << (index % BITS);
    *bm ^= mask;
}

/* [BMGC Deta Structure] */
struct SubHeap;
struct Segment;
union  AllocationNode;
struct HeapManager;

typedef struct GCRoot {
    void *ptr;
    f_trace trace;
} GCRoot;
DEF_ARRAY_T_OP(GCRoot);

typedef void NodePtr;
typedef struct SubHeap SubHeap;
typedef struct Segment Segment;
typedef struct HeapManager HeapManager;
typedef union  AllocationNode AllocationNode;

typedef struct BitPtr {
    uintptr_t idx;
    uintptr_t mask;
} BitPtr;

typedef struct AllocationPointer {
    BitPtr bitptrs[SEGMENT_LEVEL];
    Segment *seg;
    NodePtr *blockptr;
} AllocationPointer;

struct SubHeap {
    AllocationPointer p;
    int heap_klass;
#ifdef USE_GENERATIONAL_GC
    int minor_count;
#endif
    Segment *freelist;
    Segment **seglist;
    unsigned seglist_size;
    unsigned seglist_max;
};

#define for_each_heap(H, I, HEAPS) \
    for (I = SUBHEAP_KLASS_MIN, H = (HEAPS)+I; I <= SUBHEAP_KLASS_MAX; ++H, ++I)

typedef struct Node kObject;
typedef struct MarkStack {
    kObject **stack;
    size_t tail;
    size_t capacity;
    size_t capacity_log2;
} MarkStack;

typedef Segment *SegmentPtr;
typedef void *VoidPtr;
typedef kObject *ObjectPtr;
typedef bitmap_t *BitMapPtr;
DEF_ARRAY_T_OP_NOPOINTER(SegmentPtr);
DEF_ARRAY_T_OP_NOPOINTER(size_t);
DEF_ARRAY_T_OP_NOPOINTER(VoidPtr);
DEF_ARRAY_T_OP_NOPOINTER(ObjectPtr);
DEF_ARRAY_T_OP_NOPOINTER(BitMapPtr);

struct HeapManager {
    bitmap_t flags;
    SubHeap heaps[SUBHEAP_KLASS_MAX+1];
    MarkStack mstack;
#if defined(USE_GENERATIONAL_GC)
    ARRAY(BitMapPtr)  remember_sets;
#endif
    Segment *segmentList;
    ARRAY(SegmentPtr) segment_pool_a;
    ARRAY(size_t)     segment_size_a;
    ARRAY(VoidPtr)    managed_heap_a;
    ARRAY(VoidPtr)    managed_heap_end_a;
    ARRAY(size_t)     heap_size_a;
    ARRAY(GCRoot)     roots;
};

struct Segment {
    bitmap_t *base[SEGMENT_LEVEL];
    const AllocationNode *block;
    int heap_klass;
    unsigned int live_count;
    struct Segment *next;
#ifdef USE_GENERATIONAL_GC
    bitmap_t *snapshots[SEGMENT_LEVEL];
    unsigned int tenure_live_count;
    bitmap_t *remember_set; /* for debug */
#else
    void *unused;
#endif
#if GCDEBUG
    void  *managed_heap;
    void  *managed_heap_end;
#endif
};

typedef struct NodeHeader {
    Segment *seg;
    long klass;
#if defined(USE_GENERATIONAL_GC)
    bitmap_t *remember_set;
#endif
} NodeHeader;

typedef struct gc_stat {
    size_t total_object;
    size_t object_count[SUBHEAP_KLASS_MAX+1];
    size_t gc_count;
    size_t marked[SUBHEAP_KLASS_MAX+1];
    size_t collected[SUBHEAP_KLASS_MAX+1];
    size_t markingTime;
    size_t sweepingTime;
    size_t current_request_size;
    AllocationNode *managed_heap;
    AllocationNode *managed_heap_end;
    FILE *fp;
} bmgc_stat;

#ifdef GCSTAT
static bmgc_stat global_gc_stat = {};
#endif

#define DEF_BM(size)  struct bm##size { uintptr_t bm[size]; }
DEF_BM(  1);DEF_BM(  2);DEF_BM(  4);
DEF_BM(  8);DEF_BM( 16);DEF_BM( 32);
DEF_BM( 64);DEF_BM(128);DEF_BM(256);
#define BITMAP_L0_SIZE(N) (CEIL(((float)SEGMENT_SIZE)/PowerOf2(N)/BITS))
#define BITMAP_L1_SIZE(N) (CEIL(((float)SEGMENT_SIZE)/PowerOf2(N)/BITS/BITS))
#define BITMAP_L2_SIZE(N) (CEIL(((float)SEGMENT_SIZE)/PowerOf2(N)/BITS/BITS/BITS))

static const unsigned SegmentBitMapCount[] = {
    0,0,0,0,0,
    BITMAP_L0_SIZE(5 ),
    BITMAP_L0_SIZE(6 ),
    BITMAP_L0_SIZE(7 ),
    BITMAP_L0_SIZE(8 ),
    BITMAP_L0_SIZE(9 ),
    BITMAP_L0_SIZE(10),
    BITMAP_L0_SIZE(11),
    BITMAP_L0_SIZE(12)
};

#define BITMAP_INFO_LIST(OP)\
    OP( 5, BITMAP_L0_SIZE( 5), BITMAP_L1_SIZE( 5), BITMAP_L2_SIZE( 5))\
OP( 6, BITMAP_L0_SIZE( 6), BITMAP_L1_SIZE( 6), BITMAP_L2_SIZE( 6))\
OP( 7, BITMAP_L0_SIZE( 7), BITMAP_L1_SIZE( 7), BITMAP_L2_SIZE( 7))\
OP( 8, BITMAP_L0_SIZE( 8), BITMAP_L1_SIZE( 8), BITMAP_L2_SIZE( 8))\
OP( 9, BITMAP_L0_SIZE( 9), BITMAP_L1_SIZE( 9), BITMAP_L2_SIZE( 9))\
OP(10, BITMAP_L0_SIZE(10), BITMAP_L1_SIZE(10), BITMAP_L2_SIZE(10))\
OP(11, BITMAP_L0_SIZE(11), BITMAP_L1_SIZE(11), BITMAP_L2_SIZE(11))\
OP(12, BITMAP_L0_SIZE(12), BITMAP_L1_SIZE(12), BITMAP_L2_SIZE(12))

#if SIZEOF_VOIDP*8 == 64
struct BM5  { bitmap_t m0[64]; bitmap_t m1[1]; bitmap_t m2[1]; };
struct BM6  { bitmap_t m0[32]; bitmap_t m1[1]; bitmap_t m2[1]; };
struct BM7  { bitmap_t m0[16]; bitmap_t m1[1]; bitmap_t m2[1]; };
struct BM8  { bitmap_t m0[ 8]; bitmap_t m1[1]; bitmap_t m2[1]; };
struct BM9  { bitmap_t m0[ 4]; bitmap_t m1[1]; bitmap_t m2[1]; };
struct BM10 { bitmap_t m0[ 2]; bitmap_t m1[1]; bitmap_t m2[1]; };
struct BM11 { bitmap_t m0[ 1]; bitmap_t m1[1]; bitmap_t m2[1]; };
struct BM12 { bitmap_t m0[ 1]; bitmap_t m1[1]; bitmap_t m2[1]; };
#elif SIZEOF_VOIDP*8 == 32
struct BM5  { bitmap_t m0[128]; bitmap_t m1[4]; bitmap_t m2[1]; };
struct BM6  { bitmap_t m0[ 64]; bitmap_t m1[2]; bitmap_t m2[1]; };
struct BM7  { bitmap_t m0[ 32]; bitmap_t m1[1]; bitmap_t m2[1]; };
struct BM8  { bitmap_t m0[ 16]; bitmap_t m1[1]; bitmap_t m2[1]; };
struct BM9  { bitmap_t m0[  8]; bitmap_t m1[1]; bitmap_t m2[1]; };
struct BM10 { bitmap_t m0[  4]; bitmap_t m1[1]; bitmap_t m2[1]; };
struct BM11 { bitmap_t m0[  2]; bitmap_t m1[1]; bitmap_t m2[1]; };
struct BM12 { bitmap_t m0[  1]; bitmap_t m1[1]; bitmap_t m2[1]; };
#else /* SIZEOF_VOIDP*8 != 32 */
#define BITMAP_LAYOUT(KLASS, N0, N1, N2)\
    struct BM##KLASS { bitmap_t m0[N0]; bitmap_t m1[N1]; bitmap_t m2[N2];};
BITMAP_INFO_LIST(BITMAP_LAYOUT);
#undef BITMAP_LAYOUT
#endif /* SIZEOF_VOIDP*8 != 64 */

#define _BLOCK_(size)  struct block##size{uint8_t m[size];} \
    b##size [SEGMENT_SIZE/(sizeof(struct block##size))]
union AllocationNode {
    _BLOCK_(8   );_BLOCK_(16  );_BLOCK_(32  );_BLOCK_(64  );
    _BLOCK_(128 );_BLOCK_(256 );_BLOCK_(512 );_BLOCK_(1024);
    _BLOCK_(2048);_BLOCK_(4096);
};

#define SEGMENT_BLOCK_COUNT(n) ((n >= SUBHEAP_KLASS_MIN)?(SEGMENT_SIZE / PowerOf2(n ) - 1):0)
static const unsigned SegmentNodeCount[] = {
    0, 0, 0,
    SEGMENT_BLOCK_COUNT(3 ), SEGMENT_BLOCK_COUNT(4 ),
    SEGMENT_BLOCK_COUNT(5 ), SEGMENT_BLOCK_COUNT(6 ),
    SEGMENT_BLOCK_COUNT(7 ), SEGMENT_BLOCK_COUNT(8 ),
    SEGMENT_BLOCK_COUNT(9 ), SEGMENT_BLOCK_COUNT(10),
    SEGMENT_BLOCK_COUNT(11), SEGMENT_BLOCK_COUNT(12),
};

#define MARGINE 0.975
static const unsigned int SegmentNodeCount_GC_MARGIN[] = {
    0, 0, 0,
    CEIL(SEGMENT_BLOCK_COUNT(3 )*MARGINE), CEIL(SEGMENT_BLOCK_COUNT(4 )*MARGINE),
    CEIL(SEGMENT_BLOCK_COUNT(5 )*MARGINE), CEIL(SEGMENT_BLOCK_COUNT(6 )*MARGINE),
    CEIL(SEGMENT_BLOCK_COUNT(7 )*MARGINE), CEIL(SEGMENT_BLOCK_COUNT(8 )*MARGINE),
    CEIL(SEGMENT_BLOCK_COUNT(9 )*MARGINE), CEIL(SEGMENT_BLOCK_COUNT(10)*MARGINE),
    CEIL(SEGMENT_BLOCK_COUNT(11)*MARGINE), CEIL(SEGMENT_BLOCK_COUNT(12)*MARGINE),
};

static bitmap_t bitmap_empty = BITMAP_FULL;
static bitmap_t *bitmap_dummy = &bitmap_empty;
static Segment segment_dummy = {
    {0}, 0, 0, 0, 0,
#ifdef USE_GENERATIONAL_GC
    {0}, 0, 0,
#else
    0,
#endif
#if GCDEBUG
    0, 0
#endif
};

static const unsigned int BITMAP_LIMIT[][SEGMENT_LEVEL] = {
    {0/* klass0 */}, {0/* klass1 */}, {0/* klass2 */}, {0/* klass3 */}, {0/* klass4 */},
#define BITMAP_SIZE_IN_EACH_LEVEL(KLASS, N0, N1, N2) {N0, N1, N2},
    BITMAP_INFO_LIST(BITMAP_SIZE_IN_EACH_LEVEL)
#undef BITMAP_SIZE_IN_EACH_LEVEL
};

static const unsigned int BITMAP_OFFSET[][SEGMENT_LEVEL] = {
    {0/* klass0 */}, {0/* klass1 */}, {0/* klass2 */}, {0/* klass3 */}, {0/* klass4 */},
#define OFFSET(TYPE, FIELD) ((unsigned) ((unsigned long)&(((struct BM##TYPE *) 0)->FIELD))/sizeof(bitmap_t))
#define BITMAP_OFFSET(K, N0, N1, N2) {OFFSET(K, m0), OFFSET(K, m1), OFFSET(K, m2)},
    BITMAP_INFO_LIST(BITMAP_OFFSET)
#undef BITMAP_OFFSET
};

static const uintptr_t BITMAP_MASK[][SEGMENT_LEVEL] = {
#define BITMASK_DEFAULT {0, 0, 0}
    BITMASK_DEFAULT /* klass0 */,
    BITMASK_DEFAULT /* klass1 */,
    BITMASK_DEFAULT /* klass2 */,
    BITMASK_DEFAULT /* klass3 */,
    BITMASK_DEFAULT /* klass4 */,
#undef BITMASK_DEFAULT
#if SIZEOF_VOIDP*8 == 32
    {/* klass5  */              0,               0, BITMAP_FULL<<4},
    {/* klass6  */              0,               0, BITMAP_FULL<<2},
    {/* klass7  */              0,               0, BITMAP_FULL<<1},
    {/* klass8  */              0, BITMAP_FULL<<16, BITMAP_FULL},
    {/* klass9  */              0, BITMAP_FULL<< 8, BITMAP_FULL},
    {/* klass10 */              0, BITMAP_FULL<< 4, BITMAP_FULL},
    {/* klass11 */              0, BITMAP_FULL<< 2, BITMAP_FULL},
    {/* klass12 */              0, BITMAP_FULL<< 1, BITMAP_FULL},
#else
    {/* klass5  */              0,               0, BITMAP_FULL},
    {/* klass6  */              0, BITMAP_FULL<<32, BITMAP_FULL},
    {/* klass7  */              0, BITMAP_FULL<<16, BITMAP_FULL},
    {/* klass8  */              0, BITMAP_FULL<< 8, BITMAP_FULL},
    {/* klass9  */              0, BITMAP_FULL<< 4, BITMAP_FULL},
    {/* klass10 */              0, BITMAP_FULL<< 2, BITMAP_FULL},
    {/* klass11 */              0, BITMAP_FULL<< 1, BITMAP_FULL},
    {/* klass12 */BITMAP_FULL<<32, BITMAP_FULL<< 0, BITMAP_FULL},
#endif
};

#ifdef GCDEBUG
static void BitMapTree_Check_align(bitmap_t *base, unsigned klass)
{
#define DEBUG_CHECK_OFFSET(N)\
    if(klass == N) {\
        struct BM##N *bm##N = (struct BM##N *) base;\
        assert(bm##N->m0 == base + BITMAP_OFFSET[klass][0]/(sizeof(uintptr_t)));\
        assert(bm##N->m1 == base + BITMAP_OFFSET[klass][1]/(sizeof(uintptr_t)));\
        assert(bm##N->m2 == base + BITMAP_OFFSET[klass][2]/(sizeof(uintptr_t)));\
        return;\
    }
    DEBUG_CHECK_OFFSET(5);
    DEBUG_CHECK_OFFSET(6);
    DEBUG_CHECK_OFFSET(7);
    DEBUG_CHECK_OFFSET(8);
    DEBUG_CHECK_OFFSET(9);
    DEBUG_CHECK_OFFSET(10);
    DEBUG_CHECK_OFFSET(11);
    DEBUG_CHECK_OFFSET(12);
}
#endif

static void BitMapTree_init(bitmap_t *base[SEGMENT_LEVEL], unsigned klass)
{
    unsigned i;
    bitmap_t *bitmap = base[0];
    for (i = 1; i < SEGMENT_LEVEL; ++i) {
        unsigned offset = BITMAP_OFFSET[klass][i];
        base[i] = bitmap + offset;
    }
}

static void BITPTRS_INIT(BitPtr bitptrs[SEGMENT_LEVEL], Segment *seg, unsigned klass)
{
    unsigned i;
    BitMapTree_init(seg->base, klass);
    for (i = 0; i < SEGMENT_LEVEL; ++i) {
        bitptrs[i].idx  = 0;
        bitptrs[i].mask = 1;
    }
}

static void BITMAP_SET_LIMIT(bitmap_t *bitmap, unsigned klass)
{
    bitmap[BITMAP_OFFSET[klass][1]-1] = BITMAP_MASK[klass][0];
    bitmap[BITMAP_OFFSET[klass][2]-1] = BITMAP_MASK[klass][1];
    bitmap[BITMAP_OFFSET[klass][2]  ] = BITMAP_MASK[klass][2];
    BM_SET(bitmap[0], 1);
}

#ifdef USE_GENERATIONAL_GC
static void BITMAP_SET_LIMIT_AND_COPY_BM(bitmap_t *bitmap, bitmap_t *snapshot, unsigned klass)
{
    BITMAP_SET_LIMIT(bitmap, klass);
    bitmap[BITMAP_OFFSET[klass][1]-1] |= snapshot[BITMAP_OFFSET[klass][1]-1];
    bitmap[BITMAP_OFFSET[klass][2]-1] |= snapshot[BITMAP_OFFSET[klass][2]-1];
    bitmap[BITMAP_OFFSET[klass][2]  ] |= snapshot[BITMAP_OFFSET[klass][2]  ];
    BM_SET(bitmap[0], 1);
    BM_SET(snapshot[0], 1);
}

static void SNAPSHOT_INIT(Segment *seg, unsigned klass)
{
    BitMapTree_init(seg->snapshots, klass);
}
#endif

static const unsigned BM_SIZE[] = {
    0, 0, 0, 0, 0,
    sizeof(struct BM5),  sizeof(struct BM6),
    sizeof(struct BM7),  sizeof(struct BM8),
    sizeof(struct BM9),  sizeof(struct BM10),
    sizeof(struct BM11), sizeof(struct BM12),
};

#define AllocBitMap(n)      ((bitmap_t *)(do_malloc(BM_SIZE[n])))
#define DeleteBitMap(bm, n) ((do_free((void *)bm, BM_SIZE[n])))
#define ClearBitMap(bm, n)  (do_bzero((void *)bm, BM_SIZE[n]))

#if GCDEBUG
#define gc_info(fmt, ...)  fprintf(stderr, "(%s:%d) " fmt "\n" , __func__, __LINE__,  ## __VA_ARGS__)
#define gc_debug(fmt, ...) fprintf(stderr, "(%s:%d) " fmt "\n" , __func__, __LINE__, ## __VA_ARGS__)
#define gc_stat(fmt, ...)  gc_debug(fmt, ##__VA_ARGS__);\
    fprintf(stderr, "(%s:%d) " fmt "\n" , __func__, __LINE__,  ## __VA_ARGS__)
#else
#define gc_info(fmt, ...)
#define gc_debug(fmt, ...)
#define gc_stat(fmt, ...)  fprintf(stderr, "(%s:%d) " fmt "\n" , __func__, __LINE__,  ## __VA_ARGS__)
#endif

#define Object_SetTenure(o) KFlag_Set(uintptr_t,(o)->flag, 1,1)
#define Object_SetYoung(o)  KFlag_Set(uintptr_t,(o)->flag, 1,0)
#define Object_isTenure(o) (KFlag_Is(uintptr_t, (o)->flag, 1))
#define Object_isYoung(o)  (!Object_isTenure(o))

enum gc_mode {
#define GC_MINOR_FLAG 0
#define GC_MAJOR_FLAG 1
    GC_NOP   = 0,
    GC_MINOR = 1 << GC_MINOR_FLAG,
    GC_MAJOR = 1 << GC_MAJOR_FLAG,
    GC_MAJOR_AND_MINOR = GC_MINOR|GC_MAJOR
};

/* ------------------------------------------------------------------------ */
/* [BMGC API declaration] */
/* ------------------------------------------------------------------------ */

static kObject *bm_malloc_internal(HeapManager *mng, size_t n);
static void bitmapMarkingGC(HeapManager *mng, enum gc_mode mode);
static HeapManager *HeapManager_init(size_t heap_size);
static void HeapManager_delete(HeapManager *mng);
static void HeapManager_final_free(HeapManager *mng);
static inline void bmgc_Object_free(kObject *o);
static bool findNextFreeNode(AllocationPointer *p);
static void BMGC_dump(HeapManager *mng);

/* ------------------------------------------------------------------------ */
/* malloc */

static void THROW_OutOfMemory(size_t size)
{
    exit(EXIT_FAILURE);
}

static void *call_malloc_aligned(size_t size, size_t align)
{
    void *block = NULL;
#if defined(HAVE_POSIX_MEMALIGN)
    int ret = posix_memalign(&block, align, size);
    (void)ret;
#elif defined(HAVE_MEMALIGN)
    block = memalign(align, size);
#elif defined(K_USING_WINDOWS_)
    block = VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
#else
    block = malloc(size + align);
    if(unlikely(block == NULL))
        return NULL;
    if((uintptr_t)block % align != 0) {
        char *t2 = (char *)((((uintptr_t)block / align) + 1) * align);
        void **p = (void**)(t2 + size);
        assert((char *)p < ((char *)block) + size + align);
        p[0] = block;
        block = (void *)t2;
        assert((uintptr_t)block % align == 0);
    }
    else {
        void **p = (void **)((char *)block + size);
        p[0] = block;
    }
#endif
    return block;
}

static void call_free_aligned(void *block, size_t size)
{
#if defined(HAVE_POSIX_MEMALIGN) || defined(HAVE_MEMALIGN)
    free(block);
#elif defined(K_USING_WINDOWS_)
    VirtualFree(block, 0, MEM_RELEASE);
#else
    void **p = (void **)((char *)block + size);
    block = p[0];
    free(block);
#endif
}

#if GCDEBUG
static size_t malloced_size = 0;
#define DBG_CHECK_MALLOCED_SIZE()      assert(malloced_size == 0)
#define DBG_CHECK_MALLOCED_INC_SIZE(n) (malloced_size += (n))
#define DBG_CHECK_MALLOCED_DEC_SIZE(n) (malloced_size -= (n))
#else
#define DBG_CHECK_MALLOCED_SIZE()
#define DBG_CHECK_MALLOCED_INC_SIZE(n)
#define DBG_CHECK_MALLOCED_DEC_SIZE(n)
#endif

static inline void do_bzero(void *ptr, size_t size)
{
    memset(ptr, 0, size);
}

static inline void do_memcpy(void *s1, void *s2, size_t size)
{
    memcpy(s1, s2, size);
}

static inline void *do_malloc(size_t size)
{
    void *ptr = malloc(size);
    do_bzero(ptr, size);
    DBG_CHECK_MALLOCED_INC_SIZE(size);
    return ptr;
}

static inline void *do_calloc(size_t count, size_t size)
{
    void *ptr = calloc(count, size);
    DBG_CHECK_MALLOCED_INC_SIZE(size);
    return ptr;
}

static inline void *do_realloc(void *ptr, size_t oldSize, size_t newSize)
{
    char *newptr = (char *) realloc(ptr, newSize);
    do_bzero(newptr+oldSize, newSize-oldSize);
    DBG_CHECK_MALLOCED_INC_SIZE(newSize - oldSize);
    return (void *) newptr;
}

static inline void do_free(void *ptr, size_t size)
{
#if GCDEBUG
    memset(ptr, 0xa, size);
#endif
    DBG_CHECK_MALLOCED_DEC_SIZE(size);
    free(ptr);
}


/* ------------------------------------------------------------------------ */
/* [mstack] */

static MarkStack *mstack_init(MarkStack *mstack)
{
    if(mstack->capacity == 0) {
        mstack->capacity_log2 = 12;
        mstack->capacity = (1 << mstack->capacity_log2) - 1;
        assert(K_PAGESIZE == 1 << 12);
        mstack->stack = (kObject**)do_malloc(sizeof(kObject *)*(mstack->capacity + 1));
    }
    mstack->tail = 0;
    return mstack;
}

static void mstack_push(MarkStack *mstack, kObject *ref)
{
    size_t ntail = (mstack->tail + 1) & mstack->capacity;
    if(unlikely(ntail == 0)) {
        size_t capacity = 1 << mstack->capacity_log2;
        size_t stacksize = sizeof(kObject *) * capacity;
        mstack->stack = (kObject**)do_realloc(mstack->stack, stacksize, stacksize * 2);
        mstack->capacity_log2 += 1;
        mstack->capacity = (1 << mstack->capacity_log2) - 1;
        ntail = (mstack->tail + 1) & mstack->capacity;
    }
    mstack->stack[mstack->tail] = ref;
    mstack->tail = ntail;
}

static kObject *mstack_next(MarkStack *mstack)
{
    kObject *ref = NULL;
    if(likely(mstack->tail != 0)) {
        mstack->tail -=1;
        ref = mstack->stack[mstack->tail];
        prefetch_(ref, 0, 0);
    }
    return ref;
}

// static void KnewGcContext(KonohaContext *kctx)
// {
// 	size_t default_size = SUBHEAP_DEFAULT_SEGPOOL_SIZE;
// #ifdef GC_CONFIG
// 	char *poolsize = knh_PLATAPI getenv_i("KONOHA_DEFAULT_MEMPOOL_SIZE");
// 	if(poolsize) {
// 		kint_t tmp;
// 		if(knh_bytes_Parseint(B(poolsize), &tmp))
// 			default_size = (size_t) tmp;
// 	}
// #endif
// 	((KonohaContextVar *)kctx)->gcContext = HeapManager_init(kctx, default_size);
// }
//
// static void KdeleteGcContext(KonohaContext *kctx)
// {
// 	HeapManager *mng = (HeapManager *)kctx->gcContext;
// 	if(mng->mstack.capacity > 0) {
// 		do_free(mng->mstack.stack,  (mng->mstack.capacity + 1) * sizeof(kObject *));
// 		mng->mstack.stack    = NULL;
// 		mng->mstack.capacity = 0;
// 	}
// 	HeapManager_final_free(mng);
// 	HeapManager_delete(mng);
// }

static inline size_t SizeToKlass(size_t n) {
    size_t size = ALIGN(n, MIN_ALIGN);
    size_t size_w = size - 1;
    return (BITS - CLZ(size_w));
}

#define SEGMENTLIST_NEXT(seg, list) do {\
    seg  = (list);\
    assert(seg != NULL);\
    (list) = seg->next;\
    seg->next = NULL;\
} while(0)

#define BM_IS_FULL(BM) (~(BM) == 0)
#define SEG_BITMAP_N(seg, n, idx) ((bitmap_t *)((seg->base[n])+idx))
#define SEG_TRACE_BITMAP_N(seg, n, idx) ((bitmap_t *)((seg->trace[n])+idx))
#define AP_BITMAP_N(ap, n, idx)   SEG_BITMAP_N(ap->seg, n, idx)

static Segment *allocSegment(HeapManager *mng, int klass)
{
    Segment *seg = NULL;
    if(mng->segmentList) {
        SEGMENTLIST_NEXT(seg, mng->segmentList);
        gc_debug("klass=%d seg=%p mng->seg=%p", klass, seg, mng->segmentList);
    }
    return seg;
}

static void findNodeOfLastSegment(Segment *seg, SubHeap *h, size_t size)
{
    const AllocationNode *block = seg->block;
    NodeHeader *head = (NodeHeader *) block;
    head->seg   = seg;
    head->klass = seg->heap_klass;
    gc_info("seg=%p, block=(%p,%p)", seg, block, block+1);
    h->p.blockptr = (AllocationNode *)((char *)block+(size));
}

static bool newSegment(HeapManager *mng, SubHeap *h)
{
    unsigned klass = h->heap_klass;
    Segment *seg = allocSegment(mng, klass);
    assert(h->freelist == NULL);

    if(!seg) return false;
    assert(seg->live_count == 0);
    if(h->seglist_size == h->seglist_max) {
        size_t newSize, oldSize;
        oldSize = sizeof(Segment *)*h->seglist_max;
        newSize = sizeof(Segment *)*h->seglist_max * 2;
        h->seglist_max *= 2;
        h->seglist = (Segment**)(do_realloc(h->seglist, oldSize, newSize));
    }
    seg->base[0] = AllocBitMap(klass);
    seg->heap_klass = klass;
    h->seglist[h->seglist_size++] = seg;

    h->p.seg = seg;
    findNodeOfLastSegment(seg, h, PowerOf2(klass));
    BITPTRS_INIT(h->p.bitptrs, seg, klass);
    BITMAP_SET_LIMIT(seg->base[0], klass);
#ifdef USE_GENERATIONAL_GC
    seg->snapshots[0] = AllocBitMap(klass);
    SNAPSHOT_INIT(seg, klass);
    BITMAP_SET_LIMIT(seg->snapshots[0], klass);
#endif

    return true;
}

static inline Segment *freelist_pop(SubHeap *h)
{
    Segment *seg = h->freelist;
    h->freelist = seg->next;
    return seg;
}

static inline bool freelist_isEmpty(SubHeap *h)
{
    return (h->freelist == NULL);
}

static bool fetchSegment(SubHeap *h, unsigned klass)
{
    Segment *seg;
    if(freelist_isEmpty(h))
        return false;
    seg = freelist_pop(h);
    h->p.seg = seg;
    BITPTRS_INIT(h->p.bitptrs, seg, klass);
    return true;
}

static bool nextSegment(HeapManager *mng, SubHeap *h, AllocationPointer *p)
{
    Segment *seg;
    while(h->freelist != NULL) {
        seg = freelist_pop(h);
        assert(seg->live_count < SegmentNodeCount[h->heap_klass]);
        p->seg = seg;
        BITPTRS_INIT(h->p.bitptrs, seg, h->heap_klass);
        if(findNextFreeNode(p)) {
            gc_info("h[%d], seg=%p", h->heap_klass, seg);
            return true;
        }
    }
#ifdef USE_GENERATIONAL_GC
    bitmap_set(&mng->flags, GC_MINOR_FLAG,
            (uintptr_t)((--h->minor_count) & (MINOR_COUNT-1)) == 0);
#endif

    if(newSegment(mng, h)) {
        findNextFreeNode(p);
        return true;
    }

    return false;
}

#define BP(p, n) ((p)->bitptrs[n])
static void BitPtr0_inc(AllocationPointer *p)
{
    uintptr_t bpidx  = BP(p, 0).idx;
    uintptr_t bpmask = BP(p, 0).mask;
    bitmap_t *bm = AP_BITMAP_N(p, 0, bpidx);
    uintptr_t rot;
    BM_SET(*bm, bpmask);
    rot = bpmask >> (BITS - 1);

    BP(p, 0).mask = (bpmask << 1) | rot;
}

static bool inc(AllocationPointer *p, SubHeap *h)
{
    int size = PowerOf2(h->heap_klass);
    p->blockptr = (AllocationNode *)((char *)p->blockptr+size);
    BitPtr0_inc(p);
    return ++p->seg->live_count > SegmentNodeCount_GC_MARGIN[h->heap_klass];
}

static bool isMarked(AllocationPointer *p)
{
    uintptr_t idx  = BP(p, 0).idx;
    uintptr_t mask = BP(p, 0).mask;
    bitmap_t *bm = AP_BITMAP_N(p, 0, idx);
    return (BM_TEST(*bm, mask));
}

#define BITPTR_INIT_(bpidx, bpmask, idx) do {\
    bpidx  = idx / BITS;\
    bpmask = 1UL << (idx % BITS);\
} while(0)

static BitPtr *BitPtr_Init(BitPtr *bp, uintptr_t idx)
{
    BITPTR_INIT_(bp->idx, bp->mask, idx);
    return bp;
}

static uintptr_t bitptrToIndex(uintptr_t bpidx, uintptr_t bpmask)
{
    return bpidx * BITS + FFS(bpmask) - 1;
}

static NodePtr *blockAddress(Segment *seg, uintptr_t idx, uintptr_t mask)
{
    size_t size = seg->heap_klass;
    size_t offset = bitptrToIndex(idx, mask) << size;
    const NodePtr *ptr = seg->block;
    return (AllocationNode *)((char *)ptr+offset);
}

#define BP_NEXT_MASK(ap, bpidx, bpmask, j) do {\
    bitmap_t *bm   = AP_BITMAP_N(ap, j, bpidx);\
    uintptr_t temp = *bm | (bpmask - 1UL);\
    uintptr_t mask = (temp + 1UL) & ~temp;\
    bpmask = mask;\
} while(0)

static bitmap_t *bitmap_get_limit(bitmap_t *base, unsigned klass, unsigned level)
{
    return base + BITMAP_LIMIT[klass][level];
}

static void BitPtr_searchUnfilledNode(AllocationPointer *ap, BitPtr *bp, int level)
{
    bitmap_t *bm;
    bitmap_t *base = AP_BITMAP_N(ap, level, bp->idx);
    bitmap_t *limit = bitmap_get_limit(base, ap->seg->heap_klass, level);
    BM_SET(*base, bp->mask);

    bp->mask = 0;
    for (bm = base; bm < limit; ++bm) {
        bitmap_t mask = 1;
        while(1) {
            uintptr_t temp = *bm;
            bitmap_t *bitmap;
            mask = (temp + 1UL) & ~temp;
            if(mask == 0) {
                break;
            }
            bitmap = AP_BITMAP_N(ap, level-1, bitptrToIndex(bm - base, mask));
            if(BM_IS_FULL(*bitmap)) {
                BM_SET(*bm, mask);
                continue;
            }
            bp->idx  = bm - base;
            bp->mask = mask;
            return;
        }
    }
    return;
}

static bool findNextFreeNode(AllocationPointer *p)
{
    uintptr_t idx = BP(p, 0).idx;
    BP_NEXT_MASK(p, BP(p, 0).idx, BP(p, 0).mask, 0);
    if(BP(p, 0).mask == 0) {
        BitPtr *bp;
        unsigned i;
#if GCDEBUG
        gc_info("klass=%" PREFIX_d ", idx=%" PREFIX_d " mask=%" PREFIX_x", seg=%p",
                (p->seg)?p->seg->heap_klass:-1,
                BP(p, 0).idx, BP(p, 0).mask, p->seg);
#endif
        for (i = 1; i < SEGMENT_LEVEL; ++i) {
            bp = BitPtr_Init(&BP(p, i), idx);
            BitPtr_searchUnfilledNode(p, bp, i);
            BP_NEXT_MASK(p, bp->idx, bp->mask, i);
            if(bp->mask != 0) {
                assert(BP(p, i).idx == bp->idx && BP(p, i).mask == bp->mask);
                break;
            }
            idx /= BITS;
        }
        if(i == SEGMENT_LEVEL)
            return false;
        do {
            --i;
            BP(p, i).idx  = bitptrToIndex(BP(p, i+1).idx, BP(p, i+1).mask);
            BP(p, i).mask = 1;
            BP_NEXT_MASK(p, BP(p, i).idx, BP(p,i).mask, i);
            gc_info("klass=%" PREFIX_d ", level=%" PREFIX_d " idx=%" PREFIX_d ", mask=%" PREFIX_x,
                    p->seg->heap_klass, i, BP(p, i).idx, BP(p, i).mask);
            assert(BP(p, i).mask != 0);
        } while(i > 0);
    }
    p->blockptr = blockAddress(p->seg, BP(p, 0).idx, BP(p, 0).mask);
    return true;
}

static void *tryAlloc(HeapManager *mng, SubHeap *h)
{
    AllocationPointer *p = &h->p;
    void *temp;
    bool isEmpty;
    if(isMarked(p)) {
        if(findNextFreeNode(p) == false) {
            if(nextSegment(mng, h, p) == false) {
                return NULL;
            }
        }
    }
    temp = p->blockptr;
    prefetch_(temp, 0, 0);
    isEmpty = inc(p, h);

#ifdef USE_GENERATIONAL_GC
    bitmap_set(&mng->flags, GC_MAJOR_FLAG,
            (mng->segmentList == NULL && h->freelist == NULL && isEmpty));
#else
    (void)isEmpty;
#endif
    return temp;
}

#define HEAP_SEGMENTLIST_INIT_SIZE 16
static bool Heap_Init(HeapManager *mng, SubHeap *h, int klass)
{
    size_t i;

#ifdef USE_GENERATIONAL_GC
    h->minor_count = MINOR_COUNT;
#endif
    h->heap_klass = klass;
    h->seglist_size = 0;
    h->seglist_max  = HEAP_SEGMENTLIST_INIT_SIZE;
    h->seglist  = (Segment**)(do_malloc(sizeof(Segment**)*h->seglist_max));
    h->freelist = NULL;
    h->p.bitptrs[0].idx  = 0;
    h->p.bitptrs[0].mask = 1;
    h->p.seg = &segment_dummy;
    for (i = 0; i < SEGMENT_LEVEL; ++i) {
        h->p.seg->base[i] = (bitmap_t *)bitmap_dummy;
    }
    return true;
}

static void Heap_dispose(SubHeap *h)
{
    if(h->seglist) {
        do_free(h->seglist, sizeof(Segment**)*h->seglist_max);
    }
    do_bzero(h, sizeof(*h));
}

static Segment *SegmentPool_Init(size_t size, AllocationNode *block)
{
    size_t i;
    Segment *pool = (Segment *)(do_malloc(sizeof(Segment) * size));
    Segment *seg  = pool;
    Segment *next = seg+1, *tail = pool + size - 1;
    for (i = 0; i < size; ++i, ++block, ++next, ++seg) {
        seg->block = block;
        seg->next  = next;
#if GCDEBUG
        seg->managed_heap = block;
        seg->managed_heap_end = block+1;
#endif
    }
    tail->next = NULL;
    return pool;
}

#if defined(USE_GENERATIONAL_GC)
static void dispatchRememberSet(HeapManager *mng, size_t heap_size, AllocationNode *block)
{
    NodeHeader *head;
    Segment *seg = mng->segmentList;
    bitmap_t *map = (bitmap_t *)do_malloc(heap_size / (MIN_ALIGN) / sizeof(bitmap_t));
    ARRAY_add(BitMapPtr,  &mng->remember_sets, map);
    while(seg) {
        head = (NodeHeader *) block;
        head->remember_set = map;
        seg->remember_set  = map;
        seg = seg->next;
        map += SEGMENT_SIZE / MIN_ALIGN / BITS;
        block++;
    }
}
#endif

static void SegmentPool_dispose(Segment *pool, size_t size)
{
    Segment *seg = NULL;
    size_t i;

    for (i = 0; i < size; i++) {
        seg = pool + i;
        if(seg->base[0]) {
            DeleteBitMap(seg->base[0], seg->heap_klass);
        }
    }
    do_free(pool, sizeof(Segment) * size);
}

static void HeapManager_ExpandHeap(HeapManager *mng, size_t list_size)
{
    Segment *segment_pool;

    size_t heap_size = list_size * SEGMENT_SIZE;
    void *managed_heap, *managed_heap_end;
    managed_heap     = call_malloc_aligned(heap_size, SEGMENT_SIZE);
    if(managed_heap == NULL) {
        THROW_OutOfMemory(heap_size);
    }
    managed_heap_end = (char *)managed_heap + heap_size;
    do_bzero(managed_heap, heap_size);
#if defined(GCDEBUG) && defined(GCSTAT)
    global_gc_stat.managed_heap = (AllocationNode *) managed_heap;
    global_gc_stat.managed_heap_end = (AllocationNode *) managed_heap_end;
#endif

    segment_pool = SegmentPool_Init(list_size, (AllocationNode *) managed_heap);
    mng->segmentList  = segment_pool;

#if defined(USE_GENERATIONAL_GC)
    dispatchRememberSet(mng, heap_size, (AllocationNode *) managed_heap);
#endif

    ARRAY_add(size_t,  &mng->heap_size_a, heap_size);
    ARRAY_add(VoidPtr, &mng->managed_heap_a    , managed_heap);
    ARRAY_add(VoidPtr, &mng->managed_heap_end_a, managed_heap_end);

    ARRAY_add(SegmentPtr, &mng->segment_pool_a, segment_pool);
    ARRAY_add(size_t, &mng->segment_size_a, list_size);
#ifdef GCSTAT
    gc_stat("Expand Heap(%uMB)[%p, %p]", (int)heap_size/MB_,
            managed_heap, managed_heap_end);
#endif
}

static HeapManager *HeapManager_init(size_t list_size)
{
    size_t i;
    SubHeap *h;

    HeapManager *mng = (HeapManager *) do_malloc(sizeof(*mng));
    mng->flags = 0;
    ARRAY_init(size_t,  &mng->heap_size_a, 1);
    ARRAY_init(VoidPtr, &mng->managed_heap_a, 1);
    ARRAY_init(VoidPtr, &mng->managed_heap_end_a, 1);
    ARRAY_init(SegmentPtr, &mng->segment_pool_a, 1);
    ARRAY_init(size_t, &mng->segment_size_a, 1);
#if defined(USE_GENERATIONAL_GC)
    ARRAY_init(BitMapPtr, &mng->remember_sets, 1);
#endif

    HeapManager_ExpandHeap(mng, list_size);
    for_each_heap(h, i, mng->heaps) {
        Heap_Init(mng, (mng->heaps+i), i);
    }
    ARRAY_init(GCRoot, &mng->roots, 1);
    return mng;
}

static void HeapManager_delete(HeapManager *mng)
{
    size_t i;
    Segment **x;
    void **p;
    SubHeap *h;

    if(mng->mstack.capacity > 0) {
        MarkStack mstack = mng->mstack;
        do_free(mstack.stack,  (mstack.capacity + 1) * sizeof(kObject *));
        mng->mstack.stack    = NULL;
        mng->mstack.capacity = 0;
    }

    HeapManager_final_free(mng);

    for_each_heap(h, i, mng->heaps) {
        Heap_dispose(mng->heaps+i);
    }

    FOR_EACH_ARRAY_(mng->segment_pool_a, x, i) {
        size_t *size = ARRAY_n(mng->segment_size_a, i);
        SegmentPool_dispose(*x, *size);
    }
    ARRAY_dispose(SegmentPtr, &mng->segment_pool_a);
    ARRAY_dispose(size_t,     &mng->segment_size_a);

    FOR_EACH_ARRAY_(mng->managed_heap_a, p, i) {
        size_t *size = ARRAY_n(mng->heap_size_a, i);
        call_free_aligned(*p, *size);
    }
    ARRAY_dispose(size_t,  &mng->heap_size_a);
    ARRAY_dispose(VoidPtr, &mng->managed_heap_a);
    ARRAY_dispose(VoidPtr, &mng->managed_heap_end_a);
    ARRAY_dispose(GCRoot, &mng->roots);
    do_free(mng, sizeof(*mng));
}

static SubHeap *findSubHeapBySize(HeapManager *mng, size_t n)
{
    unsigned klass = SizeToKlass(n);
    assert(n <= SUBHEAP_KLASS_SIZE_MAX);
    assert(n != 0);
    return &(mng->heaps)[klass];
}

#if GCDEBUG
static bool DBG_CHECK_OBJECT_IN_SEGMENT(kObject *o, Segment *seg)
{
    kObject *s = (kObject *) seg->managed_heap;
    kObject *e = (kObject *) seg->managed_heap_end;
    return (s < o && o < e);
}

static bool DBG_CHECK_OBJECT_IN_HEAP(kObject *o, SubHeap *h)
{
    Segment *seg = h->p.seg;
    return (DBG_CHECK_OBJECT_IN_SEGMENT(o, seg));
}
#else
#define DBG_CHECK_OBJECT_IN_SEGMENT(o, seg) true
#define DBG_CHECK_OBJECT_IN_HEAP(o, h) true
#endif

static void deferred_sweep(HeapManager *mng, kObject *o)
{
#ifdef GC_USE_DEFERREDSWEEP
#if GCSTAT
    NodeHeader *head = (NodeHeader *) (((uintptr_t)o) & ~(SEGMENT_SIZE - 1UL));
    global_gc_stat.collected[head->klass] += 1;
#endif
    bmgc_Object_free(o);
#else
    // assert(kObject_class(o) == NULL);
#endif
}

#define minorGC(mng) bitmapMarkingGC(mng, GC_MAJOR)
#define majorGC(mng) bitmapMarkingGC(mng, GC_MINOR)

static kObject *bm_malloc_internal(HeapManager *mng, size_t n)
{
    kObject *temp = NULL;
    SubHeap *h;
    assert(n != 0);
#if GCDEBUG
    global_gc_stat.current_request_size = n;
#endif
    if(n > SUBHEAP_KLASS_SIZE_MAX) {
        // is it really okay? (kimio)
        char *ptr = (char *) do_malloc(n+sizeof(NodeHeader));
        return (kObject *) (ptr + sizeof(NodeHeader));
    }
    h = findSubHeapBySize(mng, n);
    temp = (kObject *)tryAlloc(mng, h);

    if(temp != NULL)
        goto L_finaly;
#ifdef USE_SAFEPOINT_POLICY
#ifdef USE_GENERATIONAL_GC
    bitmap_set(&mng->flags, GC_MAJOR_FLAG, 1);
#endif
    HeapManager_ExpandHeap(mng, SUBHEAP_DEFAULT_SEGPOOL_SIZE*2);
    newSegment(mng, h);
#else
#ifdef USE_GENERATIONAL_GC
    minorGC(mng);
    temp = tryAlloc(mng, h);
#endif
    if(temp != NULL)
        goto L_finaly;
    majorGC(mng);
#endif /* defined(USE_SAFEPOINT_POLICY) */
    temp = (kObject *)tryAlloc(mng, h);
    if(temp == NULL) {
        THROW_OutOfMemory(n);
        assert(0);
    }
L_finaly:;
         assert(DBG_CHECK_OBJECT_IN_HEAP(temp, h));
#if GCDEBUG
         global_gc_stat.total_object++;
         global_gc_stat.object_count[h->heap_klass]++;
#endif
         deferred_sweep(mng, temp);
         return temp;
}

#ifdef GCDEBUG
static void dumpBM(uintptr_t bm)
{
    int i;
    uintptr_t mask;
    fprintf(stderr, "                 ");
    for (i = BITS-1; i >= 0; i--) {
        if(i %10 == 0) {
            fprintf(stderr, "%d", i / 10);
        } else {
            fprintf(stderr, " ");
        }
    }
    fprintf(stderr, "\n");
    fprintf(stderr, "%16" PREFIX_x , bm);
    for (i = BITS-1; i >= 0; i--) {
        fprintf(stderr, "%d", i % 10);
    }
    fprintf(stderr, "\n                ");
    for (mask = 1UL << (BITS-1); mask; mask >>= 1) {
        fprintf(stderr, "%d", (bm & mask)?1:0);
    }
    fprintf(stderr, "\n");
}
#endif

static void clearAllBitMapsAndCount(HeapManager *mng, SubHeap *h)
{
    size_t i;
    for (i = 0; i < h->seglist_size; i++) {
        Segment *seg = h->seglist[i];
        ClearBitMap(seg->base[0], h->heap_klass);
        BITMAP_SET_LIMIT(seg->base[0], h->heap_klass);
        gc_info("klass=%d, seg[%" PREFIX_d "]=%p count=%d",
                seg->heap_klass, i, seg, seg->live_count);
        seg->live_count = 0;
    }
}

#ifdef USE_GENERATIONAL_GC
#define LOAD_SNAPSHOT(seg)\
    do_memcpy(seg->base[0], seg->snapshots[0], BM_SIZE[seg->heap_klass])

#define SAVE_SNAPSHOT(seg)\
    do_memcpy(seg->snapshots[0], seg->base[0], BM_SIZE[seg->heap_klass])

#define LOAD_LIVECOUNT(seg) seg->live_count = seg->tenure_live_count
#define SAVE_LIVECOUNT(seg) seg->tenure_live_count = seg->live_count

static void setTenureBitMapsAndCount(HeapManager *mng, SubHeap *h)
{
    size_t i;
    for (i = 0; i < h->seglist_size; i++) {
        Segment *seg = h->seglist[i];
        ClearBitMap(seg->base[0], h->heap_klass);
        LOAD_SNAPSHOT(seg);
        LOAD_LIVECOUNT(seg);
        BITMAP_SET_LIMIT_AND_COPY_BM(seg->base[0], seg->snapshots[0], h->heap_klass);
        gc_info("klass=%d, seg[%lu]=%p count=%d",
                seg->heap_klass, i, seg, seg->live_count);
    }
}
#endif

#define NEXT_MASK(bm, mask) do {\
    uintptr_t temp;\
    temp = bm | (mask - 1UL);\
    mask = (temp + 1UL) & ~temp;\
    BM_SET(bm, mask);\
} while(0)

static kObject *indexToAddr(Segment *seg, uintptr_t idx, uintptr_t mask)
{
    const NodePtr *ptr = seg->block;
    size_t size = seg->heap_klass;
    size_t n = idx * BITS + FFS(mask) - 1;
    size_t offset = n << size;
    return (kObject *)((char *)ptr+offset);
}

static void b0_final_sweep(bitmap_t bm, size_t idx, Segment *seg)
{
    kObject *o;
    bitmap_t mask = 1;
    NEXT_MASK(bm, mask);
    while(mask) {
        o = indexToAddr(seg, idx, mask);
#if GCDEBUG
        // global_gc_stat.collected[seg->heap_klass] += 1;
#endif
        bmgc_Object_free(o);
        NEXT_MASK(bm, mask);
    }
}

static void HeapManager_final_free(HeapManager *mng)
{
    size_t j;
    SubHeap *h;
    for_each_heap(h, j, mng->heaps) {
        size_t i;
        clearAllBitMapsAndCount(mng, h);
        for (i = 0; i < h->seglist_size; i++) {
            Segment *seg = h->seglist[i];
            bitmap_t *bm0;
            bitmap_t *b0 = (bitmap_t *) seg->base[0];
            bitmap_t *l0 = b0 + SegmentBitMapCount[j];
            for (bm0 = b0; bm0 < l0; ++bm0) {
                b0_final_sweep(*bm0, bm0 - b0, seg);
            }
        }
        gc_info("heap[%d] collected %" PREFIX_d,
                h->heap_klass,
                global_gc_stat.collected[h->heap_klass]);
    }
}

#if GCDEBUG
static void Heap_dump(const SubHeap *h)
{
    gc_info("klass[%2d] object_count=%u segment_list=(%" PREFIX_d ") ",
            h->heap_klass, global_gc_stat.object_count[h->heap_klass],
            h->seglist_size);
}

#endif /* GCDEBUG */

#ifdef GCDEBUG
static void BMGC_dump(HeapManager *mng)
{
    size_t i;
    gc_info("********************************");
    gc_info("* Heap Information");
    gc_info("********************************");
    gc_info("total allocated object count=%" PREFIX_d, global_gc_stat.total_object);
    for (i = SUBHEAP_KLASS_MIN; i <= SUBHEAP_KLASS_MAX; i++) {
        SubHeap *h = mng->heaps + i;
        Heap_dump(h);
    }
    gc_info("\n");
}

static bool DBG_CHECK_BITMAP(Segment *seg, bitmap_t *bm)
{
    bitmap_t *b0 = (bitmap_t *)seg->base[0];
    bitmap_t *l0 = (bitmap_t *)seg->base[0] + SegmentBitMapCount[seg->heap_klass];
    return (b0 <= bm && bm <= l0);
}
#else
static void BMGC_dump(HeapManager *info) {}
#define DBG_CHECK_BITMAP(seg, bm) true
#endif

static void bmgc_gc_Init(HeapManager *mng, enum gc_mode mode)
{
    size_t i;
    SubHeap *h;
#ifdef USE_GENERATIONAL_GC
    void (*finit_bitmap) (HeapManager *mng, SubHeap *h);
    finit_bitmap = (mode & GC_MAJOR) ? clearAllBitMapsAndCount : setTenureBitMapsAndCount;
#endif
    BMGC_dump(mng);
    for_each_heap(h, i, mng->heaps) {
#ifdef USE_GENERATIONAL_GC
        finit_bitmap(mng, h);
#else
        clearAllBitMapsAndCount(mng, h);
#endif
    }
}

#define OBJECT_LOAD_BLOCK_INFO(o, seg, index, klass) do {\
    uintptr_t addr, offset;\
    NodeHeader *head;\
    addr   = ((uintptr_t)o) & ~(SEGMENT_SIZE - 1UL);\
    offset = ((uintptr_t)o) &  (SEGMENT_SIZE - 1UL);\
    head = (NodeHeader *) addr;\
    seg   = head->seg;\
    klass = head->klass;\
    index = offset >> klass;\
} while(0)

static void bitmap_mark(bitmap_t bm, Segment *seg, uintptr_t idx, uintptr_t mask)
{
    if(BM_IS_FULL(bm)) {
        size_t i;
        for (i = 1; i < SEGMENT_LEVEL-1; ++i) {
            uintptr_t bpidx, bpmask;
            bitmap_t *bm1;
            BITPTR_INIT_(bpidx, bpmask, idx);
            bm1 = SEG_BITMAP_N(seg, i, bpidx);
            BM_SET(*bm1, bpmask);
            if(!BM_IS_FULL(*bm1))
                break;
            idx /= BITS;
        }
    }
}

static void mark_mstack(HeapManager *mng, kObject *o, MarkStack *mstack)
{
    Segment *seg;
    int index, klass;
    uintptr_t bpidx, bpmask;
    bitmap_t *bm;
    OBJECT_LOAD_BLOCK_INFO(o, seg, index, klass);
    BITPTR_INIT_(bpidx, bpmask, index);
    bm  = SEG_BITMAP_N(seg, 0, bpidx);
    prefetch_(SEG_BITMAP_N(seg, 1, 0), 1, 1);

    assert(DBG_CHECK_OBJECT_IN_SEGMENT(o, seg));
    assert(DBG_CHECK_BITMAP(seg, bm));
    if(!BM_TEST(*bm, bpmask)) {
        BM_SET(*bm, bpmask);
#ifdef USE_GENERATIONAL_GC
        Object_SetTenure(((kObject *)o));
#endif
        bitmap_mark(*bm, seg, bpidx, bpmask);
        ++(seg->live_count);
        mstack_push(mstack, o);
#ifdef GCSTAT
        global_gc_stat.marked[klass]++;
#endif
    }
}

struct GCVisitor {
    NodeVisitor base;
    HeapManager  *mng;
    MarkStack    *mstack;
};

static void NodeVisitor_visit(NodeVisitor *visitor, kObject *object)
{
    struct GCVisitor *gv = (struct GCVisitor *)visitor;
    mark_mstack(gv->mng, object, gv->mstack);
}

static void NodeVisitor_visitRange(NodeVisitor *visitor, kObject **begin, kObject **end)
{
    struct GCVisitor *gv = (struct GCVisitor *)visitor;
    kObject **itr;
    for (itr = begin; itr != end; ++itr) {
        mark_mstack(gv->mng, *itr, gv->mstack);
    }
}

#if defined(USE_GENERATIONAL_GC)
static void RememberSet_Add(kObject *o)
{
    uintptr_t addr   = ((uintptr_t)o & ~(SEGMENT_SIZE - 1UL));
    uintptr_t offset = ((uintptr_t)o &  (SEGMENT_SIZE - 1UL)) >> SUBHEAP_KLASS_MIN;
    NodeHeader *head = (NodeHeader *) addr;
    bitmap_t *map = head->remember_set;
#ifdef DEBUG_WRITE_BARRIER
    int ret = bitmap_get(map+(offset/BITS), offset%BITS);
    if(ret == 0 && Object_isTenure(o)) {
        fprintf(stderr, "W %p\n", o);
    }
#endif

#ifdef USE_GENERATIONAL_GC
    bitmap_set(map+(offset/BITS), offset%BITS, Object_isTenure(o));
#else
    Segment *seg;
    int index, klass;
    uintptr_t bpidx, bpmask;
    OBJECT_LOAD_BLOCK_INFO(o, seg, index, klass);
    BITPTR_INIT_(bpidx, bpmask, index);
    bitmap_t *bm  = SEG_TRACE_BITMAP_N(seg, 0, bpidx);
    bitmap_set(map+(offset/BITS), offset%BITS, !BM_TEST(*bm, bpmask));
#endif
}
#endif

#ifdef USE_GENERATIONAL_GC
static void Kwrite_barrier(kObject *parent)
{
    RememberSet_Add(parent);
}

static void KupdateObjectField(kObject *parent, kObject *oldValPtr, kObject *newVal)
{
    Kwrite_barrier(parent);
}
#endif

#if defined(USE_GENERATIONAL_GC)
static void RememberSet_Reftrace(HeapManager *mng, KObjectVisitor *visitor)
{
    size_t i;
    bitmap_t *x;
    FOR_EACH_ARRAY_(mng->remember_sets, i) {
        uintptr_t base_Address = (uintptr_t) ARRAY_n(mng->managed_heap_a, i);
        size_t bitmap_size = ARRAY_n(mng->heap_size_a, i) / (MIN_ALIGN * BITS);
        bitmap_t *base = x;
        bitmap_t *m = base;
        bitmap_t *e = m + bitmap_size;
        for (; m != e; base+=BITS, base_Address +=
#if SIZEOF_VOIDP*8 == 64
                (SEGMENT_SIZE)
#else
                (SEGMENT_SIZE/sizeof(void *))
#endif
            ) {
            for (; m < base + BITS; ++m) {
                bitmap_t b = (*m);
                while(b != 0) {
                    uintptr_t index, offset;
                    index = CTZ(b);
                    b ^= 1UL << index;
                    offset = (m - base) * BITS + index;
                    kObject *o = (kObject *)(base_Address + (offset << SUBHEAP_KLASS_MIN));
#ifdef DEBUG_WRITE_BARRIER
                    fprintf(stderr, "R %p\n", o);
#endif
                    KLIB kObjectProto_Reftrace(o, visitor);
                }
                bitmap_reset(m, 0);
            }
        }
    }
}

#endif

static void NodeManager_mark_root(HeapManager *mng, NodeVisitor *visitor);
static void Node_reftrace(Node o, NodeVisitor *visitor);

static void bmgc_gc_mark_root(HeapManager *mng, NodeVisitor *visitor)
{
    NodeManager_mark_root(mng, visitor);
}

static void bmgc_gc_mark(HeapManager *mng, enum gc_mode mode)
{
    MarkStack *mstack = mstack_init(&mng->mstack);
    kObject *ref = NULL;
    struct GCVisitor visitor = {};
    visitor.base.fn_visit = NodeVisitor_visit;
    visitor.base.fn_visit_range = NodeVisitor_visitRange;
    visitor.mng    = mng;
    visitor.mstack = mstack;

    bmgc_gc_mark_root(mng, &visitor.base);
#ifdef USE_GENERATIONAL_GC
    if(mode & GC_MINOR) {
        RememberSet_Reftrace(mng, &visitor.base);
    }
#endif
    ref = mstack_next(mstack);
    if(unlikely(ref == 0))
        return;
    do {
        Node_reftrace(ref, &visitor.base);
    } while((ref = mstack_next(mstack)) != NULL);
}

#define LIST_PUSH(tail, e) do {\
    *tail = e;\
    tail  = &e->next;\
} while(0)

static void rearrangeSegList(SubHeap *h, unsigned klass, bitmap_t *checkFull)
{
    size_t i, count_dead = 0;
    Segment *unfilled = NULL, **unfilled_tail = &unfilled;

    if(h->seglist_size < 1)
        return;
    for (i = 0; i < h->seglist_size; i++) {
        Segment *seg = h->seglist[i];
        size_t dead = SegmentNodeCount[klass] - seg->live_count;
        count_dead += dead;
        if(dead > 0)
            LIST_PUSH(unfilled_tail, seg);
#ifdef USE_GENERATIONAL_GC
        SAVE_SNAPSHOT(seg);
        SAVE_LIVECOUNT(seg);
#endif
    }
    *unfilled_tail = NULL;
    h->freelist = unfilled;
    fetchSegment(h, klass);
    bitmap_set(checkFull, klass,
            (count_dead < SegmentNodeCount[klass] && h->freelist == NULL));
}

static void bmgc_gc_sweep(HeapManager *mng)
{
    bitmap_t checkFull = 0;
    size_t i, j;
    SubHeap *h;

    for_each_heap(h, j, mng->heaps) {
#ifndef GC_USE_DEFERREDSWEEP
        for (i = 0; i < h->seglist_size; i++) {
            Segment *seg = h->seglist[i];
            bitmap_t *bm0;
            bitmap_t *b0 = (bitmap_t *) seg->base[0];
            bitmap_t *l0 = b0 + SegmentBitMapCount[j];
            for (bm0 = b0; bm0 < l0; ++bm0) {
                b0_final_sweep(*bm0, bm0 - b0, seg);
            }
        }
        gc_info("heap[%d] collected %lu",
                h->heap_klass,
                global_gc_stat.collected[h->heap_klass]);
#endif
        rearrangeSegList(h, j, &checkFull);
    }

    if(checkFull) {
#ifdef USE_GENERATIONAL_GC
        bitmap_set(&mng->flags, GC_MAJOR_FLAG, 1);
#endif
        HeapManager_ExpandHeap(mng, SUBHEAP_DEFAULT_SEGPOOL_SIZE*2);
        for_each_heap(h, i, mng->heaps) {
            if(bitmap_get(&checkFull, i))
                newSegment(mng, h);
        }
    }
}

static void bitmapMarkingGC(HeapManager *mng, enum gc_mode mode)
{
    gc_info("GC starting");
    bitmap_reset(&mng->flags, 0);
    bmgc_gc_Init(mng, mode);
#ifdef GCSTAT
    size_t i = 0, marked = 0, collected = 0, heap_size = 0;
    for (i = 0; i < ARRAY_size(mng->heap_size_a); i++) {
        heap_size += ARRAY_n(mng->heap_size_a, i);
    }
#endif
    bmgc_gc_mark(mng, mode);

    bmgc_gc_sweep(mng);

#ifdef GCSTAT
    SubHeap *h;
    for_each_heap(h, i, mng->heaps) {
        marked    += global_gc_stat.marked[i];
        collected += global_gc_stat.collected[i];
        global_gc_stat.marked[i]    = 0;
        global_gc_stat.collected[i] = 0;
    }
    global_gc_stat.gc_count += 1;
    gc_stat("%sGC(%" PREFIX_d ") HeapSize=%" PREFIX_d" MB, last_collected=%" PREFIX_d", marked=%"PREFIX_d,
            (mode & GC_MAJOR)?"major":"minor",
            global_gc_stat.gc_count, (heap_size/MB_), collected, marked);
#endif
}

/* ------------------------------------------------------------------------ */

static inline void bmgc_Object_free(kObject *o)
{
    unsigned len = Node_length(o);
    if (len > MOZVM_SMALL_ARRAY_LIMIT) {
        ARRAY_dispose(Node, &o->entry.array);
    }
    do_bzero((void *)o, sizeof(kObject));
#ifdef GCSTAT
    {
        Segment *seg;
        uintptr_t klass, index;
        OBJECT_LOAD_BLOCK_INFO(o, seg, index, klass);
        global_gc_stat.collected[seg->heap_klass] += 1;
    }
#endif
}

static HeapManager *g_manager = NULL;

void NodeManager_init()
{
    size_t default_size = SUBHEAP_DEFAULT_SEGPOOL_SIZE;
 #ifdef GCSTAT
	// global_gc_stat.fp = fopen("KONOHA_BMGC_INFO", "a");
#endif
   g_manager = HeapManager_init(default_size);
}

void NodeManager_dispose()
{
    HeapManager_delete(g_manager);
    g_manager = NULL;
#ifdef GCSTAT
	// fclose(global_gc_stat.fp);
#endif
}

void NodeManager_reset()
{
    NodeManager_dispose();
    NodeManager_init();
}

void NodeManager_print_stats()
{
}

void NodeManager_add_gc_root(void *ptr, f_trace f)
{
    HeapManager *mng = g_manager;
    GCRoot root;
    root.ptr = ptr;
    root.trace = f;
    ARRAY_add(GCRoot, &mng->roots, &root);
}

static void NodeManager_mark_root(HeapManager *mng, NodeVisitor *visitor)
{
    GCRoot *x, *e;
    FOR_EACH_ARRAY(mng->roots, x, e) {
        x->trace(x->ptr, visitor);
    }
}

static void Node_reftrace(Node o, NodeVisitor *visitor)
{
    unsigned len = Node_length(o);
    Node *begin, *end;
    assert(MOZVM_SMALL_ARRAY_LIMIT == 2);
    if (len <= MOZVM_SMALL_ARRAY_LIMIT) {
        begin = o->entry.raw.ary;
        end   = o->entry.raw.ary + len;
    }
    else {
        begin = ARRAY_BEGIN(o->entry.array);
        end   = ARRAY_END(o->entry.array);
    }
    visitor->fn_visit_range(visitor, begin, end);
}

static kObject *node_alloc()
{
    size_t size = sizeof(kObject);
    HeapManager *mng = g_manager;
    kObject *o = (kObject *)bm_malloc_internal(mng, size);
    // memset(o, 0, sizeof(*o));
    NODE_GC_INIT(o);
    return o;
}

/* ------------------------------------------------------------------------ */

#ifdef __cplusplus
}
#endif
