#ifndef __ospli_osplo__tglib__
#define __ospli_osplo__tglib__

#include <stddef.h>
#include <dds_dcps.h>

struct tgtype;

struct tgtopic_key {
  char *name; /* field name */
  size_t off; /* from start of data */
  const struct tgtype *type; /* aliases tgtopic::type */
};

struct tgtopic {
  char *name;
  size_t size;
  struct tgtype *type;
  unsigned nkeys;
  struct tgtopic_key *keys;
};

enum tgprint_mode {
  TGPM_DENSE,
  TGPM_SPACE,
  TGPM_FIELDS,
  TGPM_MULTILINE
};

struct tgstring {
  char *buf;
  size_t pos;
  size_t size;
  size_t chop;
  int chopped;
};

void tgstring_init(struct tgstring *s, size_t chop);
void tgstring_fini(struct tgstring *s);

struct tgtopic *tgnew(DDS_Topic tp, int printtype);
void tgfree(struct tgtopic *tp);
int tgprint(struct tgstring *s, const struct tgtopic *tp, const void *data, enum tgprint_mode mode);
int tgprintkey(struct tgstring *s, const struct tgtopic *tp, const void *keydata, enum tgprint_mode mode);

void *tgscan(const struct tgtopic *tp, const char *src, char **endp);
void tgfreedata(const struct tgtopic *tp, void *data);

#endif /* defined(__ospli_osplo__tglib__) */
