#include "types.h"

#ifndef __FDT__

#define __FDT__

#define FDT_MAGIC      0xD00DFEEDU
#define FDT_BEGIN_NODE 0x00000001
#define FDT_END_NODE   0x00000002
#define FDT_PROP       0x00000003
#define FDT_NOP        0x00000004
#define FDT_END        0x00000009

struct fdt_header {
  uint32 magic;
  uint32 totalsize;
  uint32 off_dt_struct;
  uint32 off_dt_strings;
  uint32 off_mem_rsvmap;
  uint32 version;
  uint32 last_comp_version;
  uint32 boot_cpuid_phys;
  uint32 size_dt_strings;
  uint32 size_dt_struct;
};

struct fdt_reserve_entry {
  uint64 address;
  uint64 size;
};

struct fdt_property {
  uint32 len;
  uint32 nameoff;
};

static inline uint32
fdt32_to_cpu(uint32 x)
{
  return ((x & 0x000000ff) << 24) | ((x & 0x0000ff00) << 8) |
         ((x & 0x00ff0000) >> 8) | ((x & 0xff000000) >> 24);
}

static inline uint64
fdt64_to_cpu(uint64 x)
{
  return ((uint64)fdt32_to_cpu((uint32)(x & 0xffffffff)) << 32) |
         (uint64)fdt32_to_cpu((uint32)(x >> 32));
}

#define MATCH32b(VAL, CONST) ((VAL) == (fdt32_to_cpu(CONST)))
#define MATCH64b(VAL, CONST) ((VAL) == (fdt64_to_cpu(CONST)))

typedef enum {
  FDT_OK = 0,
  FDT_ERR_BAD_MAGIC,
  FDT_ERR_TRUNCATED,
  FDT_ERR_UNEXPECTED_TOKEN,
  FDT_ERR_NODE_NOT_FOUND,
  FDT_ERR_PROP_NOT_FOUND
} fdt_status_t;

struct fdt_result {
  fdt_status_t status;
  uint64 value;
};

struct fdt_property_list {
  char *name;
  char *value;
  uint32 len;
  struct fdt_property_list *next;
};

struct fdt_node {
  char *name;
  struct fdt_property_list *props;
  struct fdt_node *children;
  struct fdt_node *next;
};

#endif // __FDT__
