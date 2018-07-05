/* Copyright 2017 PrismTech Limited

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License. */
#include <stdio.h>
#include <ctype.h>
#include <stdint.h>
#include <inttypes.h>

#include <dds_dcps.h>

#include "common.h"
#include "tglib.h"
#include "porting.h"

static const char *mdParticipantBuiltinTopicData = "<MetaData version=\"1.0.0\"><Module name=\"DDS\"><TypeDef name=\"BuiltinTopicKey_t\"><Array size=\"3\"><Long/></Array></TypeDef><TypeDef name=\"octSeq\"><Sequence><Octet/></Sequence></TypeDef><Struct name=\"UserDataQosPolicy\"><Member name=\"value\"><Type name=\"octSeq\"/></Member></Struct><Struct name=\"ParticipantBuiltinTopicData\"><Member name=\"key\"><Type name=\"BuiltinTopicKey_t\"/></Member><Member name=\"user_data\"><Type name=\"UserDataQosPolicy\"/></Member></Struct></Module></MetaData>";
static const char *mdTopicBuiltinTopicData = "<MetaData version=\"1.0.0\"><Module name=\"DDS\"><TypeDef name=\"BuiltinTopicKey_t\"><Array size=\"3\"><Long/></Array></TypeDef><Enum name=\"DurabilityQosPolicyKind\"><Element name=\"VOLATILE_DURABILITY_QOS\" value=\"0\"/><Element name=\"TRANSIENT_LOCAL_DURABILITY_QOS\" value=\"1\"/><Element name=\"TRANSIENT_DURABILITY_QOS\" value=\"2\"/><Element name=\"PERSISTENT_DURABILITY_QOS\" value=\"3\"/></Enum><Struct name=\"Duration_t\"><Member name=\"sec\"><Long/></Member><Member name=\"nanosec\"><ULong/></Member></Struct><Enum name=\"HistoryQosPolicyKind\"><Element name=\"KEEP_LAST_HISTORY_QOS\" value=\"0\"/><Element name=\"KEEP_ALL_HISTORY_QOS\" value=\"1\"/></Enum><Enum name=\"LivelinessQosPolicyKind\"><Element name=\"AUTOMATIC_LIVELINESS_QOS\" value=\"0\"/><Element name=\"MANUAL_BY_PARTICIPANT_LIVELINESS_QOS\" value=\"1\"/><Element name=\"MANUAL_BY_TOPIC_LIVELINESS_QOS\" value=\"2\"/></Enum><Enum name=\"ReliabilityQosPolicyKind\"><Element name=\"BEST_EFFORT_RELIABILITY_QOS\" value=\"0\"/><Element name=\"RELIABLE_RELIABILITY_QOS\" value=\"1\"/></Enum><Struct name=\"TransportPriorityQosPolicy\"><Member name=\"value\"><Long/></Member></Struct><Enum name=\"DestinationOrderQosPolicyKind\"><Element name=\"BY_RECEPTION_TIMESTAMP_DESTINATIONORDER_QOS\" value=\"0\"/><Element name=\"BY_SOURCE_TIMESTAMP_DESTINATIONORDER_QOS\" value=\"1\"/></Enum><Struct name=\"ResourceLimitsQosPolicy\"><Member name=\"max_samples\"><Long/></Member><Member name=\"max_instances\"><Long/></Member><Member name=\"max_samples_per_instance\"><Long/></Member></Struct><Enum name=\"OwnershipQosPolicyKind\"><Element name=\"SHARED_OWNERSHIP_QOS\" value=\"0\"/><Element name=\"EXCLUSIVE_OWNERSHIP_QOS\" value=\"1\"/></Enum><TypeDef name=\"octSeq\"><Sequence><Octet/></Sequence></TypeDef><Struct name=\"DurabilityQosPolicy\"><Member name=\"kind\"><Type name=\"DurabilityQosPolicyKind\"/></Member></Struct><Struct name=\"LifespanQosPolicy\"><Member name=\"duration\"><Type name=\"Duration_t\"/></Member></Struct><Struct name=\"LatencyBudgetQosPolicy\"><Member name=\"duration\"><Type name=\"Duration_t\"/></Member></Struct><Struct name=\"DeadlineQosPolicy\"><Member name=\"period\"><Type name=\"Duration_t\"/></Member></Struct><Struct name=\"HistoryQosPolicy\"><Member name=\"kind\"><Type name=\"HistoryQosPolicyKind\"/></Member><Member name=\"depth\"><Long/></Member></Struct><Struct name=\"DurabilityServiceQosPolicy\"><Member name=\"service_cleanup_delay\"><Type name=\"Duration_t\"/></Member><Member name=\"history_kind\"><Type name=\"HistoryQosPolicyKind\"/></Member><Member name=\"history_depth\"><Long/></Member><Member name=\"max_samples\"><Long/></Member><Member name=\"max_instances\"><Long/></Member><Member name=\"max_samples_per_instance\"><Long/></Member></Struct><Struct name=\"LivelinessQosPolicy\"><Member name=\"kind\"><Type name=\"LivelinessQosPolicyKind\"/></Member><Member name=\"lease_duration\"><Type name=\"Duration_t\"/></Member></Struct><Struct name=\"ReliabilityQosPolicy\"><Member name=\"kind\"><Type name=\"ReliabilityQosPolicyKind\"/></Member><Member name=\"max_blocking_time\"><Type name=\"Duration_t\"/></Member><Member name=\"synchronous\"><Boolean/></Member></Struct><Struct name=\"DestinationOrderQosPolicy\"><Member name=\"kind\"><Type name=\"DestinationOrderQosPolicyKind\"/></Member></Struct><Struct name=\"OwnershipQosPolicy\"><Member name=\"kind\"><Type name=\"OwnershipQosPolicyKind\"/></Member></Struct><Struct name=\"TopicDataQosPolicy\"><Member name=\"value\"><Type name=\"octSeq\"/></Member></Struct><Struct name=\"TopicBuiltinTopicData\"><Member name=\"key\"><Type name=\"BuiltinTopicKey_t\"/></Member><Member name=\"name\"><String/></Member><Member name=\"type_name\"><String/></Member><Member name=\"durability\"><Type name=\"DurabilityQosPolicy\"/></Member><Member name=\"durability_service\"><Type name=\"DurabilityServiceQosPolicy\"/></Member><Member name=\"deadline\"><Type name=\"DeadlineQosPolicy\"/></Member><Member name=\"latency_budget\"><Type name=\"LatencyBudgetQosPolicy\"/></Member><Member name=\"liveliness\"><Type name=\"LivelinessQosPolicy\"/></Member><Member name=\"reliability\"><Type name=\"ReliabilityQosPolicy\"/></Member><Member name=\"transport_priority\"><Type name=\"TransportPriorityQosPolicy\"/></Member><Member name=\"lifespan\"><Type name=\"LifespanQosPolicy\"/></Member><Member name=\"destination_order\"><Type name=\"DestinationOrderQosPolicy\"/></Member><Member name=\"history\"><Type name=\"HistoryQosPolicy\"/></Member><Member name=\"resource_limits\"><Type name=\"ResourceLimitsQosPolicy\"/></Member><Member name=\"ownership\"><Type name=\"OwnershipQosPolicy\"/></Member><Member name=\"topic_data\"><Type name=\"TopicDataQosPolicy\"/></Member></Struct></Module></MetaData>";
static const char *mdPublicationBuiltinTopicData = "<MetaData version=\"1.0.0\"><Module name=\"DDS\"><TypeDef name=\"BuiltinTopicKey_t\"><Array size=\"3\"><Long/></Array></TypeDef><Enum name=\"DurabilityQosPolicyKind\"><Element name=\"VOLATILE_DURABILITY_QOS\" value=\"0\"/><Element name=\"TRANSIENT_LOCAL_DURABILITY_QOS\" value=\"1\"/><Element name=\"TRANSIENT_DURABILITY_QOS\" value=\"2\"/><Element name=\"PERSISTENT_DURABILITY_QOS\" value=\"3\"/></Enum><Struct name=\"Duration_t\"><Member name=\"sec\"><Long/></Member><Member name=\"nanosec\"><ULong/></Member></Struct><Enum name=\"LivelinessQosPolicyKind\"><Element name=\"AUTOMATIC_LIVELINESS_QOS\" value=\"0\"/><Element name=\"MANUAL_BY_PARTICIPANT_LIVELINESS_QOS\" value=\"1\"/><Element name=\"MANUAL_BY_TOPIC_LIVELINESS_QOS\" value=\"2\"/></Enum><Enum name=\"ReliabilityQosPolicyKind\"><Element name=\"BEST_EFFORT_RELIABILITY_QOS\" value=\"0\"/><Element name=\"RELIABLE_RELIABILITY_QOS\" value=\"1\"/></Enum><Enum name=\"DestinationOrderQosPolicyKind\"><Element name=\"BY_RECEPTION_TIMESTAMP_DESTINATIONORDER_QOS\" value=\"0\"/><Element name=\"BY_SOURCE_TIMESTAMP_DESTINATIONORDER_QOS\" value=\"1\"/></Enum><TypeDef name=\"octSeq\"><Sequence><Octet/></Sequence></TypeDef><Enum name=\"OwnershipQosPolicyKind\"><Element name=\"SHARED_OWNERSHIP_QOS\" value=\"0\"/><Element name=\"EXCLUSIVE_OWNERSHIP_QOS\" value=\"1\"/></Enum><Struct name=\"OwnershipStrengthQosPolicy\"><Member name=\"value\"><Long/></Member></Struct><Enum name=\"PresentationQosPolicyAccessScopeKind\"><Element name=\"INSTANCE_PRESENTATION_QOS\" value=\"0\"/><Element name=\"TOPIC_PRESENTATION_QOS\" value=\"1\"/><Element name=\"GROUP_PRESENTATION_QOS\" value=\"2\"/></Enum><TypeDef name=\"StringSeq\"><Sequence><String/></Sequence></TypeDef><Struct name=\"DurabilityQosPolicy\"><Member name=\"kind\"><Type name=\"DurabilityQosPolicyKind\"/></Member></Struct><Struct name=\"LifespanQosPolicy\"><Member name=\"duration\"><Type name=\"Duration_t\"/></Member></Struct><Struct name=\"LatencyBudgetQosPolicy\"><Member name=\"duration\"><Type name=\"Duration_t\"/></Member></Struct><Struct name=\"DeadlineQosPolicy\"><Member name=\"period\"><Type name=\"Duration_t\"/></Member></Struct><Struct name=\"LivelinessQosPolicy\"><Member name=\"kind\"><Type name=\"LivelinessQosPolicyKind\"/></Member><Member name=\"lease_duration\"><Type name=\"Duration_t\"/></Member></Struct><Struct name=\"ReliabilityQosPolicy\"><Member name=\"kind\"><Type name=\"ReliabilityQosPolicyKind\"/></Member><Member name=\"max_blocking_time\"><Type name=\"Duration_t\"/></Member><Member name=\"synchronous\"><Boolean/></Member></Struct><Struct name=\"DestinationOrderQosPolicy\"><Member name=\"kind\"><Type name=\"DestinationOrderQosPolicyKind\"/></Member></Struct><Struct name=\"GroupDataQosPolicy\"><Member name=\"value\"><Type name=\"octSeq\"/></Member></Struct><Struct name=\"TopicDataQosPolicy\"><Member name=\"value\"><Type name=\"octSeq\"/></Member></Struct><Struct name=\"UserDataQosPolicy\"><Member name=\"value\"><Type name=\"octSeq\"/></Member></Struct><Struct name=\"OwnershipQosPolicy\"><Member name=\"kind\"><Type name=\"OwnershipQosPolicyKind\"/></Member></Struct><Struct name=\"PresentationQosPolicy\"><Member name=\"access_scope\"><Type name=\"PresentationQosPolicyAccessScopeKind\"/></Member><Member name=\"coherent_access\"><Boolean/></Member><Member name=\"ordered_access\"><Boolean/></Member></Struct><Struct name=\"PartitionQosPolicy\"><Member name=\"name\"><Type name=\"StringSeq\"/></Member></Struct><Struct name=\"PublicationBuiltinTopicData\"><Member name=\"key\"><Type name=\"BuiltinTopicKey_t\"/></Member><Member name=\"participant_key\"><Type name=\"BuiltinTopicKey_t\"/></Member><Member name=\"topic_name\"><String/></Member><Member name=\"type_name\"><String/></Member><Member name=\"durability\"><Type name=\"DurabilityQosPolicy\"/></Member><Member name=\"deadline\"><Type name=\"DeadlineQosPolicy\"/></Member><Member name=\"latency_budget\"><Type name=\"LatencyBudgetQosPolicy\"/></Member><Member name=\"liveliness\"><Type name=\"LivelinessQosPolicy\"/></Member><Member name=\"reliability\"><Type name=\"ReliabilityQosPolicy\"/></Member><Member name=\"lifespan\"><Type name=\"LifespanQosPolicy\"/></Member><Member name=\"destination_order\"><Type name=\"DestinationOrderQosPolicy\"/></Member><Member name=\"user_data\"><Type name=\"UserDataQosPolicy\"/></Member><Member name=\"ownership\"><Type name=\"OwnershipQosPolicy\"/></Member><Member name=\"ownership_strength\"><Type name=\"OwnershipStrengthQosPolicy\"/></Member><Member name=\"presentation\"><Type name=\"PresentationQosPolicy\"/></Member><Member name=\"partition\"><Type name=\"PartitionQosPolicy\"/></Member><Member name=\"topic_data\"><Type name=\"TopicDataQosPolicy\"/></Member><Member name=\"group_data\"><Type name=\"GroupDataQosPolicy\"/></Member></Struct></Module></MetaData>";
static const char *mdSubscriptionBuiltinTopicData = "<MetaData version=\"1.0.0\"><Module name=\"DDS\"><TypeDef name=\"BuiltinTopicKey_t\"><Array size=\"3\"><Long/></Array></TypeDef><Enum name=\"DurabilityQosPolicyKind\"><Element name=\"VOLATILE_DURABILITY_QOS\" value=\"0\"/><Element name=\"TRANSIENT_LOCAL_DURABILITY_QOS\" value=\"1\"/><Element name=\"TRANSIENT_DURABILITY_QOS\" value=\"2\"/><Element name=\"PERSISTENT_DURABILITY_QOS\" value=\"3\"/></Enum><Struct name=\"Duration_t\"><Member name=\"sec\"><Long/></Member><Member name=\"nanosec\"><ULong/></Member></Struct><Enum name=\"LivelinessQosPolicyKind\"><Element name=\"AUTOMATIC_LIVELINESS_QOS\" value=\"0\"/><Element name=\"MANUAL_BY_PARTICIPANT_LIVELINESS_QOS\" value=\"1\"/><Element name=\"MANUAL_BY_TOPIC_LIVELINESS_QOS\" value=\"2\"/></Enum><Enum name=\"ReliabilityQosPolicyKind\"><Element name=\"BEST_EFFORT_RELIABILITY_QOS\" value=\"0\"/><Element name=\"RELIABLE_RELIABILITY_QOS\" value=\"1\"/></Enum><Enum name=\"OwnershipQosPolicyKind\"><Element name=\"SHARED_OWNERSHIP_QOS\" value=\"0\"/><Element name=\"EXCLUSIVE_OWNERSHIP_QOS\" value=\"1\"/></Enum><Enum name=\"DestinationOrderQosPolicyKind\"><Element name=\"BY_RECEPTION_TIMESTAMP_DESTINATIONORDER_QOS\" value=\"0\"/><Element name=\"BY_SOURCE_TIMESTAMP_DESTINATIONORDER_QOS\" value=\"1\"/></Enum><TypeDef name=\"octSeq\"><Sequence><Octet/></Sequence></TypeDef><Enum name=\"PresentationQosPolicyAccessScopeKind\"><Element name=\"INSTANCE_PRESENTATION_QOS\" value=\"0\"/><Element name=\"TOPIC_PRESENTATION_QOS\" value=\"1\"/><Element name=\"GROUP_PRESENTATION_QOS\" value=\"2\"/></Enum><TypeDef name=\"StringSeq\"><Sequence><String/></Sequence></TypeDef><Struct name=\"DurabilityQosPolicy\"><Member name=\"kind\"><Type name=\"DurabilityQosPolicyKind\"/></Member></Struct><Struct name=\"TimeBasedFilterQosPolicy\"><Member name=\"minimum_separation\"><Type name=\"Duration_t\"/></Member></Struct><Struct name=\"LatencyBudgetQosPolicy\"><Member name=\"duration\"><Type name=\"Duration_t\"/></Member></Struct><Struct name=\"DeadlineQosPolicy\"><Member name=\"period\"><Type name=\"Duration_t\"/></Member></Struct><Struct name=\"LivelinessQosPolicy\"><Member name=\"kind\"><Type name=\"LivelinessQosPolicyKind\"/></Member><Member name=\"lease_duration\"><Type name=\"Duration_t\"/></Member></Struct><Struct name=\"ReliabilityQosPolicy\"><Member name=\"kind\"><Type name=\"ReliabilityQosPolicyKind\"/></Member><Member name=\"max_blocking_time\"><Type name=\"Duration_t\"/></Member><Member name=\"synchronous\"><Boolean/></Member></Struct><Struct name=\"OwnershipQosPolicy\"><Member name=\"kind\"><Type name=\"OwnershipQosPolicyKind\"/></Member></Struct><Struct name=\"DestinationOrderQosPolicy\"><Member name=\"kind\"><Type name=\"DestinationOrderQosPolicyKind\"/></Member></Struct><Struct name=\"GroupDataQosPolicy\"><Member name=\"value\"><Type name=\"octSeq\"/></Member></Struct><Struct name=\"TopicDataQosPolicy\"><Member name=\"value\"><Type name=\"octSeq\"/></Member></Struct><Struct name=\"UserDataQosPolicy\"><Member name=\"value\"><Type name=\"octSeq\"/></Member></Struct><Struct name=\"PresentationQosPolicy\"><Member name=\"access_scope\"><Type name=\"PresentationQosPolicyAccessScopeKind\"/></Member><Member name=\"coherent_access\"><Boolean/></Member><Member name=\"ordered_access\"><Boolean/></Member></Struct><Struct name=\"PartitionQosPolicy\"><Member name=\"name\"><Type name=\"StringSeq\"/></Member></Struct><Struct name=\"SubscriptionBuiltinTopicData\"><Member name=\"key\"><Type name=\"BuiltinTopicKey_t\"/></Member><Member name=\"participant_key\"><Type name=\"BuiltinTopicKey_t\"/></Member><Member name=\"topic_name\"><String/></Member><Member name=\"type_name\"><String/></Member><Member name=\"durability\"><Type name=\"DurabilityQosPolicy\"/></Member><Member name=\"deadline\"><Type name=\"DeadlineQosPolicy\"/></Member><Member name=\"latency_budget\"><Type name=\"LatencyBudgetQosPolicy\"/></Member><Member name=\"liveliness\"><Type name=\"LivelinessQosPolicy\"/></Member><Member name=\"reliability\"><Type name=\"ReliabilityQosPolicy\"/></Member><Member name=\"ownership\"><Type name=\"OwnershipQosPolicy\"/></Member><Member name=\"destination_order\"><Type name=\"DestinationOrderQosPolicy\"/></Member><Member name=\"user_data\"><Type name=\"UserDataQosPolicy\"/></Member><Member name=\"time_based_filter\"><Type name=\"TimeBasedFilterQosPolicy\"/></Member><Member name=\"presentation\"><Type name=\"PresentationQosPolicy\"/></Member><Member name=\"partition\"><Type name=\"PartitionQosPolicy\"/></Member><Member name=\"topic_data\"><Type name=\"TopicDataQosPolicy\"/></Member><Member name=\"group_data\"><Type name=\"GroupDataQosPolicy\"/></Member></Struct></Module></MetaData>";

union tokenval {
  int64_t i;
  uint64_t ui;
  char ch;
  double d;
  char *str;
};

enum tokenkind {
  TOK_ERROR,
  TOK_EOF,
  TOK_INT,
  TOK_FLOAT,
  TOK_CHAR,
  TOK_STRING,
  TOK_SYMBOL,

  TOK_COMMA,
  TOK_DOT,
  TOK_EQUALS,
  TOK_LBRACE,
  TOK_RBRACE,
  TOK_COLON,
  TOK_LBRACKET,
  TOK_RBRACKET,

  TOK_LITERAL
};

struct token {
  const char *src;
  enum tokenkind kind;
  union tokenval val;
};

#define TOKEN_INIT(src) { (src), TOK_ERROR, { .i = 0 } }

struct lexer {
  int error;
  int have_token;
  struct token token;
  const char *srcstart;
  const char *src;
};

enum tgkind {
  TG_BOOLEAN,
  TG_CHAR,
  TG_INT,
  TG_UINT,
  TG_FLOAT,
  TG_ENUM,
  TG_TIME,
  TG_STRING,
  TG_SEQUENCE,
  TG_ARRAY,
  TG_STRUCT,
  TG_UNION,
  TG_TYPEDEF
};

struct tgtype {
  char *name; /* or null if anonymous */
  size_t size; /* size in memory, also discriminates between widths of prim types */
  size_t align;
  enum tgkind kind;
  union tgtype_u {
    struct tgtype_td {
      struct tgtype *type;
    } td;
    struct tgtype_S {
      unsigned n;
      struct tgtype_Sms {
        size_t off;
        char *name;
        struct tgtype *type;
      } *ms;
    } S;
    struct tgtype_str {
      unsigned maxn;
    } str;
    struct tgtype_seq {
      unsigned maxn;
      struct tgtype *type;
    } seq;
    struct tgtype_ary {
      unsigned n;
      struct tgtype *type;
    } ary;
    struct tgtype_e {
      unsigned n;
      struct tgtype_ems {
        int v;
        char *name;
      } *ms;
    } e;
    struct tgtype_U {
      struct tgtype *dtype;
      size_t off;
      unsigned n;
      int msidxdef;
      struct tgtype_Ums {
        char *name;
        struct tgtype *type;
      } *ms;
      unsigned nlab;
      struct tgtype_lab {
        uint64_t val;
        int msidx;
      } *labs;
    } U;
  } u;
};

struct dictnode {
  struct dictnode *next;
  const char *name; /* aliases type->name */
  struct tgtype *type;
};

struct parse_context {
  struct dictnode *dict;
  char *nameprefix;
  int depth;
};

struct parse_arg {
  struct parse_context *context;
  struct tgtype *type;
};

#if PRE_V6_5
typedef DDS_sequence_octet dds_seq_t;
#else
typedef struct DDS_sequence_s dds_seq_t;
#endif

static void init_lexer(struct token *tok, struct lexer *l, const char *src);
static enum tokenkind scantoken(struct token *tok, struct lexer *l);
static void freetoken(struct token *tok);
static int scanerror(struct token *tok, struct lexer *l, const char *fmt, ...);
static int casttoint(struct token *tok, struct lexer *l);

struct alignof_int16_t { char c; int16_t x; };
struct alignof_int32_t { char c; int32_t x; };
struct alignof_int64_t { char c; int64_t x; };
struct alignof_float   { char c; float x; };
struct alignof_double  { char c; double x; };
struct alignof_ptr     { char c; void *x; };
struct alignof_dds_seq { char c; dds_seq_t x; };
#define alignof(t) offsetof (struct alignof_##t, x)

static const char *typekindstr(DDS_TypeElementKind kind)
{
  switch (kind)
  {
    case DDS_TYPE_ELEMENT_KIND_MODULE: return "module";
    case DDS_TYPE_ELEMENT_KIND_STRUCT: return "struct";
    case DDS_TYPE_ELEMENT_KIND_MEMBER: return "member";
    case DDS_TYPE_ELEMENT_KIND_UNION: return "union";
    case DDS_TYPE_ELEMENT_KIND_UNIONCASE: return "unioncase";
    case DDS_TYPE_ELEMENT_KIND_UNIONSWITCH: return "unionswitch";
    case DDS_TYPE_ELEMENT_KIND_UNIONLABEL: return "unionlabel";
    case DDS_TYPE_ELEMENT_KIND_TYPEDEF: return "typedef";
    case DDS_TYPE_ELEMENT_KIND_ENUM: return "enum";
    case DDS_TYPE_ELEMENT_KIND_ENUMLABEL: return "enumlabel";
    case DDS_TYPE_ELEMENT_KIND_TYPE: return "type";
    case DDS_TYPE_ELEMENT_KIND_ARRAY: return "array";
    case DDS_TYPE_ELEMENT_KIND_SEQUENCE: return "sequence";
    case DDS_TYPE_ELEMENT_KIND_STRING: return "string";
    case DDS_TYPE_ELEMENT_KIND_CHAR: return "char";
    case DDS_TYPE_ELEMENT_KIND_BOOLEAN: return "boolean";
    case DDS_TYPE_ELEMENT_KIND_OCTET: return "octet";
    case DDS_TYPE_ELEMENT_KIND_SHORT: return "short";
    case DDS_TYPE_ELEMENT_KIND_USHORT: return "unsigned short";
    case DDS_TYPE_ELEMENT_KIND_LONG: return "long";
    case DDS_TYPE_ELEMENT_KIND_ULONG: return "unsigned long";
    case DDS_TYPE_ELEMENT_KIND_LONGLONG: return "long long";
    case DDS_TYPE_ELEMENT_KIND_ULONGLONG: return "unsigned long long";
    case DDS_TYPE_ELEMENT_KIND_FLOAT: return "float";
    case DDS_TYPE_ELEMENT_KIND_DOUBLE: return "double";
    case DDS_TYPE_ELEMENT_KIND_TIME: return "time";
    case DDS_TYPE_ELEMENT_KIND_UNIONLABELDEFAULT: return "unionlabeldefault";
  }
  return NULL;
}

const char *tgkindstr(enum tgkind kind)
{
  switch (kind)
  {
    case TG_BOOLEAN: return "boolean";
    case TG_CHAR: return "char";
    case TG_INT: return "int";
    case TG_UINT: return "uint";
    case TG_FLOAT: return "float";
    case TG_ENUM: return "enum";
    case TG_TIME: return "time";
    case TG_STRING: return "string";
    case TG_SEQUENCE: return "sequence";
    case TG_ARRAY: return "array";
    case TG_STRUCT: return "struct";
    case TG_UNION: return "union";
    case TG_TYPEDEF: return "typedef";
  }
  return "?";
}

static char *fullname(struct parse_arg *arg, const char *name)
{
  if (name == NULL)
    return NULL;
  else
  {
    size_t sz = strlen(arg->context->nameprefix) + 2 + strlen(name) + 1;
    char *x = malloc(sz);
    (void) snprintf(x, sz, "%s::%s", arg->context->nameprefix, name);
    return x;
  }
}

static struct tgtype *lookupname1(struct dictnode *dict, const char *name)
{
  struct dictnode *n = dict;
  while (n && strcmp(n->name, name) != 0)
    n = n->next;
  return n ? n->type : NULL;
}

static struct tgtype *lookupname(struct parse_arg *arg, const char *name)
{
  if (strncmp(name, "::", 2) == 0)
    return lookupname1(arg->context->dict, name);
  else
  {
    char *prefix = strdup(arg->context->nameprefix);
    size_t sz = strlen(prefix) + 2 + strlen(name) + 1;
    char *tmp = malloc(sz);
    struct tgtype *t;
    (void) snprintf(tmp, sz, "%s::%s", prefix, name);
    while((t = lookupname1(arg->context->dict, tmp)) == NULL && *prefix)
    {
      char *x = strrchr(prefix, ':');
      assert(x > prefix && x[-1] == ':');
      x[-1] = 0;
      (void) snprintf(tmp, sz, "%s::%s", prefix, name);
    }
    free(prefix);
    free(tmp);
    return t;
  }
}

static void addtodict(struct parse_arg *arg, const char *name, struct tgtype *t)
{
  if (name)
  {
    struct dictnode *n;
    assert(t->name == NULL);
    t->name = strdup(name);
    n = malloc(sizeof(*n));
    n->name = fullname(arg, name);
    n->type = t;
    n->next = arg->context->dict;
    arg->context->dict = n;
  }
}

static void pushmodule(struct parse_arg *arg, const char *name)
{
  size_t sz = strlen(arg->context->nameprefix) + 2 + strlen(name) + 1;
  char *np = malloc(sz);
  (void) snprintf(np, sz, "%s::%s", arg->context->nameprefix, name);
  free(arg->context->nameprefix);
  arg->context->nameprefix = np;
}

static void popmodule(struct parse_arg *arg)
{
  char *x = strrchr(arg->context->nameprefix, ':');
  assert(x > arg->context->nameprefix && x[-1] == ':');
  x[-1] = 0;
}

static DDS_boolean parse_type_cb(DDS_TypeElementKind kind, const DDS_string name, const DDS_TypeAttributeSeq *attrs, DDS_TypeParserHandle handle __attribute__ ((unused)), void *varg);

static struct tgtype *newtgtype(enum tgkind kind, size_t sz /* 0 if still to be computed */, size_t align)
{
  struct tgtype *t = malloc(sizeof (*t));
  memset(t, 0, sizeof (*t));
  t->kind = kind;
  t->size = sz;
  t->align = align;
  return t;
}

static size_t alignup (size_t pos, size_t align)
{
  assert((align & -align) == align);
  return (pos + align - 1) & -align;
}

static struct tgtype *detypedef (const struct tgtype *t)
{
  while (t->kind == TG_TYPEDEF)
    t = t->u.td.type;
  return (struct tgtype *) t;
}

static DDS_boolean parse_struct_cb(DDS_TypeElementKind kind, const DDS_string name, const DDS_TypeAttributeSeq *attrs __attribute__ ((unused)), DDS_TypeParserHandle handle, void *varg)
{
  struct parse_arg *arg = varg;
  struct parse_arg subarg = { .context = arg->context, .type = NULL };
  struct tgtype *t = arg->type;
  struct tgtype_S *ts = &t->u.S;
  assert(kind == DDS_TYPE_ELEMENT_KIND_MEMBER);
  ts->ms = realloc(ts->ms, ++ts->n * sizeof(*ts->ms));
  arg->context->depth++;
  (void) DDS_TypeSupport_walk_type_description(handle, parse_type_cb, &subarg);
  arg->context->depth--;
  assert(subarg.type != NULL);
  ts->ms[ts->n-1].off = alignup (t->size, subarg.type->align);
  ts->ms[ts->n-1].name = strdup(name);
  ts->ms[ts->n-1].type = subarg.type;
  t->size = ts->ms[ts->n-1].off + subarg.type->size;
  if (t->align < subarg.type->align)
    t->align = subarg.type->align;
  return 1;
}

static DDS_boolean parse_enum_cb(DDS_TypeElementKind kind, const DDS_string name, const DDS_TypeAttributeSeq *attrs, DDS_TypeParserHandle handle __attribute__ ((unused)), void *varg)
{
  struct parse_arg *arg = varg;
  assert(kind == DDS_TYPE_ELEMENT_KIND_ENUMLABEL);
  assert(attrs->_length == 1);
  assert(attrs->_buffer[0].value._d == DDS_TYPE_ATTRIBUTE_KIND_NUMBER);
  arg->type->u.e.ms = realloc(arg->type->u.e.ms, ++arg->type->u.e.n * sizeof(*arg->type->u.e.ms));
  arg->type->u.e.ms[arg->type->u.e.n-1].v = attrs->_buffer[0].value._u.nvalue;
  arg->type->u.e.ms[arg->type->u.e.n-1].name = strdup(name);
  return 1;
}

static DDS_boolean parse_unioncase_cb(DDS_TypeElementKind kind, const DDS_string name, const DDS_TypeAttributeSeq *attrs, DDS_TypeParserHandle handle, void *varg)
{
  struct parse_arg *arg = varg;
  struct tgtype *t = arg->type;
  struct tgtype_U *tu = &t->u.U;

  switch (kind)
  {
    case DDS_TYPE_ELEMENT_KIND_UNIONLABELDEFAULT:
      tu->msidxdef = (int) tu->n;
      break;

    case DDS_TYPE_ELEMENT_KIND_UNIONLABEL: {
      unsigned i, j;
      assert(tu->dtype != NULL); /* I think the discriminator type always is first, but I may be wrong ... */
      tu->labs = realloc(tu->labs, ((unsigned) tu->nlab + attrs->_length) * sizeof(*tu->labs));
      for (i = 0; i < attrs->_length; i++)
      {
        const DDS_TypeAttribute *a = &attrs->_buffer[i];
        switch (a->value._d)
        {
          case DDS_TYPE_ATTRIBUTE_KIND_NUMBER:
            tu->labs[tu->nlab].val = (uint64_t) a->value._u.nvalue;
            break;
          case DDS_TYPE_ATTRIBUTE_KIND_STRING:
            if(tu->dtype->kind == TG_ENUM) {
              for (j = 0; j < tu->dtype->u.e.n; j++)
                if (strcmp(a->value._u.svalue, tu->dtype->u.e.ms[j].name) == 0)
                  break;
              assert(j < tu->dtype->u.e.n);
              tu->labs[tu->nlab].val = (uint64_t) tu->dtype->u.e.ms[j].v;
            } else {
              assert(tu->dtype->kind == TG_INT || tu->dtype->kind == TG_UINT);
              tu->labs[tu->nlab].val = strtoull(a->value._u.svalue, NULL, 10);
            }
            break;
        }
        tu->labs[tu->nlab].msidx = (int) tu->n;
        tu->nlab++;
      }
      break;
    }

    default: {
      struct parse_arg subarg = { .context = arg->context, .type = NULL };
      (void) parse_type_cb(kind, name, attrs, handle, &subarg);
      assert(subarg.type != NULL);
      assert(tu->ms[tu->n].name != NULL);
      tu->ms[tu->n].type = subarg.type;
      t->align = alignup(t->align, subarg.type->align);
      tu->off = alignup(tu->off, subarg.type->align);
      if (t->size < alignup(tu->off + subarg.type->size, t->align))
        t->size = alignup(tu->off + subarg.type->size, t->align);
      break;
    }
  }
  return 1;
}

static DDS_boolean parse_union_cb(DDS_TypeElementKind kind, const DDS_string name __attribute__ ((unused)), const DDS_TypeAttributeSeq *attrs __attribute__ ((unused)), DDS_TypeParserHandle handle, void *varg)
{
  struct parse_arg *arg = varg;
  struct tgtype *t = arg->type;
  struct tgtype_U *tu = &t->u.U;

  switch (kind)
  {
    case DDS_TYPE_ELEMENT_KIND_UNIONSWITCH: {
      struct parse_arg subarg = { .context = arg->context, .type = NULL };
      assert(tu->dtype == NULL);
      arg->context->depth++;
      (void) DDS_TypeSupport_walk_type_description(handle, parse_type_cb, &subarg);
      arg->context->depth--;
      assert(subarg.type != NULL);
      tu->dtype = detypedef (subarg.type);
      t->size = tu->off = tu->dtype->size;
      t->align = tu->dtype->align;
      break;
    }

    case DDS_TYPE_ELEMENT_KIND_UNIONCASE: {
      tu->ms = realloc(tu->ms, (tu->n+1) * sizeof(*tu->ms));
      memset(&tu->ms[tu->n], 0, sizeof(*tu->ms));
      tu->ms[tu->n].name = strdup(name);
      arg->context->depth++;
      (void) DDS_TypeSupport_walk_type_description(handle, parse_unioncase_cb, arg);
      arg->context->depth--;
      assert(tu->ms[tu->n].type != NULL);
      tu->n++;
      break;
    }

    default:
      assert(0);
  }
  return 1;
}

static DDS_boolean print_cb(DDS_TypeElementKind kind, const DDS_string name, const DDS_TypeAttributeSeq *attrs, DDS_TypeParserHandle handle, void *varg)
{
  struct parse_arg *arg = varg;
  unsigned i;
  printf("%*.*s%s %s\n", 4*arg->context->depth, 4*arg->context->depth, " ", typekindstr(kind), name ? name : "(null)");
  for (i = 0; i < attrs->_length; i++)
  {
    const DDS_TypeAttribute *a = &attrs->_buffer[i];
    printf("%*.*s  %s=", 4*arg->context->depth, 4*arg->context->depth, " ", a->name);
    switch (a->value._d)
    {
      case DDS_TYPE_ATTRIBUTE_KIND_NUMBER:
        printf("%d\n", a->value._u.nvalue);
        break;
      case DDS_TYPE_ATTRIBUTE_KIND_STRING:
        printf("%s\n", a->value._u.svalue);
        break;
    }
  }
  arg->context->depth++;
  (void) DDS_TypeSupport_walk_type_description(handle, print_cb, arg);
  arg->context->depth--;
  return 1;
}

static unsigned intattr_w_0_default(const DDS_TypeAttributeSeq *attrs, const char *name)
{
  unsigned i;
  for (i = 0; i < attrs->_length; i++)
  {
    const DDS_TypeAttribute *a = &attrs->_buffer[i];
    if (strcmp(a->name, name) == 0)
    {
      assert(a->value._d == DDS_TYPE_ATTRIBUTE_KIND_NUMBER);
      assert(a->value._u.nvalue >= 0);
      return (unsigned) a->value._u.nvalue;
    }
  }
  return 0;
}

static DDS_boolean parse_type_cb(DDS_TypeElementKind kind, const DDS_string name, const DDS_TypeAttributeSeq *attrs, DDS_TypeParserHandle handle, void *varg)
{
  struct parse_arg *arg = varg;

  arg->context->depth++;
  switch (kind)
  {
    case DDS_TYPE_ELEMENT_KIND_MODULE:
    {
      struct parse_arg subarg = { .context = arg->context, .type = NULL };
      pushmodule(arg, name);
      (void) DDS_TypeSupport_walk_type_description(handle, parse_type_cb, &subarg);
      popmodule(arg);
      break;
    }

    case DDS_TYPE_ELEMENT_KIND_TYPEDEF:
    {
      struct parse_arg subarg = { .context = arg->context, .type = NULL };
      struct tgtype *t;
      (void) DDS_TypeSupport_walk_type_description(handle, parse_type_cb, &subarg);
      t = newtgtype(TG_TYPEDEF, subarg.type->size, subarg.type->align);
      t->u.td.type = subarg.type;
      if (name) addtodict(arg, name, t);
      break;
    }

    case DDS_TYPE_ELEMENT_KIND_TYPE:
      arg->type = lookupname(arg, name);
      assert(arg->type);
      break;

    case DDS_TYPE_ELEMENT_KIND_STRUCT:
    {
      struct tgtype *t = newtgtype(TG_STRUCT, 0, 0);
      struct parse_arg subarg = { .context = arg->context, .type = t };
      (void) DDS_TypeSupport_walk_type_description(handle, parse_struct_cb, &subarg);
      t->size = alignup(t->size, t->align);
      if (name) addtodict(arg, name, t);
      arg->type = t;
      break;
    }

    case DDS_TYPE_ELEMENT_KIND_UNION:
    {
      struct tgtype *t = newtgtype(TG_UNION, 0, 0);
      struct parse_arg subarg = { .context = arg->context, .type = t };
      t->u.U.msidxdef = -1;
      (void) DDS_TypeSupport_walk_type_description(handle, parse_union_cb, &subarg);
      t->size = alignup(t->size, t->align);
      if (name) addtodict(arg, name, t);
      arg->type = t;
      break;
    }

    case DDS_TYPE_ELEMENT_KIND_ARRAY:
    case DDS_TYPE_ELEMENT_KIND_SEQUENCE:
    {
      unsigned n = intattr_w_0_default(attrs, "size");
      int isseq = (kind == DDS_TYPE_ELEMENT_KIND_SEQUENCE || n == 0);
      struct tgtype *t = newtgtype(isseq ? TG_SEQUENCE : TG_ARRAY, 0, 0);
      struct parse_arg subarg = { .context = arg->context, .type = NULL };
      (void) DDS_TypeSupport_walk_type_description(handle, parse_type_cb, &subarg);
      assert(subarg.type);
      if (isseq) {
        t->u.seq.maxn = n;
        t->u.seq.type = subarg.type;
        t->size = sizeof (dds_seq_t);
        t->align = alignof (dds_seq);
      } else {
        t->u.ary.type = subarg.type;
        t->u.ary.n = n;
        t->size = n * subarg.type->size;
        t->align = subarg.type->align;
      }
      if (name) addtodict(arg, name, t);
      arg->type = t;
      break;
    }

    case DDS_TYPE_ELEMENT_KIND_ENUM:
    {
      struct tgtype *t = newtgtype(TG_ENUM, 4, alignof (int32_t));
      struct parse_arg subarg = { .context = arg->context, .type = t };
      (void) DDS_TypeSupport_walk_type_description(handle, parse_enum_cb, &subarg);
      if (name) addtodict(arg, name, t);
      arg->type = t;
      break;
    }

    case DDS_TYPE_ELEMENT_KIND_BOOLEAN:   arg->type = newtgtype(TG_BOOLEAN, 1, 1); break;
    case DDS_TYPE_ELEMENT_KIND_CHAR:      arg->type = newtgtype(TG_CHAR,    1, 1); break;
    case DDS_TYPE_ELEMENT_KIND_OCTET:     arg->type = newtgtype(TG_UINT,    1, 1); break;
    case DDS_TYPE_ELEMENT_KIND_SHORT:     arg->type = newtgtype(TG_INT,     2, alignof(int16_t)); break;
    case DDS_TYPE_ELEMENT_KIND_USHORT:    arg->type = newtgtype(TG_UINT,    2, alignof(int16_t)); break;
    case DDS_TYPE_ELEMENT_KIND_LONG:      arg->type = newtgtype(TG_INT,     4, alignof(int32_t)); break;
    case DDS_TYPE_ELEMENT_KIND_ULONG:     arg->type = newtgtype(TG_UINT,    4, alignof(int32_t)); break;
    case DDS_TYPE_ELEMENT_KIND_LONGLONG:  arg->type = newtgtype(TG_INT,     8, alignof(int64_t)); break;
    case DDS_TYPE_ELEMENT_KIND_ULONGLONG: arg->type = newtgtype(TG_UINT,    8, alignof(int64_t)); break;
    case DDS_TYPE_ELEMENT_KIND_FLOAT:     arg->type = newtgtype(TG_FLOAT,   4, alignof(float));   break;
    case DDS_TYPE_ELEMENT_KIND_DOUBLE:    arg->type = newtgtype(TG_FLOAT,   8, alignof(double));  break;
    case DDS_TYPE_ELEMENT_KIND_TIME:      arg->type = newtgtype(TG_TIME,    8, alignof(int32_t)); break;
    case DDS_TYPE_ELEMENT_KIND_STRING:    arg->type = newtgtype(TG_STRING,  sizeof(char *), alignof(ptr)); break;

    default:
      print_cb(kind, name, attrs, handle, arg);
      break;
  }
  arg->context->depth--;
  return 1;
}

static int lookupfield(const struct tgtype **ptype, size_t *poff, const struct tgtype *type, const char *name)
{
  struct lexer l;
  struct token tok;
  enum tokenkind tk;
  enum { SYMBOL = 1, DOTSYMBOL = 2, INDEX = 4 };
  unsigned allowed = SYMBOL;
  init_lexer(&tok, &l, name);
  *poff = 0;
  *ptype = detypedef(type);
  while ((tk = scantoken(&tok, &l)) != TOK_ERROR && tk != TOK_EOF && *ptype != NULL) {
    switch (tk) {
      case TOK_DOT:
        if (!(allowed & DOTSYMBOL))
          return scanerror(&tok, &l, "'.' unexpected");
        allowed = SYMBOL;
        break;
      case TOK_SYMBOL:
        if (!(allowed & SYMBOL))
          return scanerror(&tok, &l, "symbol unexpected");
        else if ((*ptype)->kind != TG_STRUCT)
          return scanerror(&tok, &l, "expected type to be a struct");
        else {
          const struct tgtype_S *ts = &(*ptype)->u.S;
          const unsigned n = ts->n;
          unsigned i;
          for (i = 0; i < n; i++) {
            if (strcmp(tok.val.str, ts->ms[i].name) == 0) {
              *poff += ts->ms[i].off;
              (*ptype) = detypedef(ts->ms[i].type);
              break;
            }
          }
          if (i == n) {
            return scanerror(&tok, &l, "field not found in struct");
          }
          allowed = DOTSYMBOL | INDEX;
        }
        break;
      case TOK_LBRACKET:
        if (!(allowed & INDEX))
          return scanerror(&tok, &l, "index unexpected");
        else if ((*ptype)->kind != TG_ARRAY)
          return scanerror(&tok, &l, "expected type to be an array");
        else {
          const struct tgtype_ary *ta = &(*ptype)->u.ary;
          (void) scantoken(&tok, &l);
          if (!casttoint(&tok, &l))
            return scanerror(&tok, &l, "integer expected");
          if (tok.val.ui > (unsigned) ta->n)
            return scanerror(&tok, &l, "index out of bounds");
          *poff += (size_t) tok.val.ui * ta->type->size;
          (*ptype) = detypedef(ta->type);
          if (scantoken(&tok, &l) != TOK_RBRACKET)
            return scanerror(&tok, &l, "']' expected");
          allowed = DOTSYMBOL | INDEX;
        }
        break;
      default:
        return scanerror(&tok, &l, "unexpected token");
    }
  }
  if (*ptype == NULL) {
    return scanerror(&tok, &l, "non-existent key");
  } else if (tk != TOK_EOF || allowed == SYMBOL) {
    return scanerror(&tok, &l, "junk at end of input");
  } else switch ((*ptype)->kind) {
    case TG_CHAR:
    case TG_BOOLEAN:
    case TG_INT:
    case TG_UINT:
    case TG_ENUM:
    case TG_STRING:
      break;
    default:
      return scanerror(&tok, &l, "not a valid key type");
  }
  freetoken(&tok);
  return 1;
}

static char *get_metadescription(DDS_Topic dds_tp, char **typename, char **keylist)
{
  char *md;
  *typename = DDS_Topic_get_type_name(dds_tp);
  if (strcmp(*typename, "kernelModule::v_participantInfo") == 0) {
    DDS_free(*typename);
    md = DDS_string_dup(mdParticipantBuiltinTopicData);
    *keylist = DDS_string_dup("key[1],key[0]");
    *typename = DDS_string_dup("DDS::ParticipantBuiltinTopicData");
  } else if(strcmp(*typename, "kernelModule::v_publicationInfo") == 0) {
    DDS_free(*typename);
    md = DDS_string_dup(mdPublicationBuiltinTopicData);
    *keylist = DDS_string_dup("key[1],key[0]");
    *typename = DDS_string_dup("DDS::PublicationBuiltinTopicData");
  } else if(strcmp(*typename, "kernelModule::v_subscriptionInfo") == 0) {
    DDS_free(*typename);
    md = DDS_string_dup(mdSubscriptionBuiltinTopicData);
    *keylist = DDS_string_dup("key[1],key[0]");
    *typename = DDS_string_dup("DDS::SubscriptionBuiltinTopicData");
  } else if(strcmp(*typename, "kernelModule::v_topicInfo") == 0) {
    DDS_free(*typename);
    md = DDS_string_dup(mdTopicBuiltinTopicData);
    *keylist = DDS_string_dup("key[1],key[0]");
    *typename = DDS_string_dup("DDS::TopicBuiltinTopicData");
  } else if(strcmp(*typename, "kernelModule::v_deliveryInfo") == 0) {
    /* workaround for a bug */
    md = DDS_Topic_get_metadescription(dds_tp);
    *keylist = DDS_string_dup("");
  } else {
    *keylist = DDS_Topic_get_keylist(dds_tp);
    md = DDS_Topic_get_metadescription(dds_tp);
  }
  return md;
}

struct tgtopic *tgnew(DDS_Topic dds_tp, int printtype)
{
  DDS_ReturnCode_t result;
  struct parse_context context = { .dict = NULL, .nameprefix = strdup(""), .depth = 0 };
  struct parse_arg arg = { .context = &context, .type = NULL };
  struct tgtopic *tp;
  char *topicname;
  char *typename;
  char *keylist;
  char *xs;

  topicname = DDS_Topic_get_name(dds_tp);
  xs = get_metadescription(dds_tp, &typename, &keylist);
  if (printtype && (result = DDS_TypeSupport_parse_type_description(xs, print_cb, &arg)) != DDS_RETCODE_OK)
    error("DDS_TypeSupport_parse_type_description: error %d (%s)\n", (int) result, dds_strerror(result));
  if ((result = DDS_TypeSupport_parse_type_description(xs, parse_type_cb, &arg)) != DDS_RETCODE_OK)
    error("DDS_TypeSupport_parse_type_description: error %d (%s)\n", (int) result, dds_strerror(result));
  DDS_free(xs);

  tp = malloc(sizeof(*tp));
  tp->name = strdup(topicname);
  if (arg.type)
    tp->type = arg.type;
  else
  {
    tp->type = lookupname(&arg, typename);
    if (tp->type == NULL)
      error("topic %s: can't find type %s\n", tp->name, typename);
  }
  tp->size = tp->type->size;

  if (*keylist == 0)
  {
    tp->nkeys = 0;
    tp->keys = NULL;
  }
  else
  {
    char *cursor, *key;
    unsigned i = 0, n = 0;
    cursor = keylist;
    while (cursor)
    {
      n++;
      cursor = strchr(cursor, ',');
      if (cursor) cursor++;
    }
    tp->nkeys = n;
    tp->keys = malloc(n * sizeof (*tp->keys));
    cursor = keylist;
    while ((key = strsep(&cursor, ",")) != NULL)
    {
      tp->keys[i].name = strdup(key);
      if (!lookupfield(&tp->keys[i].type, &tp->keys[i].off, tp->type, key))
        error("topic %s key %s not found\n", tp->name, key);
      i++;
    }
  }

  DDS_free(typename);
  DDS_free(topicname);
  DDS_free(keylist);

  while (context.dict)
  {
    struct dictnode *n = context.dict;
    context.dict = n->next;
    free(n);
  }
  free(context.nameprefix);
  return tp;
}

void tgfree(struct tgtopic *tp)
{
  unsigned i;
  for (i = 0; i < tp->nkeys; i++)
    free(tp->keys[i].name);
  /* FIXME: free tp->type */
  free(tp->name);
  free(tp->keys);
  free(tp);
}

static uint64_t loaddisc(const struct tgtype *t, const char *data)
{
  switch (t->kind) {
    case TG_BOOLEAN:
      return (uint64_t) *data;
    case TG_INT:
      switch (t->size) {
        case 1: return (uint8_t) (*(int8_t *)data);
        case 2: return (uint16_t) (*(int16_t *)data);
        case 4: return (uint32_t) (*(int32_t *)data);
        case 8: return (uint64_t) (*(int64_t *)data);
      }
      break;
    case TG_UINT:
      switch (t->size) {
        case 1: return *(uint8_t *)data;
        case 2: return *(uint16_t *)data;
        case 4: return *(uint32_t *)data;
        case 8: return *(uint64_t *)data;
      }
      break;
    case TG_ENUM:
      return *(uint32_t *)data;
    default:
      break;
  }
  assert(0);
  return 0;
}

static void storedisc(char *dst, const struct tgtype *t, uint64_t v)
{
  switch (t->kind) {
    case TG_BOOLEAN:
      *(char *)dst = (char) !!v;
      break;
    case TG_INT:
      switch (t->size) {
        case 1: *(int8_t *)dst = (int8_t) v; break;
        case 2: *(int16_t *)dst = (int16_t) v; break;
        case 4: *(int32_t *)dst = (int32_t) v; break;
        case 8: *(int64_t *)dst = (int64_t) v; break;
        default: assert(0);
      }
      break;
    case TG_UINT:
      switch (t->size) {
        case 1: *(uint8_t *)dst = (uint8_t) v; break;
        case 2: *(uint16_t *)dst = (uint16_t) v; break;
        case 4: *(uint32_t *)dst = (uint32_t) v; break;
        case 8: *(uint64_t *)dst = (uint64_t) v; break;
        default: assert(0);
      }
      break;
    case TG_ENUM:
      *(uint32_t *)dst = (uint32_t) v;
      break;
    default:
      assert(0);
  }
}

void tgstring_init(struct tgstring *s, size_t chop)
{
  s->size = 4096;
  s->buf = malloc(s->size);
  s->buf[0] = 0;
  s->pos = 0;
  s->chop = chop;
  s->chopped = 0;
}

void tgstring_fini(struct tgstring *s)
{
  free(s->buf);
  tgstring_init(s, s->chop);
}

static int tgprintf(struct tgstring *s, const char *fmt, ...) __attribute__ ((format (printf, 2, 3)));

static int tgprintf(struct tgstring *s, const char *fmt, ...)
{
  int n;
  va_list ap;
  if (s->chopped)
    return 0;
  assert (s->pos < s->size);
  va_start(ap, fmt);
  n = vsnprintf(s->buf + s->pos, s->size - s->pos, fmt, ap);
  va_end(ap);
  while (n >= 0 && (size_t)n >= s->size - s->pos)
  {
    static size_t chunk = 4096-1;
    s->size += ((size_t)n+1 + (chunk-1)) & ~(chunk-1);
    s->buf = realloc(s->buf, s->size);
    va_start(ap, fmt);
    n = vsnprintf(s->buf + s->pos, s->size - s->pos, fmt, ap);
    va_end(ap);
  }
  if (n > 0)
    s->pos += (size_t)n;
  assert(s->pos < s->size);
  if (s->pos > s->chop)
  {
    s->pos = s->chop;
    s->buf[s->chop] = 0;
    s->chopped = 1;
  }
  return !s->chopped;
}

static int printchar(struct tgstring *s, int ch, int quote)
{
  assert(ch >= 0);
  if (ch == quote || ch == '\\')
    return tgprintf(s, "\\%c", ch);
  else if (isprint(ch))
    return tgprintf(s, "%c", ch);
  else
    return tgprintf(s, "\\x%02x", ch);
}

static unsigned isprint_runlen (const unsigned char *s, unsigned n)
{
  unsigned m;
  for (m = 0; m < n && s[m] != '"' && s[m] != '\\' && isprint (s[m]); m++)
    ;
  return m;
}

static int tgprint1(struct tgstring *s, const struct tgtype *t, const char *data, int indent, enum tgprint_mode mode)
{
  const char *space = (mode == TGPM_DENSE) ? "" : " ";
  const char *commaspace = (mode == TGPM_DENSE) ? "," : ", ";

  switch (t->kind) {
    case TG_BOOLEAN:
      return tgprintf(s, "%s", *data ? "true" : "false");

    case TG_CHAR:
      tgprintf(s, "'");
      printchar(s, (unsigned char) *data, '\'');
      return tgprintf(s, "'");

    case TG_INT:
      switch (t->size) {
        case 1: return tgprintf(s, "%" PRId8, *(int8_t *)data); break;
        case 2: return tgprintf(s, "%" PRId16, *(int16_t *)data); break;
        case 4: return tgprintf(s, "%" PRId32, *(int32_t *)data); break;
        case 8: return tgprintf(s, "%" PRId64, *(int64_t *)data); break;
        default: assert(0);
      }
      break;

    case TG_UINT:
      switch (t->size) {
        case 1: return tgprintf(s, "%" PRIu8, *(uint8_t *)data); break;
        case 2: return tgprintf(s, "%" PRIu16, *(uint16_t *)data); break;
        case 4: return tgprintf(s, "%" PRIu32, *(uint32_t *)data); break;
        case 8: return tgprintf(s, "%" PRIu64, *(uint64_t *)data); break;
        default: assert(0);
      }
      break;

    case TG_FLOAT:
      switch (t->size) {
        case 4: return tgprintf(s, "%f", *(float *)data); break;
        case 8: return tgprintf(s, "%f", *(double *)data); break;
        default: assert(0);
      }
      break;

    case TG_ENUM: {
      int val = *(int *)data;
      unsigned i;
      for (i = 0; i < t->u.e.n; i++) {
        if (t->u.e.ms[i].v == val) {
          return tgprintf(s, "%s", t->u.e.ms[i].name);
        }
      }
      return tgprintf(s, "(%d)", val);
    }

    case TG_STRING: {
      const char *str = *((char **)data);
      if (str == NULL)
        return tgprintf (s, "(null)");
      else {
        tgprintf(s, "\"");
        while (*str)
        {
          unsigned char v = (unsigned char) *str++;
          if (!printchar(s, v, '"'))
            return 0;
        }
        return tgprintf(s, "\"");
      }
    }

    case TG_TIME: {
      DDS_Time_t t = *(DDS_Time_t *)data;
      if (t.sec == DDS_TIMESTAMP_INVALID_SEC && t.nanosec == DDS_TIMESTAMP_INVALID_NSEC)
        return tgprintf(s, "invalid");
      else if(t.sec == DDS_DURATION_INFINITE_SEC && t.nanosec == DDS_DURATION_INFINITE_NSEC)
        return tgprintf(s, "inf");
      else
        return tgprintf(s, "%d.%09u", t.sec, t.nanosec);
    }

    case TG_TYPEDEF:
      return tgprint1(s, t->u.td.type, data, indent, mode);

    case TG_STRUCT: {
      const struct tgtype_S *ts = &t->u.S;
      unsigned i;
      int c;
      c = tgprintf(s, "{%s", mode == TGPM_MULTILINE ? "" : space);
      for (i = 0; c && i < ts->n; i++) {
        if (mode == TGPM_MULTILINE)
          (void)tgprintf(s, "%s\n%*.*s", i == 0 ? "" : ",", indent+4, indent+4, "");
        else
          (void)tgprintf(s, "%s", i == 0 ? "" : commaspace);
        if (mode >= TGPM_FIELDS)
          (void)tgprintf(s, ".%s%s=%s", ts->ms[i].name, space, space);
        c = tgprint1(s, ts->ms[i].type, data + ts->ms[i].off, indent+4, mode);
      }
      return tgprintf(s, "%s}", ts->n > 0 ? space : "");
    }

    case TG_SEQUENCE: {
      const dds_seq_t *seq = (const dds_seq_t *)data;
      const unsigned char *data1 = (const unsigned char *)seq->_buffer;
      const struct tgtype *st = t->u.seq.type;
      const size_t size1 = st->size;
      const char *sep;
      int sepind, c;
      if (mode != TGPM_MULTILINE || !(st->kind == TG_SEQUENCE || st->kind == TG_ARRAY || st->kind == TG_STRUCT || st->kind == TG_UNION)) {
        sep = "";
        sepind = 0;
      } else {
        sep = "\n";
        sepind = indent+4;
      }
      c = tgprintf(s, "{%s", space);
      /* Special-case sequences/arrays of chars and octets */
      if (!(st->kind == TG_CHAR || (st->kind == TG_UINT && st->size == 1))) {
        for (unsigned i = 0; c && i < seq->_length; i++) {
          if (i != 0) tgprintf(s, ",%s%*.*s", sep, sepind, sepind, "");
          c = tgprint1(s, st, (char *)data1, indent+4, mode);
          data1 += size1;
        }
      } else {
        unsigned i = 0;
        while (c && i < seq->_length)
        {
          unsigned m = isprint_runlen(data1, seq->_length - i);
          if (m >= 4) {
            tgprintf(s, "%s\"", i == 0 ? "" : ",");
            for (unsigned j = 0; j < m; j++)
              tgprintf(s, "%c", *data1++);
            c = tgprintf(s, "\"");
          } else {
            m = 1;
            c = tgprintf(s, "%s%d", i == 0 ? "" : ",", *data1++);
          }
          i += m;
        }
      }
      return tgprintf(s, "%s}", seq->_length > 0 ? space : "");
    }

    case TG_ARRAY: {
      unsigned i;
      int c;
      c = tgprintf(s, "{%s", space);
      for (i = 0; c && i < t->u.ary.n; i++) {
        if (i != 0) tgprintf(s, ",");
        c = tgprint1(s, t->u.ary.type, data, indent+4, mode);
        data += t->u.ary.type->size;
      }
      return tgprintf(s, "%s}", space);
    }

    case TG_UNION: {
      const struct tgtype_U *tu = &t->u.U;
      uint64_t dv;
      unsigned i;
      int msidx;
      tgprint1(s, tu->dtype, data, indent+4, mode);
      dv = loaddisc(tu->dtype, data);
      for (i = 0; i < tu->nlab; i++)
        if (dv == tu->labs[i].val)
          break;
      msidx = (i < tu->nlab) ? tu->labs[i].msidx : tu->msidxdef;
      if (msidx == -1)
        return tgprintf(s, ":(invalid)");
      else {
        tgprintf(s, ":");
        if (mode >= TGPM_FIELDS)
          tgprintf(s, ".%s%s=%s", tu->ms[msidx].name, space, space);
        return tgprint1(s, tu->ms[msidx].type, data + tu->off, indent + 4, mode);
      }
    }
  }
  return 1;
}

int tgprint(struct tgstring *s, const struct tgtopic *tp, const void *data, enum tgprint_mode mode)
{
  if (s->chop == 0)
    return 0;
  else
  {
    s->chopped = 0;
    s->pos = 0;
    s->buf[0] = 0;
    (void)tgprint1(s, tp->type, data, 0, mode);
    return !s->chopped;
  }
}

int tgprintkey(struct tgstring *s, const struct tgtopic *tp, const void *keydata, enum tgprint_mode mode)
{
  if (s->chop == 0)
    return 0;
  else
  {
    const char *space = (mode == TGPM_DENSE) ? "" : " ";
    const char *commaspace = (mode == TGPM_DENSE) ? "," : ", ";
    unsigned i;
    int c;
    s->chopped = 0;
    s->pos = 0;
    s->buf[0] = 0;
    c = tgprintf(s, "{%s", mode == TGPM_MULTILINE ? "" : space);
    for (i = 0; c && i < tp->nkeys; i++)
    {
      if (mode == TGPM_MULTILINE)
        (void)tgprintf(s, "%s\n%*.*s", i == 0 ? "" : ",", 4, 4, "");
      else
        (void)tgprintf(s, "%s", i == 0 ? "" : commaspace);
      if (mode >= TGPM_FIELDS)
        (void)tgprintf(s, ".%s%s=%s", tp->keys[i].name, space, space);
      c = tgprint1(s, tp->keys[i].type, (const char *) keydata + tp->keys[i].off, 0, mode);
    }
    (void)tgprintf(s, "%s}", tp->nkeys > 0 ? space : "");
    return !s->chopped;
  }
}

static void tgfreedata1(const struct tgtype *t, char *data)
{
  switch(t->kind) {
    case TG_BOOLEAN:
    case TG_CHAR:
    case TG_INT:
    case TG_UINT:
    case TG_FLOAT:
    case TG_ENUM:
    case TG_TIME:
      break;

    case TG_STRING:
      free(*(char **)data);
      break;

    case TG_TYPEDEF:
      tgfreedata1(t->u.td.type, data);
      break;

    case TG_STRUCT: {
      const struct tgtype_S *ts = &t->u.S;
      for (unsigned i = 0; i < ts->n; i++)
        tgfreedata1(ts->ms[i].type, data + ts->ms[i].off);
      break;
    }

    case TG_ARRAY:
      for (unsigned i = 0; i < t->u.ary.n; i++)
        tgfreedata1(t->u.ary.type, data + i * t->u.ary.type->size);
      break;

    case TG_SEQUENCE: {
      const dds_seq_t *seq = (const dds_seq_t *)data;
      for (unsigned i = 0; i < seq->_length; i++)
        tgfreedata1(t->u.seq.type, (char*)seq->_buffer + i * t->u.seq.type->size);
      free(seq->_buffer);
      break;
    }

    case TG_UNION: {
      const struct tgtype_U *tu = &t->u.U;
      uint64_t dv = loaddisc(tu->dtype, data);
      unsigned i;
      int msidx;
      for (i = 0; i < tu->nlab; i++)
        if (dv == tu->labs[i].val)
          break;
      msidx = (i < tu->nlab) ? tu->labs[i].msidx : tu->msidxdef;
      if (msidx >= 0)
        tgfreedata1(tu->ms[msidx].type, data + tu->off);
      break;
    }
  }
}

void tgfreedata(const struct tgtopic *tp, void *data)
{
  tgfreedata1(tp->type, data);
}

static void skipspace(struct lexer *l)
{
  while (isspace((unsigned char) *l->src))
    l->src++;
}

static void freetoken(struct token *tok)
{
  if (tok->kind == TOK_SYMBOL || tok->kind == TOK_STRING)
    free(tok->val.str);
  tok->kind = TOK_ERROR;
}

static void init_lexer(struct token *tok, struct lexer *l, const char *src)
{
  tok->kind = TOK_ERROR;
  l->error = l->have_token = 0;
  l->src = l->srcstart = src;
}

static int scanerror(struct token *tok, struct lexer *l, const char *fmt, ...)
{
  if (!l->error)
  {
    const int pos = (int) (tok->src - l->srcstart);
    const int sz = (int) (l->src - tok->src);
    va_list ap;

    {
      int x, y, nsrc = (int) strlen(l->src);
      while (nsrc > 0 && isspace((unsigned char)l->src[nsrc-1]))
        nsrc--;
      x = (pos < 20) ? pos : 20;
      y = (nsrc < 20) ? nsrc : 20;
      fprintf(stderr, "input error in: %*.*s\n", x+sz+y, x+sz+y, l->srcstart+pos-x);
      fprintf(stderr, "%*.*s", 16+x, 16+x, "");
      for (int i = 0; i < (sz ? sz : 1); i++)
        fputc('^', stderr);
      fprintf(stderr, "\n");
    }

    fprintf(stderr, "at position %d (%*.*s): ", pos, sz, sz, tok->src);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    l->error = 1;
  }
  freetoken(tok);
  if (l->have_token)
    freetoken(&l->token);
  return 0;
}

static enum tokenkind scanword(struct token *tok, struct lexer *l)
{
  size_t n;
  assert(isalpha((unsigned char) *l->src) || *l->src == '_');
  while (isalnum((unsigned char) *l->src) || *l->src == '_')
    l->src++;
  n = (size_t) (l->src - tok->src);
  tok->val.str = malloc(n + 1);
  memcpy(tok->val.str, tok->src, n);
  tok->val.str[n] = 0;
  return TOK_SYMBOL;
}

static enum tokenkind scanint(struct token *tok, struct lexer *l)
{
  char *endp;
  tok->val.ui = strtoull(l->src, &endp, 0);
  if (endp == l->src) {
    scanerror(tok, l, "integer expected");
    return TOK_ERROR;
  } else if (isalpha((unsigned char) *endp)) {
    scanerror(tok, l, "junk after end of integer");
    return TOK_ERROR;
  } else {
    l->src = endp;
    return TOK_INT;
  }
}

static enum tokenkind scandouble(struct token *tok, struct lexer *l)
{
  char *endp;
  tok->val.d = strtod(l->src, &endp);
  if (endp == l->src) {
    scanerror(tok, l, "floating-point number expected");
    return TOK_ERROR;
  } else if (isalpha((unsigned char) *endp)) {
    scanerror(tok, l, "junk after end of floating-point number");
    return TOK_ERROR;
  } else {
    l->src = endp;
    return TOK_FLOAT;
  }
}

static enum tokenkind scannumber(struct token *tok, struct lexer *l)
{
  char *endp;
  tok->val.ui = strtoull(l->src, &endp, 0);
  if (endp == l->src) {
    scanerror(tok, l, "number expected");
    return TOK_ERROR;
  } else if (*endp == '.' || *endp == 'e' || *endp == 'E') {
    /* presumably floating-point */
    return scandouble(tok, l);
  } else if (isalpha((unsigned char) *endp)) {
    scanerror(tok, l, "junk after end of integer");
    return TOK_ERROR;
  } else {
    l->src = endp;
    return TOK_INT;
  }
}

static int scanchareschex(char *x, struct lexer *l)
{
  int i = 0, v = 0;
  assert(isxdigit((unsigned char)*l->src));
  while (isxdigit((unsigned char)*l->src) && i++ < 2) {
    int d;
    if (isdigit((unsigned char) *l->src))
      d = (*l->src - '0');
    else if (isupper((unsigned char) *l->src))
      d = (*l->src - 'A' + 10);
    else {
      assert (islower((unsigned char) *l->src));
      d = (*l->src - 'a' + 10);
    }
    v = v * 16 + d;
    l->src++;
  }
  *x = (char) ((unsigned char) v);
  return v;
}

static int scancharescoct(char *x, struct lexer *l)
{
  struct token tok = TOKEN_INIT(l->src);
  int i = 0, v = 0;
  assert(*l->src >= '0' && *l->src <= '7');
  while (*l->src >= '0' && *l->src <= '7' && i++ < 3) {
    int d = *l->src++ - '0';
    v = v * 8 + d;
  }
  if (v > 255) {
    scanerror(&tok, l, "invalid character escape\n");
    return 0;
  }
  *x = (char) ((unsigned char) v);
  return v;
}

static int scancharesc(char *x, struct lexer *l)
{
  struct token tok = TOKEN_INIT(l->src);
  if (*l->src == 'x' || *l->src == 'X') {
    l->src++;
    return scanchareschex(x, l);
  } else if (*l->src >= '0' && *l->src <= '7') {
    return scancharescoct(x, l);
  } else {
    switch(*l->src) {
      case 'n':  l->src++; *x = '\n'; return 1;
      case 'r':  l->src++; *x = '\r'; return 1;
      case 'v':  l->src++; *x = '\v'; return 1;
      case 't':  l->src++; *x = '\t'; return 1;
      case 'f':  l->src++; *x = '\f'; return 1;
      case 'e':  l->src++; *x = 0x1b; return 1;
      case '\'': l->src++; *x = '\''; return 1;
      case '"':  l->src++; *x = '"';  return 1;
      case '\\': l->src++; *x = '\\'; return 1;
      default:
        scanerror (&tok, l, "invalid character escape");
        return 0;
    }
  }
}

static int scanbarechar(char *x, struct lexer *l)
{
  struct token tok = TOKEN_INIT(l->src);
  if (*l->src == '\\') {
    l->src++;
    if (!scancharesc(x, l))
      return 0;
  } else if (*l->src == 0) {
    scanerror (&tok, l, "unexpected end of input");
    return 0;
  } else {
    *x = *l->src;
    l->src++;
  }
  return 1;
}

static enum tokenkind scanchar(struct token *tok, struct lexer *l)
{
  if (*l->src == '\'')
  {
    l->src++;
    if (*l->src == '\'') {
      scanerror(tok, l, "invalid character literal (empty)");
      return TOK_ERROR;
    } else if (!scanbarechar(&tok->val.ch, l)) {
      return TOK_ERROR;
    } else if (*l->src != '\'') {
      scanerror(tok, l, "invalid character literal (missing closing quote)");
      return TOK_ERROR;
    }
    l->src++;
    return TOK_CHAR;
  }
  else if (isdigit((unsigned char) *l->src))
  {
    if (scanint(tok, l) == TOK_ERROR)
      return TOK_ERROR;
    tok->val.ch = (char) tok->val.ui;
    return TOK_CHAR;
  }
  else
  {
    scanerror(tok, l, "expected a character literal or an unsigned integer");
    return TOK_ERROR;
  }
}

static enum tokenkind scanstring(struct token *tok, struct lexer *l)
{
  size_t sz = 0, n = 0;
  if (*l->src != '"') {
    scanerror(tok, l, "expected a string literal");
    return TOK_ERROR;
  }
  tok->val.str = NULL;
  l->src++;
  while (*l->src && *l->src != '"') {
    char x;
    if (!scanbarechar(&x, l)) {
      free(tok->val.str);
      return TOK_ERROR;
    }
    if (n == sz)
      tok->val.str = realloc(tok->val.str, sz += 256);
    tok->val.str[n++] = (char)x;
  }
  if (n == sz)
    tok->val.str = realloc(tok->val.str, sz + 1);
  tok->val.str[n] = 0;
  if (*l->src != '"') {
    scanerror(tok, l, "unterminated string literal");
    free(tok->val.str);
    return TOK_ERROR;
  }
  l->src++;
  return TOK_STRING;
}

static enum tokenkind scantoken(struct token *tok, struct lexer *l)
{
  freetoken(tok);
  if (l->have_token) {
    *tok = l->token;
    l->have_token = 0;
    return tok->kind;
  } else {
    skipspace(l);
    tok->src = l->src;
    switch (*l->src)
    {
      case 0:
        return tok->kind = TOK_EOF;
      case '-':
      case '0': case '1': case '2': case '3': case '4':
      case '5': case '6': case '7': case '8': case '9':
        return tok->kind = scannumber(tok, l);
      case '\'':
        return tok->kind = scanchar(tok, l);
      case '"':
        return tok->kind = scanstring(tok, l);
      case '=':
        l->src++;
        return tok->kind = TOK_EQUALS;
      case ',':
        l->src++;
        return tok->kind = TOK_COMMA;
      case ':':
        l->src++;
        return tok->kind = TOK_COLON;
      case '.':
        if (isdigit((unsigned char) *(l->src + 1)))
          return tok->kind = scandouble(tok, l);
        else
        {
          l->src++;
          return tok->kind = TOK_DOT;
        }
      case '{':
        l->src++;
        return tok->kind = TOK_LBRACE;
      case '}':
        l->src++;
        return tok->kind = TOK_RBRACE;
      case '[':
        l->src++;
        return tok->kind = TOK_LBRACKET;
      case ']':
        l->src++;
        return tok->kind = TOK_RBRACKET;
      default:
        if (isalpha((unsigned char) *l->src) || *l->src == '_')
          return tok->kind = scanword(tok, l);
        else {
          tok->val.ch = *l->src++;
          return tok->kind = TOK_LITERAL;
        }
    }
  }
}

static void pushbacktoken(struct token *tok, struct lexer *l)
{
  assert(!l->have_token);
  l->have_token = 1;
  l->token = *tok;
  /* reset to avoid double frees */
  tok->kind = TOK_ERROR;
}

static int casttofloat(struct token *tok, struct lexer *l)
{
  if (tok->kind == TOK_FLOAT)
    return 1;
  else if (tok->kind == TOK_INT) {
    tok->val.d = tok->val.i;
    tok->kind = TOK_FLOAT;
    return 1;
  } else
    return scanerror(tok, l, "integer or floating-point literal expected");
}

static int casttoint(struct token *tok, struct lexer *l)
{
  if (tok->kind == TOK_INT)
    return 1;
  else if (tok->kind == TOK_CHAR) {
    tok->val.i = tok->val.ch;
    tok->kind = TOK_INT;
    return 1;
  } else
    return scanerror(tok, l, "integer or character literal expected");
}

static const struct tgtype_ems boolean_as_enum_ms[] = {
  { .v = 0, .name = "false" },
  { .v = 1, .name = "true" }
};

static const struct tgtype boolean_as_enum = {
  .name = "boolean",
  .size = 1,
  .align = 1,
  .kind = TG_ENUM,
  .u = {
    .e = {
      .n = 2,
      .ms = (struct tgtype_ems *) boolean_as_enum_ms
    }
  }
};

static int lookupenum(int *v, struct token *tok, struct lexer *l, const struct tgtype *t)
{
  unsigned i;
  assert(t->kind == TG_ENUM);
  if (tok->kind == TOK_INT) {
    for (i = 0; i < t->u.e.n; i++)
      if (t->u.e.ms[i].v == (int) tok->val.i)
        break;
  } else if (tok->kind == TOK_SYMBOL) {
    for (i = 0; i < t->u.e.n; i++)
      if (strcmp(t->u.e.ms[i].name, tok->val.str) == 0)
        break;
  } else {
    return scanerror(tok, l, "integer literal or symbol expected");
  }
  if (i == t->u.e.n)
    return scanerror(tok, l, "unknown enum constant");
  else {
    *v = t->u.e.ms[i].v;
    return 1;
  }
}

static int tgscan1fieldsel (const struct tgtype_S *ts, struct lexer *l, unsigned *fidx)
{
  struct token tok = TOKEN_INIT(l->src);
  enum tokenkind tk;
  if ((tk = scantoken(&tok, l)) != TOK_SYMBOL)
    return scanerror(&tok, l, "field name expected following '.'");
  for (*fidx = 0; *fidx < ts->n; (*fidx)++)
    if (strcmp(ts->ms[*fidx].name, tok.val.str) == 0)
      break;
  if (*fidx == ts->n)
    return scanerror(&tok, l, "field not found");
  tk = scantoken(&tok, l);
  switch (tk) {
    case TOK_EQUALS:
      break;
    case TOK_DOT:
      pushbacktoken(&tok, l);
      if (detypedef (ts->ms[*fidx].type)->kind == TG_STRUCT)
        break;
      return scanerror(&tok, l, "preceding field not of struct type");
    default:
      return scanerror(&tok, l, "'=' or subfield expected");
  }
  freetoken(&tok);
  return 1;
}

static int tgscan1(char *dst, const struct tgtype *t, struct lexer *l)
{
  struct token tok = TOKEN_INIT(l->src);
  enum tokenkind tk;
  int v;

  t = detypedef(t);
  tk = scantoken(&tok, l);
  switch(t->kind) {
    case TG_BOOLEAN:
      if (!lookupenum(&v, &tok, l, &boolean_as_enum))
        return 0;
      *dst = (char) v;
      break;

    case TG_CHAR:
      if (tk == TOK_CHAR)
        *dst = tok.val.ch;
      else if (tk == TOK_INT) {
        if (tok.val.i >= 0 && tok.val.i <= 255)
          *dst = (char)tok.val.i;
        else
          return scanerror(&tok, l, "expected character literal or integer in 0 .. 255");
      }
      break;

    case TG_INT:
      if (!casttoint(&tok, l))
        return scanerror(&tok, l, "(unwinding)");
      switch (t->size) {
        case 1: *(int8_t *)dst = (int8_t)tok.val.i; break;
        case 2: *(int16_t *)dst = (int16_t)tok.val.i; break;
        case 4: *(int32_t *)dst = (int32_t)tok.val.i; break;
        case 8: *(int64_t *)dst = (int64_t)tok.val.i; break;
        default: assert(0);
      }
      break;

    case TG_UINT:
      if (!casttoint(&tok, l))
        return scanerror(&tok, l, "(unwinding)");
      switch (t->size) {
        case 1: *(uint8_t *)dst = (uint8_t)tok.val.ui; break;
        case 2: *(uint16_t *)dst = (uint16_t)tok.val.ui; break;
        case 4: *(uint32_t *)dst = (uint32_t)tok.val.ui; break;
        case 8: *(uint64_t *)dst = (uint64_t)tok.val.ui; break;
        default: assert(0);
      }
      break;

    case TG_FLOAT:
      if (!casttofloat(&tok, l))
        return scanerror(&tok, l, "(unwinding)");
      switch (t->size) {
        case 4: *(float *)dst = (float)tok.val.d; break;
        case 8: *(double *)dst = (double)tok.val.d; break;
        default: assert(0);
      }
      break;

    case TG_ENUM:
      if (!lookupenum((int *)dst, &tok, l, t))
        return 0;
      break;

    case TG_STRING:
      if (tk != TOK_STRING)
        return scanerror(&tok, l, "string literal expected");
      *((char **)dst) = strdup(tok.val.str);
      break;

    case TG_TIME:
      if (tk == TOK_SYMBOL) {
        if (strcmp(tok.val.str, "inf") == 0)
          set_infinite_dds_duration((DDS_Duration_t *)dst);
        else
          return scanerror(&tok, l, "inf, integer or floating-point literal expected");
      } else if (!casttofloat(&tok, l)) {
        return 0;
      } else if (double_to_dds_duration ((DDS_Duration_t *)dst, tok.val.d) < 0) {
        return scanerror(&tok, l, "invalid time/duration");
      }
      break;

    case TG_TYPEDEF:
      assert(0);

    case TG_STRUCT: {
      const struct tgtype_S *ts = &t->u.S;
      unsigned fidx = 0, first = 1;
      if (tk == TOK_DOT) {
        if (!tgscan1fieldsel (ts, l, &fidx))
          return scanerror (&tok, l, "(unwinding)");
        if (!tgscan1(dst + ts->ms[fidx].off, ts->ms[fidx].type, l))
          return scanerror (&tok, l, "(unwinding)");
      } else {
        if (tk != TOK_LBRACE)
          return scanerror(&tok, l, "'{' expected at start of struct");
        while ((tk = scantoken(&tok, l)) != TOK_ERROR && tk != TOK_RBRACE) {
          if (tk == TOK_EOF)
            return scanerror(&tok, l, "unexpected end of input");
          if (!first) {
            if (tk != TOK_COMMA)
              return scanerror(&tok, l, "',' expected");
            tk = scantoken(&tok, l);
          }
          if (tk != TOK_DOT)
            pushbacktoken(&tok, l);
          else if (!tgscan1fieldsel (ts, l, &fidx))
            return scanerror (&tok, l, "(unwinding)");
          if (fidx == ts->n)
            return scanerror (&tok, l, "fields beyond end of struct present");
          if (!tgscan1(dst + ts->ms[fidx].off, ts->ms[fidx].type, l))
            return scanerror (&tok, l, "(unwinding)");
          first = 0;
          fidx++;
        }
      }
      break;
    }

    case TG_SEQUENCE: case TG_ARRAY: {
      dds_seq_t tmpseq, *dstseq;
      const struct tgtype *st = t->u.seq.type;
      const size_t size1 = st->size;
      unsigned maxn;
      if (t->kind == TG_SEQUENCE) {
        dstseq = (dds_seq_t *) dst;
        memset(dstseq, 0, sizeof (*dstseq));
        maxn = (unsigned) t->u.seq.maxn;
      } else {
        dstseq = &tmpseq;
        memset(dstseq, 0, sizeof (*dstseq));
        dstseq->_buffer = (void *) dst;
        maxn = (unsigned) t->u.ary.n;
      }
      if (tk != TOK_LBRACE)
        return scanerror(&tok, l, "'{' expected");
      while ((tk = scantoken(&tok, l)) != TOK_ERROR && tk != TOK_RBRACE) {
        if (tk == TOK_EOF)
          return scanerror(&tok, l, "unexpected end of input");
        if (dstseq->_length == 0)
          pushbacktoken(&tok, l);
        else if (tk != TOK_COMMA)
          return scanerror(&tok, l, "',' expected");
        if (maxn && dstseq->_length == maxn)
          return scanerror(&tok, l, "too many elements");
        if (t->kind == TG_SEQUENCE)
          dstseq->_buffer = realloc(dstseq->_buffer, (dstseq->_length+1) * size1);
        if (!tgscan1((char *)dstseq->_buffer + dstseq->_length * size1, st, l))
          return scanerror(&tok, l, "(unwinding)");
        dstseq->_length++;
      }
      dstseq->_maximum = dstseq->_length;
      break;
    }

    case TG_UNION: {
      const struct tgtype_U *tu = &t->u.U;
      uint64_t dv;
      unsigned i;
      int msidx;
      if (tk == TOK_DOT) { /* .MEMBER=VALUE */
        if (scantoken(&tok, l) != TOK_SYMBOL)
          return scanerror(&tok, l, "symbol expected");
        for (msidx = 0; (unsigned) msidx < tu->n; msidx++)
          if (strcmp(tu->ms[msidx].name, tok.val.str) == 0)
            break;
        if ((unsigned) msidx == tu->n)
          return scanerror(&tok, l, "non-existent member");
        if (msidx != tu->msidxdef) {
          int seen = 0;
          for (i = 0; i < tu->nlab; i++)
            if (tu->labs[i].msidx != msidx)
              continue;
            else if (seen++)
              return scanerror(&tok, l, "ambiguous discriminator value");
            else
              storedisc(dst, tu->dtype, tu->labs[i].val);
        }
        if (scantoken(&tok, l) != TOK_EQUALS)
          return scanerror(&tok, l, "'=' expected");
      } else { /* DISC:VALUE or DISC:.MEMBER=VALUE */
        pushbacktoken(&tok, l);
        if (!tgscan1(dst, tu->dtype, l))
          return scanerror(&tok, l, "(unwinding)");
        dv = loaddisc(tu->dtype, dst);
        for (i = 0; i < tu->nlab; i++)
          if (dv == tu->labs[i].val)
            break;
        msidx = (i < tu->nlab) ? tu->labs[i].msidx : tu->msidxdef;
        if (msidx == -1)
          return scanerror(&tok, l, "invalid discriminator value");
        if (scantoken(&tok, l) != TOK_COLON)
          return scanerror(&tok, l, "':' expected");
        if (scantoken(&tok, l) != TOK_DOT)
          pushbacktoken(&tok, l);
        else {
          if (scantoken(&tok, l) != TOK_SYMBOL)
            return scanerror(&tok, l, "symbol expected");
          if (strcmp(tu->ms[msidx].name, tok.val.str) != 0) {
            for (i = 0; i < tu->n; i++)
              if (strcmp(tu->ms[i].name, tok.val.str) == 0)
                break;
            if (i == tu->n)
              return scanerror(&tok, l, "non-existent member");
            else
              return scanerror(&tok, l, "mismatch between discriminator and member");
          }
          if (scantoken(&tok, l) != TOK_EQUALS)
            return scanerror(&tok, l, "'=' expected");
        }
      }
      if (!tgscan1(dst + tu->off, tu->ms[msidx].type, l))
        return scanerror(&tok, l, "(unwinding)");
      break;
    }
  }

  freetoken(&tok);
  return 1;
}

void *tgscan(const struct tgtopic *tp, const char *src, char **endp)
{
  struct lexer l;
  struct token tok;
  void *dst = malloc (tp->size);
  memset(dst, 0, tp->size);
  init_lexer(&tok, &l, src);

#if 0
  {
    enum tokenkind tk = scantoken(&tok, &l);
    if (tk == TOK_SYMBOL)
    {
      const struct tgtype *tt;
      size_t off;
      freetoken(&tok);
      lookupfield(&tt, &off, tp->type, src);
      if (tt == NULL)
        printf("? size ? off %zu\n", off);
      else
        printf("%s size %zu off %zu\n", tgkindstr(tt->kind), tt->size, off);
      return NULL;
    }
    pushbacktoken(&tok, &l);
  }
#endif

  if (!tgscan1(dst, tp->type, &l))
    goto error;
  if (endp) {
    *endp = (char *) (l.have_token ? l.token.src : l.src);
  } else if (scantoken(&tok, &l) != TOK_EOF) {
    scanerror(&tok, &l, "junk at end of input");
    goto error;
  }
  freetoken(&tok);
  return dst;

error:
  if (endp)
    *endp = NULL;
  freetoken(&tok);
  tgfreedata(tp, dst);
  return NULL;
}
