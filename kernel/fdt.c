#include "fdt.h"
#include "kernel/types.h"

#define MAX_PROPS    256
#define MAX_CHILDREN 128

struct fdt_property_list props[MAX_PROPS];
struct fdt_node nodes[MAX_CHILDREN];

static int next_node = 0, next_prop = 0;

// static uint64 mem_base, mem_size, stringbase, fdt_endpoint;
static uint64 mem_size, stringbase, fdt_endpoint;

static struct fdt_result
skip_nop_tokens(uint64 pos)
{
  uint64 curr_loc = pos;
  uint32 tok;

  for (;;) {
    if (curr_loc + sizeof(uint32) > fdt_endpoint)
      return (struct fdt_result){.status = FDT_ERR_TRUNCATED, .value = 0};

    tok = fdt32_to_cpu(*(uint32 *)curr_loc);
    if (tok != FDT_NOP)
      return (struct fdt_result){.status = FDT_OK, .value = curr_loc};

    curr_loc += sizeof(uint32);
  }
}

static struct fdt_result
read_name(uint64 pos)
{
  uint64 start = pos;
  int len = 0;

  for (;;) {
    if (pos + sizeof(char) > fdt_endpoint)
      return (struct fdt_result){.status = FDT_ERR_TRUNCATED, .value = 0};

    char c = *(char *)pos;
    len++;
    if (c == '\0')
      break;

    pos += sizeof(char);
  }

  int roundedlen = (len + 3) & ~3;
  return (struct fdt_result){.status = FDT_OK, .value = start + roundedlen};
}

static struct fdt_result
parse_prop(uint64 pos, struct fdt_property_list *proplist)
{
  uint32 tok = fdt32_to_cpu(*(uint32 *)pos);
  if (tok != FDT_PROP)
    return (struct fdt_result){.status = FDT_ERR_UNEXPECTED_TOKEN, .value = 0};

  uint64 datapos = pos + sizeof(uint32);
  struct fdt_property *prop = (struct fdt_property *)datapos;
  uint64 curr_loc = datapos + sizeof(struct fdt_property);
  proplist->name = (char *)(stringbase + fdt32_to_cpu(prop->nameoff));
  proplist->value = (char *)curr_loc;
  proplist->len = fdt32_to_cpu(prop->len);

  uint32 val_len = proplist->len;
  uint64 aligned_len = (val_len + 3) & ~3;
  uint64 next_pos = curr_loc + aligned_len;

  return (struct fdt_result){.status = FDT_OK, .value = next_pos};
}

/*
 * For now, I'm just going to visit all the nodes until I find the memory node.
 * After that I might take a look through the reserved memory so I can work out the reserved parts of memory
 */
static struct fdt_result
parse_node(uint64 pos, struct fdt_node *node)
{
  uint32 tok = fdt32_to_cpu(*(uint32 *)pos);
  if (tok != FDT_BEGIN_NODE)
    return (struct fdt_result){.status = FDT_ERR_UNEXPECTED_TOKEN, .value = 0};

  uint64 name_pos = pos + sizeof(uint32);
  node->name = (char *)name_pos;

  struct fdt_result name_res = read_name(name_pos);
  if (name_res.status != FDT_OK)
    return name_res;

  struct fdt_result skip = skip_nop_tokens(name_res.value);
  if (skip.status != FDT_OK)
    return skip;

  uint64 curr_loc = skip.value;
  struct fdt_property_list *tail = 0;
  struct fdt_node *child_tail = 0;

  for (;;) {
    tok = fdt32_to_cpu(*(uint32 *)curr_loc);
    if (tok != FDT_PROP)
      break;

    if (next_prop >= MAX_PROPS)
      return (struct fdt_result){.status = FDT_ERR_TRUNCATED, .value = 0};

    struct fdt_property_list *current_prop = &props[next_prop++];
    struct fdt_result parse_res = parse_prop(curr_loc, current_prop);
    if (parse_res.status != FDT_OK)
      return parse_res;
    current_prop->next = 0;

    if (tail == 0)
      node->props = current_prop;
    else
      tail->next = current_prop;
    tail = current_prop;

    skip = skip_nop_tokens(parse_res.value);
    if (skip.status != FDT_OK)
      return skip;
    curr_loc = skip.value;
  }

  for (;;) {
    tok = fdt32_to_cpu(*(uint32 *)curr_loc);
    if (tok != FDT_BEGIN_NODE)
      break;

    if (next_node >= MAX_CHILDREN)
      return (struct fdt_result){.status = FDT_ERR_TRUNCATED, .value = 0};

    struct fdt_node *child = &nodes[next_node++];
    *child = (struct fdt_node){.props = 0, .name = 0, .next = 0};

    struct fdt_result parse_res = parse_node(curr_loc, child);
    if (parse_res.status != FDT_OK)
      return parse_res;

    if (child_tail == 0)
      node->children = child;
    else
      child_tail->next = child;
    child_tail = child;

    skip = skip_nop_tokens(parse_res.value);
    if (skip.status != FDT_OK)
      return skip;
    curr_loc = skip.value;
  }

  skip = skip_nop_tokens(curr_loc);
  if (skip.status != FDT_OK)
    return skip;
  curr_loc = skip.value;

  tok = fdt32_to_cpu(*(uint32 *)curr_loc);
  if (tok != FDT_END_NODE)
    return (struct fdt_result){.status = FDT_ERR_UNEXPECTED_TOKEN, .value = 0};

  curr_loc += sizeof(uint32);
  return (struct fdt_result){.status = FDT_OK, .value = curr_loc};
}

struct fdt_result
parse_fdt(uint64 dtb)
{
  struct fdt_header *fdt_h = (struct fdt_header *)dtb;
  if (!MATCH32b(fdt_h->magic, FDT_MAGIC))
    return (struct fdt_result){.status = FDT_ERR_BAD_MAGIC, .value = 0};

  fdt_endpoint = (uint64)fdt_h + fdt32_to_cpu(fdt_h->totalsize);
  stringbase = (uint64)fdt_h + fdt32_to_cpu(fdt_h->off_dt_strings);

  uint64 struct_loc = dtb + fdt32_to_cpu(fdt_h->off_dt_struct);
  struct fdt_result res = skip_nop_tokens(struct_loc);
  if (res.status != FDT_OK)
    return res;

  struct fdt_node *root = &nodes[next_node++];
  *root = (struct fdt_node){.props = 0, .name = 0, .children = 0};
  res = parse_node(res.value, root);
  if (res.status != FDT_OK)
    return res;

  struct fdt_result skip = skip_nop_tokens(res.value);
  if (skip.status != FDT_OK)
    return skip;

  if (fdt32_to_cpu(*(uint32 *)skip.value) != FDT_END)
    return (struct fdt_result){.status = FDT_ERR_UNEXPECTED_TOKEN, .value = 0};

  return (struct fdt_result){.status = FDT_OK, .value = 1};
}

uint64
fdt_get_memory_size(void)
{
  return mem_size;
}

struct fdt_node
fdt_get_root_node(void)
{
  return nodes[0];
}

const char *
fdt_status_str(fdt_status_t s)
{
  switch (s) {
  case FDT_OK:
    return "ok";
  case FDT_ERR_BAD_MAGIC:
    return "bad magic number";
  case FDT_ERR_TRUNCATED:
    return "unexpected end of block";
  case FDT_ERR_UNEXPECTED_TOKEN:
    return "unexpected token";
  case FDT_ERR_NODE_NOT_FOUND:
    return "node not found";
  case FDT_ERR_PROP_NOT_FOUND:
    return "property not found";
  default:
    return "unknown error";
  }
}
