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
#ifdef __APPLE__
#define USE_EDITLINE 1
#endif

#define _ISOC99_SOURCE
#include <time.h>
#include <string.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <signal.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <ctype.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#if USE_EDITLINE
#include <histedit.h>
#endif

#include "common.h"
#include "testtype.h"
#include "tglib.h"
#include "porting.h"
#include "ddsicontrol.h"

#if PRE_V6_5
#define DDS_DataReader_read DDS__FooDataReader_read
#define DDS_DataReader_take DDS__FooDataReader_take
#define DDS_DataReader_take_w_condition DDS__FooDataReader_take_w_condition
#define DDS_DataReader_read_w_condition DDS__FooDataReader_read_w_condition
#define DDS_DataReader_return_loan DDS__FooDataReader_return_loan
#define DDS_DataWriter_register_instance DDS__FooDataWriter_register_instance
#define DDS_DataWriter_register_instance_w_timestamp DDS__FooDataWriter_register_instance_w_timestamp
#define DDS_DataWriter_write DDS__FooDataWriter_write
#define DDS_DataWriter_write_w_timestamp DDS__FooDataWriter_write_w_timestamp
#define DDS_DataWriter_dispose_w_timestamp DDS__FooDataWriter_dispose_w_timestamp
#define DDS_DataWriter_writedispose_w_timestamp DDS__FooDataWriter_writedispose_w_timestamp
#define DDS_DataWriter_unregister_instance_w_timestamp DDS__FooDataWriter_unregister_instance_w_timestamp
#endif

#define NUMSTR "0123456789"
#define HOSTNAMESTR "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ-." NUMSTR

typedef DDS_ReturnCode_t (*write_oper_t) (DDS_DataWriter wr, void *d, DDS_InstanceHandle_t h, const DDS_Time_t *ts);
static DDS_Topic find_topic(DDS_DomainParticipant dp, const char *name, const DDS_Duration_t *timeout);

enum topicsel { UNSPEC, KS, K32, K64, K128, K256, OU, ARB };
enum readermode { MODE_PRINT, MODE_CHECK, MODE_ZEROLOAD, MODE_DUMP, MODE_NONE };

#define PM_PID 1u
#define PM_TOPIC 2u
#define PM_TIME 4u
#define PM_IHANDLE 8u
#define PM_PHANDLE 16u
#define PM_STIME 32u
#define PM_RTIME 64u
#define PM_DGEN 128u
#define PM_NWGEN 256u
#define PM_RANKS 512u
#define PM_STATE 1024u

static int fdservsock = -1;
static volatile sig_atomic_t termflag = 0;
static int pid;
static DDS_GuardCondition termcond;
static unsigned nkeyvals = 1;
static int once_mode = 0;
static int extra_readers_at_end = 0;
static int wait_hist_data = 0;
static DDS_Duration_t wait_hist_data_timeout = { 0, 0 };
static double dur = 0.0;
static int sigpipe[2];
static int termpipe[2];
static int fdin = 0;
static int print_latency = 0;
static FILE *latlog_fp = NULL;
static enum tgprint_mode print_mode = TGPM_FIELDS;
static unsigned print_metadata = PM_STATE;
static unsigned print_chop = 0xffffffff;
static int printtype = 0;
static int print_final_take_notice = 1;
static int print_tcp = 0;
static DDS_Topic ddsi_control_topic = DDS_HANDLE_NIL;
static DDS_Publisher ddsi_control_pub;
static q_osplserModule_ddsi_controlDataWriter ddsi_control_wr;

#define T_SECOND ((int64_t) 1000000000)
struct tstamp_t {
  int isabs;
  int64_t t;
};

struct readerspec {
  DDS_DataReader rd;
  enum topicsel topicsel;
  struct tgtopic *tgtp;
  enum readermode mode;
  int exit_on_out_of_seq;
  int use_take;
  unsigned sleep_us;
  int polling;
  int read_maxsamples;
  int print_match_pre_read;
  int do_final_take;
  unsigned idx;
};

enum writermode {
  WM_NONE,
  WM_AUTO,
  WM_INPUT
};

struct writerspec {
  DDS_DataWriter wr;
  DDS_DataWriter dupwr;
  enum topicsel topicsel;
  DDS_string tpname;
  struct tgtopic *tgtp;
  double writerate;
  unsigned baggagesize;
  int register_instances;
  int duplicate_writer_flag;
  unsigned burstsize;
  enum writermode mode;
};

static const struct readerspec def_readerspec = {
  .rd = DDS_HANDLE_NIL,
  .topicsel = UNSPEC,
  .tgtp = NULL,
  .mode = MODE_PRINT,
  .exit_on_out_of_seq = 0,
  .use_take = 1,
  .sleep_us = 0,
  .polling = 0,
  .read_maxsamples = DDS_LENGTH_UNLIMITED,
  .print_match_pre_read = 0,
  .do_final_take = 0
};

static const struct writerspec def_writerspec = {
  .wr = DDS_HANDLE_NIL,
  .dupwr = DDS_HANDLE_NIL,
  .topicsel = UNSPEC,
  .tpname = NULL,
  .tgtp = NULL,
  .writerate = 0.0,
  .baggagesize = 0,
  .register_instances = 0,
  .duplicate_writer_flag = 0,
  .burstsize = 1,
  .mode = WM_INPUT
};

struct wrspeclist {
  struct writerspec *spec;
  struct wrspeclist *prev, *next; /* circular */
};

static void terminate (void)
{
  const char c = 0;
  termflag = 1;
  write(termpipe[1], &c, 1);
  DDS_GuardCondition_set_trigger_value(termcond, 1);
}

static void sigh (int sig __attribute__ ((unused)))
{
  const char c = 1;
  ssize_t r;
  do {
    r = write (sigpipe[1], &c, 1);
  } while (r == -1 && errno == EINTR);
}

static void *sigthread(void *varg __attribute__ ((unused)))
{
  while (1)
  {
    char c;
    ssize_t r;
    if ((r = read (sigpipe[0], &c, 1)) < 0)
    {
      if (errno == EINTR)
        continue;
      error ("sigthread: read failed, errno %d\n", (int) errno);
    }
    else if (r == 0)
      error ("sigthread: unexpected eof\n");
    else if (c == 0)
      break;
    else
      terminate();
  }
  return NULL;
}

static int open_tcpserver_sock (int port)
{
  struct sockaddr_in saddr;
  int fd;
#if __APPLE__
  saddr.sin_len = sizeof (saddr);
#endif
  saddr.sin_family = AF_INET;
  saddr.sin_port = htons (port);
  saddr.sin_addr.s_addr = INADDR_ANY;
  if ((fd = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1)
  {
    perror ("socket()");
    exit (2); /* will kill any waiting threads */
  }
  if (bind (fd, (struct sockaddr *) &saddr, sizeof (saddr)) == -1)
  {
    perror ("bind()");
    exit (2); /* will kill any waiting threads */
  }
  if (listen (fd, 1) == -1)
  {
    perror ("listen()");
    exit (2); /* will kill any waiting threads */
  }
  if (print_tcp)
    printf ("listening ... ");
  fflush (stdout);
  return fd;
}

static void usage (const char *argv0)
{
  fprintf (stderr, "\
usage: %s [OPTIONS] PARTITION...\n\
\n\
OPTIONS:\n\
  -T TOPIC[:TO][:EXPR]  set topic name to TOPIC, TO is an optional timeout in\n\
                  seconds (default 10, use inf to wait indefinitely) for\n\
                  find_topic in ARB mode; EXPR can be used to create a content\n\
                  filtered topic \"cftN\" with filter expression EXPR based on\n\
                  the topic. Environment variables in EXPR are expanded in the\n\
                  usual manner and with:\n\
                    $SYSTEMID set to the current system id (in decimal)\n\
                    $NODE_BUILTIN_PARTITION set to the node-specific built-in\n\
                        partition\n\
                  specifying a topic name when one has already been given\n\
                  introduces a new reader/writer pair\n\
  -K TYPE         select type (ARB is default if topic specified, KS if not),\n\
                  (default topic name in parenthesis):\n\
                    KS                - key, seq, octet sequence (PubSub)\n\
                    K32,K64,K128,K256 - key, seq, octet array (PubSub<N>)\n\
                    OU                - one ulong, keyless (PubSubOU)\n\
                    ARB               - use find_topic - no topic QoS override\n\
                                        possible)\n\
                    <FILE>            - read typename, keylist, metadata from\n\
                                        FILE, then define topic named T with\n\
                                        specified QoS\n\
                  specifying a type when one has already been given introduces\n\
                  a new reader/writer pair\n\
  -q FS:QOS       set QoS for entities indicated by FS, which must be one or\n\
                  more of: t (topic), p (publisher), s (subscriber),\n\
                  w (writer), r (reader), or a (all of them). For QoS syntax,\n\
                  see below. Inapplicable QoS's are ignored.\n\
  -q provider=[PROFILE,]URI  use URI, profile as QoS provider, in which case\n\
                  any QoS specification not of the LETTER=SETTING form gets\n\
                  passed as-is to the QoS provider (with the exception of the\n\
                  empty string, which is then translated to a null pointer to\n\
                  get the default value). Note that later specifications\n\
                  override earlier ones, so a QoS provider can be combined\n\
                  with the previous form as well. Also note that the default\n\
                  QoS's used by this program are slightly different from\n\
                  those of the DCPS API (topics default to by-source ordering\n\
                  and reliability, and readers and writers to the topic QoS),\n\
                  which may cause surprises :)\n\
  -m [0|p[p]|{c|x}[p][:N]|z|d[p]]  no reader, print values, check sequence\n\
                  numbers (x: exit 1 on receipt of out of sequence samples)\n\
                  (expecting N keys), \"zero-load\" mode or \"dump\" mode (which\n\
                  is differs from \"print\" primarily because it uses a data-\n\
                  available trigger and reads all samples in read-mode (default:\n\
                  p; pp, cp, dp are polling modes); set per-reader\n\
  -D DUR          run for DUR seconds\n\
  -M TO:U         wait for matching reader with user_data U and not owned\n\
                  by this instance of pubsub\n\
  -n N            limit take/read to N samples\n\
  -O              take/read once then exit 0 if samples present, or 1 if not\n\
  -P MODES        printing control (prefixing with \"no\" disables):\n\
                    meta           enable printing of all metadata\n\
                    trad           pid, time, phandle, stime, state\n\
                    pid            process id of pubsub\n\
                    topic          which topic (topic is def. for multi-topic)\n\
                    time           read time relative to program start\n\
                    phandle        publication handle\n\
                    ihandle        instance handle\n\
                    stime          source timestamp\n\
                    rtime          reception timestamp\n\
                    dgen           disposed generation count\n\
                    nwgen          no-writers generation count\n\
                    ranks          sample, generation, absolute generation ranks\n\
                    state          instance/sample/view states\n\
                    latency[=F]    show latency information for -mc[p] mode\n\
                                   =F: write raw 64-bit current & source\n\
                                       timestamps to file F\n\
                  additionally, for ARB types the following have effect:\n\
                    type           print type definition at start up\n\
                    dense          no additional white space, no field names\n\
                    fields         field names, some white space\n\
                    multiline      field names, one field per line\n\
                    chop:N         chop to N characters (N = 0 is allowed)\n\
                  for non-once mode:\n\
                    finaltake      print a \"final take\" notice before the\n\
                                   results of the optional final take just\n\
                                   before stopping\n\
                  for tcp-server setup:\n\
                    tcp            print when listening for or accepting a new\n\
                                   connection\n\
                  default is \"nometa,state,fields,finalttake\".\n\
  -r              register instances (-wN mode only)\n\
  -R              use 'read' instead of 'take'\n\
  -$              perform one final take-all just before stopping\n\
  -s MS           sleep MS ms after each read/take (default: 0)\n\
  -W TO           wait_for_historical_data TO (TO in seconds or inf)\n\
  -w F            writer mode/input selection, F:\n\
                    -     stdin (default)\n\
                    N     cycle through N keys as fast as possible\n\
                    N:R*B cycle through N keys at R bursts/second, each burst\n\
                          consisting of B samples\n\
                    N:R   as above, B=1\n\
                    :P    listen on TCP port P\n\
                    H:P   connect to TCP host H, port P\n\
                  no writer is created if -w0 and no writer listener\n\
                  automatic specifications can be given per writer; final\n\
                  interactive specification determines input used for non-\n\
                  automatic ones\n\
  -S EVENTS       monitor status events (comma separated; default: none)\n\
                  reader (abbreviated and full form):\n\
                    pr   pre-read (virtual event)\n\
                    sl   sample-lost\n\
                    sr   sample-rejected\n\
                    lc   liveliness-changed\n\
                    sm   subscription-matched\n\
                    riq  requested-incompatible-qos\n\
                    rdm  requested-deadline-missed\n\
                  writer:\n\
                    ll   liveliness-lost\n\
                    pm   publication-matched\n\
                    oiq  offered-incompatible-qos\n\
                    odm  offered-deadline-missed\n\
                  if no -S option given, then readers are created with riq,\n\
                  writers with oiq, as these are nearly always actual issues\n\
  -z N            topic size (affects KeyedSeq only)\n\
  -F              set line-buffered mode\n\
  -@              echo everything on duplicate writer (only for interactive)\n\
  -* [M:]N        sleep for M seconds just before deleteing participant and\n\
                  N seconds just before returning from main()\n\
  -!              disable signal handlers\n\
\n\
%s\n\
Note: defaults above are overridden as follows:\n\
  r:k=all,R=10000/inf/inf\n\
  w:k=all,R=100/inf/inf\n\
when group coherency is enabled, resource limits are forced to inf/inf/inf because of a restriction\n\
on the use of resource limits on the reader in combination with group coherency in some versions of\n\
OpenSplice.\n\
\n\
Input format is a white-space separated sequence (K* and OU, newline\n\
separated for ARB) of:\n\
N     write next sample, key value N\n\
wN    synonym for plain N; w@T N same with timestamp of T\n\
      T is absolute if prefixed with \"=\", T currently in seconds\n\
dN    dispose, key value N; d@T N as above\n\
DN    write dispose, key value N; D@T N as above\n\
uN    unregister, key value N; u@T N as above\n\
rN    register, key value N; u@T N as above\n\
sN    sleep for N seconds\n\
nN    nap for N microseconds\n\
zN    set topic size to N (affects KeyedSeq only)\n\
pX    set publisher partition to comma-separated list X\n\
Y     dispose_all\n\
B     begin coherent changes\n\
E     end coherent changes\n\
SP;T;U  make persistent snapshot with given partition and topic\n\
      expressions and URI\n\
CT;F;D  write DDSI control topic (if feature enabled in config)\n\
      T = {self|all|id} systemId of target\n\
      F = [md]* flags, m = mute, d = deaf\n\
      D = duration in seconds (floating or \"inf\")\n\
:M    switch to writer M, where M is:\n\
        +N, -N   next, previous Nth writer of all non-automatic writers\n\
                 N defaults to 1\n\
        N        Nth writer of all non-automatic writers\n\
        NAME     unique writer of which topic name starts with NAME\n\
P     print get_discovered_participants result\n\
Q     quit - clean termination, same as EOF, SIGTERM or SIGINT\n\
)     (a closing parenthesis) kill pubsub itself with signal SIGKILL\n\
Note: for K*, OU types, in the above N is always a decimal\n\
integer (possibly negative); because the OneULong type has no key\n\
the actual key value is irrelevant for OU mode. For ARB types, N must be\n\
a valid initializer. X must always be a list of names.\n\
\n\
When invoked as \"sub\", default is -w0 (no writer)\n\
When invoked as \"pub\", default is -m0 (no reader)\n",
           argv0, qos_arg_usagestr);
  exit (3);
}

static void expand_append (char **dst, size_t *sz, size_t *pos, char c)
{
  if (*pos == *sz)
  {
    *sz += 1024;
    *dst = realloc (*dst, *sz);
  }
  (*dst)[*pos] = c;
  (*pos)++;
}

static char *expand_envvars (const char *src0);

static char *expand_env (const char *name, char op, const char *alt)
{
  const char *env = getenv (name);
  switch (op)
  {
    case 0:
      return strdup (env ? env : "");
    case '-':
      return env ? strdup (env) : expand_envvars (alt);
    case '?':
      if (env)
        return strdup (env);
      else
      {
        char *altx = expand_envvars (alt);
        error ("%s: %s\n", name, altx);
        free (altx);
        return NULL;
      }
    case '+':
      return env ? expand_envvars (alt) : strdup ("");
    default:
      abort ();
  }
}

static char *expand_envbrace (const char **src)
{
  const char *start = *src + 1;
  char *name, *x;
  assert (**src == '{');
  (*src)++;
  while (**src && **src != ':' && **src != '}')
    (*src)++;
  if (**src == 0)
    goto err;

  name = malloc ((size_t) (*src - start) + 1);
  memcpy (name, start, (size_t) (*src - start));
  name[*src - start] = 0;
  if (**src == '}')
  {
    (*src)++;
    x = expand_env (name, 0, NULL);
    free (name);
    return x;
  }
  else
  {
    const char *altstart;
    char *alt;
    char op;
    assert (**src == ':');
    (*src)++;
    switch (**src)
    {
      case '-': case '+': case '?':
        op = **src;
        (*src)++;
        break;
      default:
        goto err;
    }
    altstart = *src;
    while (**src && **src != '}')
    {
      if (**src == '\\')
      {
        (*src)++;
        if (**src == 0)
          goto err;
      }
      (*src)++;
    }
    if (**src == 0)
      goto err;
    assert (**src == '}');
    alt = malloc ((size_t) (*src - altstart) + 1);
    memcpy (alt, altstart, (size_t) (*src - altstart));
    alt[*src - altstart] = 0;
    (*src)++;
    x = expand_env (name, op, alt);
    free (alt);
    free (name);
    return x;
  }
 err:
  error ("%*.*s: invalid expansion\n", (int) (*src - start), (int) (*src - start), start);
  return NULL;
}

static char *expand_envsimple (const char **src)
{
  const char *start = *src;
  char *name, *x;
  while (**src && (isalnum ((unsigned char)**src) || **src == '_'))
    (*src)++;
  assert (*src > start);
  name = malloc ((size_t) (*src - start) + 1);
  memcpy (name, start, (size_t) (*src - start));
  name[*src - start] = 0;
  x = expand_env (name, 0, NULL);
  free (name);
  return x;
}

static char *expand_envchar (const char **src)
{
  char name[2];
  assert (**src);
  name[0] = **src;
  name[1] = 0;
  (*src)++;
  return expand_env (name, 0, NULL);
}

static char *expand_envvars (const char *src0)
{
  /* Expands $X, ${X}, ${X:-Y}, ${X:+Y}, ${X:?Y} forms */
  const char *src = src0;
  size_t sz = strlen (src) + 1, pos = 0;
  char *dst = malloc (sz);
  while (*src)
  {
    if (*src == '\\')
    {
      src++;
      if (*src == 0)
        error ("%s: incomplete escape at end of string\n", src0);
      expand_append (&dst, &sz, &pos, *src++);
    }
    else if (*src == '$')
    {
      char *x, *xp;
      src++;
      if (*src == 0)
      {
        error ("%s: incomplete variable expansion at end of string\n", src0);
        return NULL;
      }
      else if (*src == '{')
        x = expand_envbrace (&src);
      else if (isalnum ((unsigned char) *src) || *src == '_')
        x = expand_envsimple (&src);
      else
        x = expand_envchar (&src);
      xp = x;
      while (*xp)
        expand_append (&dst, &sz, &pos, *xp++);
      free (x);
    }
    else
    {
      expand_append (&dst, &sz, &pos, *src++);
    }
  }
  expand_append (&dst, &sz, &pos, 0);
  return dst;
}

static unsigned split_partitions (const char ***p_ps, char **p_bufcopy, const char *buf)
{
  const char *b;
  const char **ps;
  char *bufcopy, *bc;
  unsigned i, nps;
  nps = 1; for (b = buf; *b; b++) nps += (*b == ',');
  ps = malloc (nps * sizeof (*ps));
  bufcopy = expand_envvars (buf);
  i = 0; bc = bufcopy;
  while (1)
  {
    ps[i++] = bc;
    while (*bc && *bc != ',') bc++;
    if (*bc == 0) break;
    *bc++ = 0;
  }
  assert (i == nps);
  *p_ps = ps;
  *p_bufcopy = bufcopy;
  return nps;
}

static int set_pub_partition (DDS_Publisher pub, const char *buf)
{
  const char **ps;
  char *bufcopy;
  unsigned nps = split_partitions(&ps, &bufcopy, buf);
  DDS_ReturnCode_t rc;
  if ((rc = change_publisher_partitions (pub, nps, ps)) != DDS_RETCODE_OK)
    fprintf (stderr, "set_partition failed: %s (%d)\n", dds_strerror (rc), (int) rc);
  free (bufcopy);
  free (ps);
  return 0;
}

#if 0
static int set_sub_partition (DDS_Subscriber sub, const char *buf)
{
  const char **ps;
  char *bufcopy;
  unsigned nps = split_partitions(&ps, &bufcopy, buf);
  DDS_ReturnCode_t rc;
  if ((rc = change_subscriber_partitions (sub, nps, ps)) != DDS_RETCODE_OK)
    fprintf (stderr, "set_partition failed: %s (%d)\n", dds_strerror (rc), (int) rc);
  free (bufcopy);
  free (ps);
  return 0;
}
#endif

static void make_persistent_snapshot(const char *args)
{
  DDS_DomainId_t id = DDS_DomainParticipant_get_domain_id(dp);
  DDS_Domain dom;
  DDS_ReturnCode_t ret;
  char *p, *px = NULL, *tx = NULL, *uri = NULL;
  px = strdup(args);
  if ((p = strchr(px, ';')) == NULL) goto err;
  *p++ = 0;
  tx = p;
  if ((p = strchr(tx, ';')) == NULL) goto err;
  *p++ = 0;
  uri = p;
  if ((dom = DDS_DomainParticipantFactory_lookup_domain(dpf, id)) == NULL) {
    printf ("failed to lookup domain\n");
  } else {
    if ((ret = DDS_Domain_create_persistent_snapshot(dom, px, tx, uri)) != DDS_RETCODE_OK)
      printf ("failed to create persistent snapshot, error %d (%s)\n", (int) ret, dds_strerror(ret));
    if ((ret = DDS_DomainParticipantFactory_delete_domain(dpf, dom)) != DDS_RETCODE_OK)
      error ("failed to delete domain objet, error %d (%s)\n", (int) ret, dds_strerror(ret));
  }
  free(px);
  return;
err:
  printf ("%s: expected PART_EXPR;TOPIC_EXPR;URI\n", args);
  free(px);
}

static void instancehandle_to_id (uint32_t *systemId, uint32_t *localId, DDS_InstanceHandle_t h)
{
  /* Undocumented and unsupported trick */
  union { struct { uint32_t systemId, localId; } s; DDS_InstanceHandle_t h; } u;
  u.h = h;
  *systemId = u.s.systemId & ~0x80000000;
  *localId = u.s.localId;
}

static void do_ddsi_control(const char *args)
{
  q_osplserModule_ddsi_control x;
  const char *a = args;
  struct qos *qos;
  int pos;
  memset(&x, 0, sizeof(x));

  if (ddsi_control_topic == NULL) {
    /* If enabled, topic exists, or will exist very soon; if disabled, we don't want to wait
       for long. 100ms seems like a reasonable compromise. */
    DDS_Duration_t to = { 0, 100000000 };
    if ((ddsi_control_topic = find_topic(dp, "q_ddsiControl", &to)) == NULL) {
      printf ("DDS_DomainParticipant_find_topic(\"q_ddsiControl\") failed\n");
      return;
    }

  }
  if (strncmp(a, "self", 4) == 0) {
    uint32_t systemId, localId;
    instancehandle_to_id (&systemId, &localId, DDS_Entity_get_instance_handle (dp));
    x.systemId = systemId;
    a += 4;
  } else if (strncmp(a, "all", 3) == 0) {
    x.systemId = 0;
    a += 3;
  } else if (sscanf(a, "%u%n", &x.systemId, &pos) == 1) {
    a += pos;
  } else {
    printf ("ddsi control: invalid args: %s\n", args);
    return;
  }
  if (*a++ != ';') {
    printf ("ddsi control: invalid args: %s\n", args);
    return;
  }
  while (*a && *a != ';') {
    switch (*a++) {
      case 'm': x.mute = 1; break;
      case 'd': x.deaf = 1; break;
      default: printf ("ddsi control: invalid flags: %s\n", args); return;
    }
  }
  if (*a++ != ';') {
    printf ("ddsi control: invalid args: %s\n", args);
    return;
  }
  if (strcmp(a, "inf") == 0) {
    x.duration = 0.0;
  } else if (sscanf(a, "%lf%n", &x.duration, &pos) == 1 && a[pos] == 0) {
    if (x.duration <= 0.0) {
      printf ("ddsi control: invalid duration (<= 0): %s\n", args);
      return;
    }
  } else {
    printf ("ddsi control: invalid args: %s\n", args);
    return;
  }

  if (ddsi_control_wr == NULL)
  {
    qos = new_pubqos();
    qos_presentation(qos, "tnc");
    ddsi_control_pub = new_publisher1(qos, "__BUILT-IN PARTITION__");
    free_qos(qos);
    qos = new_wrqos(ddsi_control_pub, ddsi_control_topic);
    qos_autodispose_unregistered_instances(qos, "n");
    ddsi_control_wr = new_datawriter(qos);
    free_qos(qos);
  }

  q_osplserModule_ddsi_controlDataWriter_write(ddsi_control_wr, &x, DDS_HANDLE_NIL);
}

static int fd_getc (int fd)
{
  /* like fgetc, but also returning EOF when need to terminate */
  fd_set fds;
  int maxfd;
  int r;

  FD_ZERO(&fds);
  FD_SET(fd, &fds);
  FD_SET(termpipe[0], &fds);
  maxfd = (fd > termpipe[0]) ? fd : termpipe[0];

  while (1)
  {
    r = select(maxfd + 1, &fds, NULL, NULL, NULL);
    if ((r == -1 && errno == EINTR) || r == 0)
      continue;
    if (r == -1)
    {
      perror("fd_getc: select()");
      exit(2);
    }

    if (FD_ISSET(termpipe[0], &fds))
    {
      return EOF;
    }
    if (FD_ISSET(fd, &fds))
    {
      unsigned char c;
      ssize_t n = read (fd, &c, 1);
      if (n == 1)
        return c;
      else if (n == 0)
        return EOF;
      else if (errno != EINTR)
      {
        perror("fd_getc: read()");
        exit(2);
      }
      /* else try again */
    }
  }
}

static int read_int (int fd, char *buf, int bufsize, int pos, int accept_minus)
{
  int c = EOF;
  while (pos < bufsize-1 && (c = fd_getc (fd)) != EOF && (isdigit ((unsigned char) c) || (c == '-' && accept_minus)))
  {
    accept_minus = 0;
    buf[pos++] = (char) c;
  }
  buf[pos] = 0;
  if (c == EOF || isspace ((unsigned char) c))
    return (pos > 0);
  else if (!isdigit ((unsigned char) c))
  {
    fprintf (stderr, "%c: unexpected character\n", c);
    return 0;
  }
  else if (pos == bufsize-1)
  {
    fprintf (stderr, "integer too long\n");
    return 0;
  }
  return 1;
}

static int read_int_w_tstamp (struct tstamp_t *tstamp, int fd, char *buf, int bufsize, int pos)
{
  int c;
  assert (pos < bufsize - 2);
  c = fd_getc (fd);
  if (c == EOF)
    return 0;
  else if (c == '@')
  {
    int posoff = 0;
    c = fd_getc (fd);
    if (c == EOF)
      return 0;
    else if (c == '=')
      tstamp->isabs = 1;
    else
    {
      buf[pos] = (char) c;
      posoff = 1;
    }
    if (read_int (fd, buf, bufsize, pos + posoff, 1))
      tstamp->t = atoi (buf + pos) * T_SECOND;
    else
      return 0;
    while ((c = fd_getc (fd)) != EOF && isspace ((unsigned char) c))
      ;
    if (!isdigit ((unsigned char) c))
      return 0;
  }
  buf[pos++] = (char) c;
  while (pos < bufsize-1 && (c = fd_getc (fd)) != EOF && isdigit ((unsigned char) c))
    buf[pos++] = (char) c;
  buf[pos] = 0;
  if (c == EOF || isspace ((unsigned char) c))
    return (pos > 0);
  else if (!isdigit ((unsigned char) c))
  {
    fprintf (stderr, "%c: unexpected character\n", c);
    return 0;
  }
  else if (pos == bufsize-1)
  {
    fprintf (stderr, "integer too long\n");
    return 0;
  }
  return 1;
}

static int read_value (int fd, char *command, int *key, struct tstamp_t *tstamp, char **arg)
{
  char buf[1024];
  int c;
  if (*arg) { free(*arg); *arg = NULL; }
  tstamp->isabs = 0;
  tstamp->t = 0;
  do {
    while ((c = fd_getc (fd)) != EOF && isspace ((unsigned char) c))
      ;
    if (c == EOF)
      return 0;
    switch (c)
    {
      case '-':
      case '0': case '1': case '2': case '3': case '4':
      case '5': case '6': case '7': case '8': case '9':
        buf[0] = (char) c;
        if (read_int (fd, buf, sizeof (buf), 1, 0))
        {
          *command = 'w';
          *key = atoi (buf);
          return 1;
        }
        break;
      case 'w': case 'd': case 'D': case 'u': case 'r':
        *command = (char) c;
        if (read_int_w_tstamp (tstamp, fd, buf, sizeof (buf), 0))
        {
          *key = atoi (buf);
          return 1;
        }
        break;
      case 'z': case 's': case 'n':
        *command = (char) c;
        if (read_int (fd, buf, sizeof (buf), 0, 0))
        {
          *key = atoi (buf);
          return 1;
        }
        break;
      case 'p': case 'S': case 'C': case ':': {
        int i = 0;
        *command = (char) c;
        while ((c = fd_getc (fd)) != EOF && !isspace ((unsigned char) c))
        {
          assert (i < (int) sizeof (buf) - 1);
          buf[i++] = (char) c;
        }
        buf[i] = 0;
        *arg = strdup(buf);
        ungetc (c, stdin);
        return 1;
      }
      case 'Y': case 'B': case 'E': case 'W': case ')': case 'Q':
        *command = (char) c;
        return 1;
      default:
        fprintf (stderr, "'%c': unexpected character\n", c);
        break;
    }
    while ((c = fd_getc (fd)) != EOF && !isspace ((unsigned char) c))
      ;
  } while (c != EOF);
  return 0;
}

static char *getl_simple (int fd, int *count)
{
  size_t sz = 0, n = 0;
  char *line;
  int c;

  if ((c = fd_getc(fd)) == EOF)
  {
    *count = 0;
    return NULL;
  }

  line = NULL;
  do {
    if (n == sz) line = realloc(line, sz += 256);
    line[n++] = (char) c;
  } while ((c = fd_getc (fd)) != EOF && c != '\n');
  if (n == sz) line = realloc(line, sz += 256);
  line[n++] = 0;
  *count = (int) (n-1);
  return line;
}

struct getl_arg {
  int use_editline;
  union {
#if USE_EDITLINE
    struct {
      FILE *el_fp;
      EditLine *el;
      History *hist;
      HistEvent ev;
    } el;
#endif
    struct {
      int fd;
      char *lastline;
    } s;
  } u;
};

static void getl_init_simple (struct getl_arg *arg, int fd)
{
  arg->use_editline = 0;
  arg->u.s.fd = fd;
  arg->u.s.lastline = NULL;
}

#if USE_EDITLINE
static int el_getc_wrapper (EditLine *el, char *c)
{
  void *fd;
  int in;
  el_get(el, EL_CLIENTDATA, &fd);
  in = fd_getc(*(int *)fd);
  if (in == EOF)
    return 0;
  else {
    *c = (char) in;
    return 1;
  }
}

static const char *prompt (EditLine *el __attribute__ ((unused)))
{
  return "";
}

static void getl_init_editline (struct getl_arg *arg, int fd)
{
  if (isatty (fdin))
  {
    arg->use_editline = 1;
    arg->u.el.el_fp = fdopen(fd, "r");
    arg->u.el.hist = history_init();
    history(arg->u.el.hist, &arg->u.el.ev, H_SETSIZE, 800);
    arg->u.el.el = el_init("pubsub", arg->u.el.el_fp, stdout, stderr);
    el_source(arg->u.el.el, NULL);
    el_set(arg->u.el.el, EL_EDITOR, "emacs");
    el_set(arg->u.el.el, EL_PROMPT, prompt);
    el_set(arg->u.el.el, EL_SIGNAL, 1);
    el_set(arg->u.el.el, EL_CLIENTDATA, &fdin);
    el_set(arg->u.el.el, EL_GETCFN, el_getc_wrapper);
    el_set(arg->u.el.el, EL_HIST, history, arg->u.el.hist);
  }
  else
  {
    getl_init_simple(arg, fd);
  }
}
#endif

static void getl_reset_input (struct getl_arg *arg, int fd)
{
  assert (!arg->use_editline);
  arg->u.s.fd = fd;
}

static void getl_fini (struct getl_arg *arg)
{
  if (arg->use_editline)
  {
#if USE_EDITLINE
    el_end(arg->u.el.el);
    history_end(arg->u.el.hist);
    fclose(arg->u.el.el_fp);
#endif
  }
  else
  {
    free(arg->u.s.lastline);
  }
}

static const char *getl (struct getl_arg *arg, int *count)
{
  if (arg->use_editline)
  {
#if USE_EDITLINE
    return el_gets(arg->u.el.el, count);
#else
    abort();
    return NULL;
#endif
  }
  else
  {
    free (arg->u.s.lastline);
    return arg->u.s.lastline = getl_simple (arg->u.s.fd, count);
  }
}

static void getl_enter_hist(struct getl_arg *arg, const char *line)
{
#if USE_EDITLINE
  if (arg->use_editline)
    history(arg->u.el.hist, &arg->u.el.ev, H_ENTER, line);
#endif
}

static char si2isc (const DDS_SampleInfo *si)
{
  switch (si->instance_state)
  {
    case DDS_ALIVE_INSTANCE_STATE: return 'A';
    case DDS_NOT_ALIVE_DISPOSED_INSTANCE_STATE: return 'D';
    case DDS_NOT_ALIVE_NO_WRITERS_INSTANCE_STATE: return 'U';
    default: return '?';
  }
}

static char si2ssc (const DDS_SampleInfo *si)
{
  switch (si->sample_state)
  {
    case DDS_READ_SAMPLE_STATE: return 'R';
    case DDS_NOT_READ_SAMPLE_STATE: return 'N';
    default: return '?';
  }
}

static char si2vsc (const DDS_SampleInfo *si)
{
  switch (si->view_state)
  {
    case DDS_NEW_VIEW_STATE: return 'N';
    case DDS_NOT_NEW_VIEW_STATE: return 'O';
    default: return '?';
  }
}

static DDS_ReturnCode_t getkeyval_KS (KeyedSeqDataReader rd, int32_t *key, DDS_InstanceHandle_t ih)
{
  DDS_ReturnCode_t result;
  KeyedSeq d_key;
  if ((result = KeyedSeqDataReader_get_key_value (rd, &d_key, ih)) == DDS_RETCODE_OK)
    *key = d_key.keyval;
  else
    *key = 0;
  return result;
}

static DDS_ReturnCode_t getkeyval_K32 (Keyed32DataReader rd, int32_t *key, DDS_InstanceHandle_t ih)
{
  DDS_ReturnCode_t result;
  Keyed32 d_key;
  if ((result = Keyed32DataReader_get_key_value (rd, &d_key, ih)) == DDS_RETCODE_OK)
    *key = d_key.keyval;
  else
    *key = 0;
  return result;
}

static DDS_ReturnCode_t getkeyval_K64 (Keyed64DataReader rd, int32_t *key, DDS_InstanceHandle_t ih)
{
  DDS_ReturnCode_t result;
  Keyed64 d_key;
  if ((result = Keyed64DataReader_get_key_value (rd, &d_key, ih)) == DDS_RETCODE_OK)
    *key = d_key.keyval;
  else
    *key = 0;
  return result;
}

static DDS_ReturnCode_t getkeyval_K128 (Keyed128DataReader rd, int32_t *key, DDS_InstanceHandle_t ih)
{
  DDS_ReturnCode_t result;
  Keyed128 d_key;
  if ((result = Keyed128DataReader_get_key_value (rd, &d_key, ih)) == DDS_RETCODE_OK)
    *key = d_key.keyval;
  else
    *key = 0;
  return result;
}

static DDS_ReturnCode_t getkeyval_K256 (Keyed256DataReader rd, int32_t *key, DDS_InstanceHandle_t ih)
{
  DDS_ReturnCode_t result;
  Keyed256 d_key;
  if ((result = Keyed256DataReader_get_key_value (rd, &d_key, ih)) == DDS_RETCODE_OK)
    *key = d_key.keyval;
  else
    *key = 0;
  return result;
}

static int print_sampleinfo (unsigned long long *tstart, unsigned long long tnow, const DDS_SampleInfo *si, const char *tag)
{
  unsigned long long relt;
  uint32_t phSystemId, phLocalId, ihSystemId, ihLocalId;
  char isc = si2isc (si), ssc = si2ssc (si), vsc = si2vsc (si);
  const char *sep;
  int n = 0;
  if (*tstart == 0)
    *tstart = tnow;
  relt = tnow - *tstart;
  instancehandle_to_id(&ihSystemId, &ihLocalId, si->instance_handle);
  instancehandle_to_id(&phSystemId, &phLocalId, si->publication_handle);
  sep = "";
  if (print_metadata & PM_PID)
    n += printf ("%d", pid);
  if (print_metadata & PM_TOPIC)
    n += printf ("%s", tag);
  if (print_metadata & PM_TIME)
    n += printf ("%s%u.%09u", n > 0 ? " " : "", (unsigned) (relt / 1000000000), (unsigned) (relt % 1000000000));
  sep = " : ";
  if (print_metadata & PM_PHANDLE) {
    n += printf ("%s%" PRIx32 ":%" PRIx32, n > 0 ? sep : "", phSystemId, phLocalId); sep = " ";
  }
  if (print_metadata & PM_IHANDLE) {
    n += printf ("%s%" PRIx32 ":%" PRIx32, n > 0 ? sep : "", ihSystemId, ihLocalId);
  }
  sep = " : ";
  if (print_metadata & PM_STIME) {
    n += printf ("%s%u.%09u", n > 0 ? sep : "", si->source_timestamp.sec, si->source_timestamp.nanosec);
    sep = " ";
  }
  if (print_metadata & PM_RTIME) {
    n += printf ("%s%u.%09u", n > 0 ? sep : "", si->reception_timestamp.sec, si->reception_timestamp.nanosec);
  }
  sep = " : ";
  if (print_metadata & PM_DGEN) {
    n += printf ("%s%d", n > 0 ? sep : "", si->disposed_generation_count);
    sep = " ";
  }
  if (print_metadata & PM_NWGEN) {
    n += printf ("%s%d", n > 0 ? sep : "", si->no_writers_generation_count);
    sep = " ";
  }
  sep = " : ";
  if (print_metadata & PM_RANKS) {
    n += printf ("%s%d %d %d", n > 0 ? sep : "", si->sample_rank, si->generation_rank, si->absolute_generation_rank);
    sep = " ";
  }
  sep = " : ";
  if (print_metadata & PM_STATE) {
    n += printf ("%s%c%c%c", n > 0 ? sep : "", isc, ssc, vsc);
    sep = " ";
  }
  return (n > 0);
}

static void print_K (unsigned long long *tstart, unsigned long long tnow, DDS_DataReader rd, const char *tag, const DDS_SampleInfo *si, int32_t keyval, uint32_t seq, DDS_ReturnCode_t (*getkeyval) (DDS_DataReader rd, int32_t *key, DDS_InstanceHandle_t ih))
{
  flockfile(stdout);
  if (print_sampleinfo(tstart, tnow, si, tag))
    printf(" : ");
  if (si->valid_data)
    printf ("%u %d\n", seq, keyval);
  else
  {
    /* May not look at mseq->_buffer[i] but want the key value
       nonetheless.  Bummer.  Actually this leads to an interesting
       problem: if the instance is in the NOT_ALIVE state and the
       middleware releases all resources related to the instance
       after our taking the sample, get_key_value _will_ fail.  So
       the blanket statement "may not look at value" if valid_data
       is not set means you can't really use take ...  */
#if 1
    (void)rd;
    (void)getkeyval;
    printf ("NA %u\n", keyval);
#else
    DDS_ReturnCode_t result;
    int32_t d_key;
    if ((result = getkeyval (rd, &d_key, si->instance_handle)) == DDS_RETCODE_OK)
      printf ("NA %u\n", d_key);
    else
      printf ("get_key_value: error %d (%s)\n", (int) result, dds_strerror (result));
#endif
  }
  funlockfile(stdout);
}

static void print_seq_KS (unsigned long long *tstart, unsigned long long tnow, KeyedSeqDataReader rd, const char *tag, const DDS_SampleInfoSeq *iseq, DDS_sequence_KeyedSeq *mseq)
{
  unsigned i;
  for (i = 0; i < mseq->_length; i++)
    print_K (tstart, tnow, rd, tag, &iseq->_buffer[i], mseq->_buffer[i].keyval, mseq->_buffer[i].seq, getkeyval_KS);
}

static void print_seq_K32 (unsigned long long *tstart, unsigned long long tnow, Keyed32DataReader rd, const char *tag, const DDS_SampleInfoSeq *iseq, DDS_sequence_Keyed32 *mseq)
{
  unsigned i;
  for (i = 0; i < mseq->_length; i++)
    print_K (tstart, tnow, rd, tag, &iseq->_buffer[i], mseq->_buffer[i].keyval, mseq->_buffer[i].seq, getkeyval_K32);
}

static void print_seq_K64 (unsigned long long *tstart, unsigned long long tnow, Keyed64DataReader rd, const char *tag, const DDS_SampleInfoSeq *iseq, DDS_sequence_Keyed64 *mseq)
{
  unsigned i;
  for (i = 0; i < mseq->_length; i++)
    print_K (tstart, tnow, rd, tag, &iseq->_buffer[i], mseq->_buffer[i].keyval, mseq->_buffer[i].seq, getkeyval_K64);
}

static void print_seq_K128 (unsigned long long *tstart, unsigned long long tnow, Keyed128DataReader rd, const char *tag, const DDS_SampleInfoSeq *iseq, DDS_sequence_Keyed128 *mseq)
{
  unsigned i;
  for (i = 0; i < mseq->_length; i++)
    print_K (tstart, tnow, rd, tag, &iseq->_buffer[i], mseq->_buffer[i].keyval, mseq->_buffer[i].seq, getkeyval_K128);
}

static void print_seq_K256 (unsigned long long *tstart, unsigned long long tnow, Keyed256DataReader rd, const char *tag, const DDS_SampleInfoSeq *iseq, DDS_sequence_Keyed256 *mseq)
{
  unsigned i;
  for (i = 0; i < mseq->_length; i++)
    print_K (tstart, tnow, rd, tag, &iseq->_buffer[i], mseq->_buffer[i].keyval, mseq->_buffer[i].seq, getkeyval_K256);
}

static void print_seq_OU (unsigned long long *tstart, unsigned long long tnow, OneULongDataReader rd __attribute__ ((unused)), const char *tag, const DDS_SampleInfoSeq *iseq, const DDS_sequence_OneULong *mseq)
{
  unsigned i;
  for (i = 0; i < mseq->_length; i++)
  {
    DDS_SampleInfo const * const si = &iseq->_buffer[i];
    flockfile(stdout);
    if (print_sampleinfo(tstart, tnow, si, tag))
      printf(" : ");
    if (si->valid_data) {
      OneULong *d = &mseq->_buffer[i];
      printf ("%u\n", d->seq);
    } else {
      printf ("NA\n");
    }
    funlockfile(stdout);
  }
}

static void print_seq_ARB (unsigned long long *tstart, unsigned long long tnow, DDS_DataReader rd __attribute__ ((unused)), const char *tag, const DDS_SampleInfoSeq *iseq, const DDS_sequence_octet *mseq, const struct tgtopic *tgtp)
{
  unsigned i;
  for (i = 0; i < mseq->_length; i++)
  {
    DDS_SampleInfo const * const si = &iseq->_buffer[i];
    struct tgstring str;
    tgstring_init(&str, print_chop);
    flockfile(stdout);
    if (print_sampleinfo(tstart, tnow, si, tag) && print_chop > 0)
      printf(" : ");
    if (si->valid_data)
      (void)tgprint(&str, tgtp, (char *) mseq->_buffer + i * tgtp->size, print_mode);
    else
      (void)tgprintkey(&str, tgtp, (char *) mseq->_buffer + i * tgtp->size, print_mode);
    printf("%s\n", str.buf);
    funlockfile(stdout);
    tgstring_fini(&str);
  }
}

static void rd_on_liveliness_changed (void *listener_data __attribute__ ((unused)), DDS_DataReader rd __attribute__ ((unused)), const DDS_LivelinessChangedStatus *status)
{
  uint32_t systemId, localId;
  instancehandle_to_id(&systemId, &localId, status->last_publication_handle);
  printf ("[liveliness-changed: alive=(%d change %d) not_alive=(%d change %d) handle=%" PRIx32 ":%" PRIx32 "]\n",
          status->alive_count, status->alive_count_change,
          status->not_alive_count, status->not_alive_count_change,
          systemId, localId);
}

static void rd_on_sample_lost (void *listener_data __attribute__ ((unused)), DDS_DataReader rd __attribute__ ((unused)), const DDS_SampleLostStatus *status)
{
  printf ("[sample-lost: total=(%d change %d)]\n", status->total_count, status->total_count_change);
}

static void rd_on_sample_rejected (void *listener_data __attribute__ ((unused)), DDS_DataReader rd __attribute__ ((unused)), const DDS_SampleRejectedStatus *status)
{
  const char *reasonstr = "?";
  switch (status->last_reason)
  {
    case DDS_NOT_REJECTED: reasonstr = "not_rejected"; break;
    case DDS_REJECTED_BY_INSTANCES_LIMIT: reasonstr = "instances"; break;
    case DDS_REJECTED_BY_SAMPLES_LIMIT: reasonstr = "samples"; break;
    case DDS_REJECTED_BY_SAMPLES_PER_INSTANCE_LIMIT: reasonstr = "samples_per_instance"; break;
  }
  printf ("[sample-rejected: total=(%d change %d) reason=%s handle=%lld]\n",
          status->total_count, status->total_count_change,
          reasonstr,
          status->last_instance_handle);
}

static void rd_on_subscription_matched (void *listener_data __attribute__((unused)), DDS_DataReader rd __attribute__((unused)), const DDS_SubscriptionMatchedStatus *status)
{
  uint32_t systemId, localId;
  instancehandle_to_id(&systemId, &localId, status->last_publication_handle);
  printf ("[subscription-matched: total=(%d change %d) current=(%d change %d) handle=%" PRIx32 ":%" PRIx32 "]\n",
          status->total_count, status->total_count_change,
          status->current_count, status->current_count_change,
          systemId, localId);
}

static void rd_on_requested_deadline_missed (void *listener_data __attribute__((unused)), DDS_DataReader rd __attribute__((unused)), const DDS_RequestedDeadlineMissedStatus *status)
{
  uint32_t systemId, localId;
  instancehandle_to_id(&systemId, &localId, status->last_instance_handle);
  printf ("[requested-deadline-missed: total=(%d change %d) handle=%" PRIx32 ":%" PRIx32 "]\n",
          status->total_count, status->total_count_change,
          systemId, localId);
}

static const char *policystr (DDS_QosPolicyId_t id)
{
  switch (id)
  {
    case DDS_USERDATA_QOS_POLICY_ID: return DDS_USERDATA_QOS_POLICY_NAME;
    case DDS_DURABILITY_QOS_POLICY_ID: return DDS_DURABILITY_QOS_POLICY_NAME;
    case DDS_PRESENTATION_QOS_POLICY_ID: return DDS_PRESENTATION_QOS_POLICY_NAME;
    case DDS_DEADLINE_QOS_POLICY_ID: return DDS_DEADLINE_QOS_POLICY_NAME;
    case DDS_LATENCYBUDGET_QOS_POLICY_ID: return DDS_LATENCYBUDGET_QOS_POLICY_NAME;
    case DDS_OWNERSHIP_QOS_POLICY_ID: return DDS_OWNERSHIP_QOS_POLICY_NAME;
    case DDS_OWNERSHIPSTRENGTH_QOS_POLICY_ID: return DDS_OWNERSHIPSTRENGTH_QOS_POLICY_NAME;
    case DDS_LIVELINESS_QOS_POLICY_ID: return DDS_LIVELINESS_QOS_POLICY_NAME;
    case DDS_TIMEBASEDFILTER_QOS_POLICY_ID: return DDS_TIMEBASEDFILTER_QOS_POLICY_NAME;
    case DDS_PARTITION_QOS_POLICY_ID: return DDS_PARTITION_QOS_POLICY_NAME;
    case DDS_RELIABILITY_QOS_POLICY_ID: return DDS_RELIABILITY_QOS_POLICY_NAME;
    case DDS_DESTINATIONORDER_QOS_POLICY_ID: return DDS_DESTINATIONORDER_QOS_POLICY_NAME;
    case DDS_HISTORY_QOS_POLICY_ID: return DDS_HISTORY_QOS_POLICY_NAME;
    case DDS_RESOURCELIMITS_QOS_POLICY_ID: return DDS_RESOURCELIMITS_QOS_POLICY_NAME;
    case DDS_ENTITYFACTORY_QOS_POLICY_ID: return DDS_ENTITYFACTORY_QOS_POLICY_NAME;
    case DDS_WRITERDATALIFECYCLE_QOS_POLICY_ID: return DDS_WRITERDATALIFECYCLE_QOS_POLICY_NAME;
    case DDS_READERDATALIFECYCLE_QOS_POLICY_ID: return DDS_READERDATALIFECYCLE_QOS_POLICY_NAME;
    case DDS_TOPICDATA_QOS_POLICY_ID: return DDS_TOPICDATA_QOS_POLICY_NAME;
    case DDS_GROUPDATA_QOS_POLICY_ID: return DDS_GROUPDATA_QOS_POLICY_NAME;
    case DDS_TRANSPORTPRIORITY_QOS_POLICY_ID: return DDS_TRANSPORTPRIORITY_QOS_POLICY_NAME;
    case DDS_LIFESPAN_QOS_POLICY_ID: return DDS_LIFESPAN_QOS_POLICY_NAME;
    case DDS_DURABILITYSERVICE_QOS_POLICY_ID: return DDS_DURABILITYSERVICE_QOS_POLICY_NAME;
    case DDS_SUBSCRIPTIONKEY_QOS_POLICY_ID: return DDS_SUBSCRIPTIONKEY_QOS_POLICY_NAME;
    case DDS_VIEWKEY_QOS_POLICY_ID: return DDS_VIEWKEY_QOS_POLICY_NAME;
    case DDS_READERLIFESPAN_QOS_POLICY_ID: return DDS_READERLIFESPAN_QOS_POLICY_NAME;
    case DDS_SHARE_QOS_POLICY_ID: return DDS_SHARE_QOS_POLICY_NAME;
    case DDS_SCHEDULING_QOS_POLICY_ID: return DDS_SCHEDULING_QOS_POLICY_NAME;
    default: return "?";
  }
}

static void format_policies (char *polstr, size_t polsz, const DDS_QosPolicyCount *xs, unsigned nxs)
{
  char *ps = polstr;
  unsigned i;
  for (i = 0; i < nxs && ps < polstr + polsz; i++)
  {
    const DDS_QosPolicyCount *x = &xs[i];
    int n = snprintf (ps, polstr + polsz - ps, "%s%s:%d", i == 0 ? "" : ", ", policystr(x->policy_id), x->count);
    ps += n;
  }
}

static void rd_on_requested_incompatible_qos (void *listener_data __attribute__((unused)), DDS_DataReader rd __attribute__((unused)), const DDS_RequestedIncompatibleQosStatus *status)
{
  char polstr[1024] = "";
  format_policies(polstr, sizeof (polstr), status->policies._buffer, status->policies._length);
  printf ("[requested-incompatible-qos: total=(%d change %d) last_policy=%s {%s}]\n",
          status->total_count, status->total_count_change,
          policystr(status->last_policy_id), polstr);
}

static void wr_on_offered_incompatible_qos (void *listener_data __attribute__((unused)), DDS_DataWriter wr __attribute__((unused)), const DDS_OfferedIncompatibleQosStatus *status)
{
  char polstr[1024] = "";
  format_policies(polstr, sizeof (polstr), status->policies._buffer, status->policies._length);
  printf ("[offered-incompatible-qos: total=(%d change %d) last_policy=%s {%s}]\n",
          status->total_count, status->total_count_change,
          policystr(status->last_policy_id), polstr);
}

static void wr_on_liveliness_lost (void *listener_data __attribute__((unused)), DDS_DataWriter wr __attribute__((unused)), const DDS_LivelinessLostStatus *status)
{
  printf ("[liveliness-lost: total=(%d change %d)]\n",
          status->total_count, status->total_count_change);
}

static void wr_on_offered_deadline_missed (void *listener_data __attribute__((unused)), DDS_DataWriter wr __attribute__((unused)), const DDS_OfferedDeadlineMissedStatus *status)
{
  printf ("[offered-deadline-missed: total=(%d change %d) handle=%lld]\n",
          status->total_count, status->total_count_change, status->last_instance_handle);
}

static void wr_on_publication_matched (void *listener_data __attribute__((unused)), DDS_DataWriter wr __attribute__((unused)), const DDS_PublicationMatchedStatus *status)
{
  uint32_t systemId, localId;
  instancehandle_to_id(&systemId, &localId, status->last_subscription_handle);
  printf ("[publication-matched: total=(%d change %d) current=(%d change %d) handle=%" PRIx32 ":%" PRIx32 "]\n",
          status->total_count, status->total_count_change,
          status->current_count, status->current_count_change,
          systemId, localId);
}

static int w_accept(int fd)
{
  fd_set fds;
  int maxfd;
  struct sockaddr_in saddr;
  socklen_t saddrlen = sizeof (saddr);
  int r;

  FD_ZERO(&fds);
  FD_SET(fd, &fds);
  FD_SET(termpipe[0], &fds);
  maxfd = (fd > termpipe[0]) ? fd : termpipe[0];

  while (1)
  {
    r = select(maxfd + 1, &fds, NULL, NULL, NULL);
   if ((r == -1 && errno == EINTR) || r == 0)
     continue;
    if (r == -1)
    {
      perror("w_accept: select()");
      exit(2);
    }

    if (FD_ISSET(termpipe[0], &fds))
    {
      return -1;
    }
    if (FD_ISSET(fd, &fds))
    {
      int fdacc;
      if ((fdacc = accept (fd, (struct sockaddr *) &saddr, &saddrlen)) == -1)
      {
        if (errno != EINTR)
        {
          perror ("accept()");
          exit (2);
        }
      }
      if (print_tcp)
        printf ("to %s\n", inet_ntoa (saddr.sin_addr));
      return fdacc;
    }
  }
}

static DDS_ReturnCode_t register_instance_wrapper (DDS_DataWriter wr, void *d, DDS_InstanceHandle_t h, const DDS_Time_t *tstamp)
{
  (void) h;
  return (DDS_DataWriter_register_instance_w_timestamp(wr, d, tstamp) == DDS_HANDLE_NIL) ? DDS_RETCODE_ERROR : DDS_RETCODE_OK;
}

static write_oper_t get_write_oper(char command)
{
  switch (command)
  {
    case 'w': return DDS_DataWriter_write_w_timestamp;
    case 'd': return DDS_DataWriter_dispose_w_timestamp;
    case 'D': return DDS_DataWriter_writedispose_w_timestamp;
    case 'u': return DDS_DataWriter_unregister_instance_w_timestamp;
    case 'r': return register_instance_wrapper;
    default:  return 0;
  }
}

static const char *get_write_operstr(char command)
{
  switch (command)
  {
    case 'w': return "write";
    case 'd': return "dispose";
    case 'D': return "writedispose";
    case 'u': return "unregister_instance";
    case 'r': return "register_instance";
    default:  return 0;
  }
}

static void non_data_operation(char command, DDS_DataWriter wr)
{
  DDS_ReturnCode_t result;
  switch (command)
  {
    case 'Y':
      if ((result = DDS_Topic_dispose_all_data (DDS_DataWriter_get_topic (wr))) != DDS_RETCODE_OK)
        error ("DDS_Topic_dispose_all: error %d\n", (int) result);
      break;
    case 'B':
      if ((result = DDS_Publisher_begin_coherent_changes (DDS_DataWriter_get_publisher (wr))) != DDS_RETCODE_OK)
        error ("DDS_Publisher_begin_coherent_changes: error %d\n", (int) result);
      break;
    case 'E':
      if ((result = DDS_Publisher_end_coherent_changes (DDS_DataWriter_get_publisher (wr))) != DDS_RETCODE_OK)
        error ("DDS_Publisher_end_coherent_changes: error %d\n", (int) result);
      break;
    case 'W': {
      DDS_Duration_t inf = DDS_DURATION_INFINITE;
      if ((result = DDS_DataWriter_wait_for_acknowledgments(wr, &inf)) != DDS_RETCODE_OK)
        error ("DDS_Publisher_wait_for_acknowledgements: error %d\n", (int) result);
      break;
    }
    case 'P': {
      DDS_Subscriber sub = DDS_DomainParticipant_get_builtin_subscriber (dp);
      DDS_DataReader rd = DDS_Subscriber_lookup_datareader (sub, "DCPSParticipant");
      DDS_Subscriber_delete_datareader (sub, rd);
      DDS_InstanceHandleSeq *hs = DDS_InstanceHandleSeq__alloc ();
      DDS_ReturnCode_t rc = DDS_DomainParticipant_get_discovered_participants (dp, hs);
      if (rc != DDS_RETCODE_OK)
        printf ("X %d :(\n", rc);
      for (int i = 0; i < (int) hs->_length; i++)
        printf ("X %llx\n", hs->_buffer[i]);
      DDS_free (hs);
      break;
    }
    case 'Q':
      terminate();
      break;
    case ')':
      kill (getpid (), SIGKILL);
      break;
    default:
      abort();
  }
}

static char *skipspaces (const char *s)
{
  while (*s && isspace((unsigned char) *s))
    s++;
  return (char *) s;
}

static int accept_error (char command, DDS_ReturnCode_t retcode)
{
  if (retcode == DDS_RETCODE_TIMEOUT)
    return 1;
  if ((command == 'd' || command == 'u') && retcode == DDS_RETCODE_PRECONDITION_NOT_MET)
    return 1;
  return 0;
}

union data {
  uint32_t seq;
  struct { uint32_t seq; int32_t keyval; } seq_keyval;
  KeyedSeq ks;
  Keyed32 k32;
  Keyed64 k64;
  Keyed128 k128;
  Keyed256 k256;
  OneULong ou;
};

static void pub_do_auto (const struct writerspec *spec)
{
  DDS_ReturnCode_t result;
  DDS_InstanceHandle_t handle[nkeyvals];
  uint64_t ntot = 0, tfirst, tlast, tprev, tfirst0, tstop;
  struct hist *hist = hist_new (30, 1000, 0);
  int k = 0;
  union data d;
  memset (&d, 0, sizeof (d));
  switch (spec->topicsel)
  {
    case UNSPEC:
      assert(0);
    case KS:
      d.ks.baggage._maximum = d.ks.baggage._length = spec->baggagesize;
      d.ks.baggage._buffer = DDS_sequence_octet_allocbuf (spec->baggagesize);
      memset (d.ks.baggage._buffer, 0xee, spec->baggagesize);
      break;
    case K32:
      memset (d.k32.baggage, 0xee, sizeof (d.k32.baggage));
      break;
    case K64:
      memset (d.k64.baggage, 0xee, sizeof (d.k64.baggage));
      break;
    case K128:
      memset (d.k128.baggage, 0xee, sizeof (d.k128.baggage));
      break;
    case K256:
      memset (d.k256.baggage, 0xee, sizeof (d.k256.baggage));
      break;
    case OU:
      break;
    case ARB:
      assert (!(fdin == -1 && fdservsock == -1));
      break;
  }
  assert (nkeyvals > 0);
  for (k = 0; (uint32_t) k < nkeyvals; k++)
  {
    d.seq_keyval.keyval = k;
    handle[k] = spec->register_instances ? DDS_DataWriter_register_instance (spec->wr, &d) : DDS_HANDLE_NIL;
  }
  sleep (1);
  d.seq_keyval.keyval = 0;
  tfirst0 = tfirst = tprev = nowll ();
  if (dur != 0.0)
    tstop = tfirst0 + (unsigned long long) (1e9 * dur);
  else
    tstop = UINT64_MAX;
  if (nkeyvals == 0)
  {
    while (!termflag && tprev < tstop)
    {
      struct timespec delay;
      delay.tv_sec = 0;
      delay.tv_nsec = 100 * 1000 * 1000;
      nanosleep (&delay, NULL);
    }
  }
  else if (spec->writerate <= 0)
  {
    while (!termflag && tprev < tstop)
    {
      if ((result = DDS_DataWriter_write (spec->wr, &d, handle[d.seq_keyval.keyval])) != DDS_RETCODE_OK)
      {
        printf ("write: error %d (%s)\n", (int) result, dds_strerror (result));
        if (result != DDS_RETCODE_TIMEOUT)
          break;
      }
      else
      {
        d.seq_keyval.keyval = (d.seq_keyval.keyval + 1) % (int32_t)nkeyvals;
        d.seq++;
        ntot++;
        if ((d.seq % 16) == 0)
        {
          unsigned long long t = nowll ();
          hist_record (hist, (t - tprev) / 16, 16);
          if (t < tfirst + 4 * 1000000000ll)
            tprev = t;
          else
          {
            tlast = t;
            hist_print (hist, tlast - tfirst, 1);
            tfirst = tprev;
            tprev = nowll ();
          }
        }
      }
    }
  }
  else
  {
    unsigned bi = 0;
    while (!termflag && tprev < tstop)
    {
      if ((result = DDS_DataWriter_write (spec->wr, &d, handle[d.seq_keyval.keyval])) != DDS_RETCODE_OK)
      {
        printf ("write: error %d (%s)\n", (int) result, dds_strerror (result));
        if (result != DDS_RETCODE_TIMEOUT)
          break;
      }

      {
        unsigned long long t = nowll ();
        d.seq_keyval.keyval = (d.seq_keyval.keyval + 1) % (int32_t)nkeyvals;
        d.seq++;
        ntot++;
        hist_record (hist, t - tprev, 1);
        if (t >= tfirst + 4 * 1000000000ll)
        {
          tlast = t;
          hist_print (hist, tlast - tfirst, 1);
          tfirst = tprev;
          t = nowll ();
        }
        if (++bi == spec->burstsize)
        {
          while (((ntot / spec->burstsize) / ((t - tfirst0) / 1e9 + 5e-3)) > spec->writerate && !termflag)
          {
            struct timespec delay;
            delay.tv_sec = 0;
            delay.tv_nsec = 10 * 1000 * 1000;
            nanosleep (&delay, NULL);
            t = nowll ();
          }
          bi = 0;
        }
        tprev = t;
      }
    }
  }
  tlast = nowll ();
  hist_print(hist, tlast - tfirst, 0);
  hist_free (hist);
  printf ("total writes: %" PRIu64 " (%e/s)\n", ntot, ntot * 1e9 / (tlast - tfirst0));
  if (spec->topicsel == KS)
    DDS_free (d.ks.baggage._buffer);
}

static char *pub_do_nonarb(const struct writerspec *spec, int fdin, uint32_t *seq)
{
  struct tstamp_t tstamp_spec = { .isabs = 0, .t = 0 };
  DDS_ReturnCode_t result;
  union data d;
  char command;
  char *arg = NULL;
  int k = 0;
  memset (&d, 0, sizeof (d));
  switch (spec->topicsel)
  {
    case UNSPEC:
      assert(0);
    case KS:
      d.ks.baggage._maximum = d.ks.baggage._length = spec->baggagesize;
      d.ks.baggage._buffer = DDS_sequence_octet_allocbuf (spec->baggagesize);
      memset (d.ks.baggage._buffer, 0xee, spec->baggagesize);
      break;
    case K32:
      memset (d.k32.baggage, 0xee, sizeof (d.k32.baggage));
      break;
    case K64:
      memset (d.k64.baggage, 0xee, sizeof (d.k64.baggage));
      break;
    case K128:
      memset (d.k128.baggage, 0xee, sizeof (d.k128.baggage));
      break;
    case K256:
      memset (d.k256.baggage, 0xee, sizeof (d.k256.baggage));
      break;
    case OU:
      break;
    case ARB:
      assert (!(fdin == -1 && fdservsock == -1));
      break;
  }
  assert (fdin >= 0);
  d.seq = *seq;
  command = 0;
  while (command != ':' && read_value (fdin, &command, &k, &tstamp_spec, &arg))
  {
    d.seq_keyval.keyval = k;
    switch (command)
    {
      case 'w': case 'd': case 'D': case 'u': case 'r': {
        write_oper_t fn = get_write_oper(command);
        DDS_Time_t tstamp;
        if (!tstamp_spec.isabs)
        {
          DDS_DomainParticipant_get_current_time(dp, &tstamp);
          tstamp_spec.t += tstamp.sec * T_SECOND + tstamp.nanosec;
        }
        tstamp.sec = (int) (tstamp_spec.t / T_SECOND);
        tstamp.nanosec = (unsigned) (tstamp_spec.t % T_SECOND);
        if ((result = fn (spec->wr, &d, DDS_HANDLE_NIL, &tstamp)) != DDS_RETCODE_OK)
        {
          printf ("%s %d: error %d (%s)\n", get_write_operstr(command), k, (int) result, dds_strerror(result));
          if (!accept_error (command, result))
            exit(2);
        }
        if (spec->dupwr && (result = fn (spec->dupwr, &d, DDS_HANDLE_NIL, &tstamp)) != DDS_RETCODE_OK)
        {
          printf ("%s %d(dup): error %d (%s)\n", get_write_operstr(command), k, (int) result, dds_strerror(result));
          if (!accept_error (command, result))
            exit(2);
        }
        d.seq++;
        break;
      }
      case 'z':
        if (spec->topicsel != KS)
          printf ("payload size cannot be set for selected type\n");
        else if (k < 12 && k != 0)
          printf ("invalid payload size: %d\n", k);
        else
        {
          uint32_t baggagesize = (k != 0) ? (uint32_t) (k - 12) : 0;
          if (d.ks.baggage._buffer)
            DDS_free (d.ks.baggage._buffer);
          d.ks.baggage._maximum = d.ks.baggage._length = baggagesize;
          d.ks.baggage._buffer = DDS_sequence_octet_allocbuf (baggagesize);
          memset (d.ks.baggage._buffer, 0xee, d.ks.baggage._length);
        }
        break;
      case 'p':
        set_pub_partition (DDS_DataWriter_get_publisher(spec->wr), arg);
        break;
      case 's':
        if (k < 0)
          printf ("invalid sleep duration: %ds\n", k);
        else
          sleep ((unsigned) k);
        break;
      case 'n':
        if (k < 0)
          printf ("invalid nap duration: %dus\n", k);
        else
          usleep ((unsigned) k);
        break;
      case 'Y': case 'B': case 'E': case 'W': case ')': case 'Q': case 'P':
        non_data_operation(command, spec->wr);
        break;
      case 'C':
        do_ddsi_control(arg);
        break;
      case 'S':
        make_persistent_snapshot(arg);
        break;
      case ':':
        break;
      default:
        abort ();
    }
  }
  if (spec->topicsel == KS)
    DDS_free (d.ks.baggage._buffer);
  *seq = d.seq;
  if (command == ':')
    return arg;
  else
  {
    free(arg);
    return NULL;
  }
}

static char *pub_do_arb_line(const struct writerspec *spec, const char *line)
{
  DDS_ReturnCode_t result;
  struct tstamp_t tstamp_spec;
  char *ret = NULL;
  char command;
  int k, pos;
  while (line && *(line = skipspaces(line)) != 0)
  {
    tstamp_spec.isabs = 0; tstamp_spec.t = 0;
    command = 'w';
    switch (*line)
    {
      case 'w': case 'd': case 'D': case 'u': case 'r':
        command = *line++;
        if (*line == '@')
        {
          if (*++line == '=') { ++line; tstamp_spec.isabs = 1; }
          tstamp_spec.t = T_SECOND * strtol (line, (char **) &line, 10);
        }
      case '{': {
        write_oper_t fn = get_write_oper(command);
        void *arb;
        char *endp;
        if ((arb = tgscan (spec->tgtp, line, &endp)) == NULL) {
          line = NULL;
        } else {
          DDS_Time_t tstamp;
          int diddodup = 0;
          if (!tstamp_spec.isabs)
          {
            DDS_DomainParticipant_get_current_time(dp, &tstamp);
            tstamp_spec.t += tstamp.sec * T_SECOND + tstamp.nanosec;
          }
          tstamp.sec = (int) (tstamp_spec.t / T_SECOND);
          tstamp.nanosec = (unsigned) (tstamp_spec.t % T_SECOND);
          line = endp;
          result = fn (spec->wr, arb, DDS_HANDLE_NIL, &tstamp);
          if (result == DDS_RETCODE_OK && spec->dupwr)
          {
            diddodup = 1;
            result = fn (spec->dupwr, arb, DDS_HANDLE_NIL, &tstamp);
          }
          tgfreedata(spec->tgtp, arb);
          if (result != DDS_RETCODE_OK)
          {
            printf ("%s%s: error %d (%s)\n", get_write_operstr(command), diddodup ? "(dup)" : "", (int) result, dds_strerror(result));
            if (!accept_error (command, result))
            {
              line = NULL;
              if (!isatty(fdin))
                exit(2);
              break;
            }
          }
        }
        break;
      }
      case 'p':
        set_pub_partition (DDS_DataWriter_get_publisher(spec->wr), line+1);
        line = NULL;
        break;
      case 's':
        if (sscanf(line+1, "%d%n", &k, &pos) != 1 || k < 0) {
          printf ("invalid sleep duration: %ds\n", k);
          line = NULL;
        } else {
          sleep ((unsigned) k);
          line += 1 + pos;
        }
        break;
      case 'n':
        if (sscanf(line+1, "%d%n", &k, &pos) != 1 || k < 0) {
          printf ("invalid nap duration: %dus\n", k);
          line = NULL;
        } else {
          usleep ((unsigned) k);
          line += 1 + pos;
        }
        break;
      case 'Y': case 'B': case 'E': case 'W': case ')': case 'Q': case 'P':
        non_data_operation(*line++, spec->wr);
        break;
      case 'C':
        do_ddsi_control(line+1);
        line = NULL;
        break;
      case 'S':
        make_persistent_snapshot(line+1);
        line = NULL;
        break;
      case ':':
        ret = strdup(line+1);
        line = NULL;
        break;
      default:
        printf ("unrecognised command: %s\n", line);
        line = NULL;
        break;
    }
  }
  return ret;
}

static char *pub_do_arb(const struct writerspec *spec, struct getl_arg *getl_arg)
{
  const char *orgline;
  char *ret = NULL;
  int count;
  while (ret == NULL && (orgline = getl(getl_arg, &count)) != NULL)
  {
    const char *line = skipspaces(orgline);
    if (*line) getl_enter_hist(getl_arg, orgline);
    ret = pub_do_arb_line (spec, line);
  }
  return ret;
}

static void *pubthread_auto(void *vspec)
{
  const struct writerspec *spec = vspec;
  assert (spec->topicsel != UNSPEC && spec->topicsel != ARB);
  pub_do_auto(spec);
  return 0;
}

static void *pubthread(void *vwrspecs)
{
  struct wrspeclist *wrspecs = vwrspecs;
  uint32_t seq = 0;
  struct getl_arg getl_arg;
#if USE_EDITLINE
  getl_init_editline(&getl_arg, fdin);
#else
  getl_init_simple(&getl_arg, fdin);
#endif
  do {
    struct wrspeclist *cursor = wrspecs;
    struct writerspec *spec = cursor->spec;
    char *nextspec;
    if (fdservsock != -1) {
      /* w_accept doesn't return on error, uses -1 to signal termination */
      if ((fdin = w_accept(fdservsock)) < 0)
        continue;
      getl_reset_input(&getl_arg, fdin);
    }
    assert (fdin >= 0);
    do {
      if (spec->topicsel != ARB)
        nextspec = pub_do_nonarb(spec, fdin, &seq);
      else
        nextspec = pub_do_arb(spec, &getl_arg);
      if (nextspec == NULL)
        spec = NULL;
      else
      {
        int cnt, pos;
        char *tmp = nextspec + strlen(nextspec);
        while (tmp > nextspec && isspace((unsigned char)tmp[-1]))
          *--tmp = 0;
        if ((sscanf (nextspec, "+%d%n", &cnt, &pos) == 1 && nextspec[pos] == 0) || ((void)(cnt = 1), strcmp(nextspec, "+") == 0)) {
          while (cnt--) cursor = cursor->next;
        } else if ((sscanf (nextspec, "-%d%n", &cnt, &pos) == 1 && nextspec[pos] == 0) || ((void)(cnt = 1), strcmp(nextspec, "+") == 0)) {
          while (cnt--) cursor = cursor->prev;
        } else if (sscanf (nextspec, "%d%n", &cnt, &pos) == 1 && nextspec[pos] == 0) {
          cursor = wrspecs; while (cnt--) cursor = cursor->next;
        } else {
          struct wrspeclist *endm = cursor, *cand = NULL;
          do {
            if (strncmp (cursor->spec->tpname, nextspec, strlen(nextspec)) == 0) {
              if (cand == NULL)
                cand = cursor;
              else {
                printf ("%s: ambiguous writer specification\n", nextspec);
                break;
              }
            }
            cursor = cursor->next;
          } while (cursor != endm);
          if (cand == NULL) {
            printf ("%s: no matching writer specification\n", nextspec);
          } else if (cursor != endm) { /* ambiguous case */
            cursor = endm;
          } else {
            cursor = cand;
          }
        }
        spec = cursor->spec;
      }
    } while (spec);
    if (fdin > 0)
      close (fdin);
    if (fdservsock != -1)
    {
      if (print_tcp)
        printf ("listening ... ");
      fflush (stdout);
    }
  } while (fdservsock != -1 && !termflag);
  getl_fini(&getl_arg);
  return 0;
}

struct eseq_admin {
  unsigned nkeys;
  unsigned nph;
  DDS_InstanceHandle_t *ph;
  unsigned **eseq;
};

static void init_eseq_admin (struct eseq_admin *ea, unsigned nkeys)
{
  ea->nkeys = nkeys;
  ea->nph = 0;
  ea->ph = NULL;
  ea->eseq = NULL;
}

static void fini_eseq_admin (struct eseq_admin *ea)
{
  free (ea->ph);
  for (unsigned i = 0; i < ea->nph; i++)
    free (ea->eseq[i]);
  free (ea->eseq);
}

static int check_eseq (struct eseq_admin *ea, unsigned seq, unsigned keyval, const DDS_InstanceHandle_t pubhandle)
{
  unsigned *eseq;
  if (keyval >= ea->nkeys)
  {
    printf ("received key %d >= nkeys %d\n", keyval, ea->nkeys);
    exit (2);
  }
  for (unsigned i = 0; i < ea->nph; i++)
    if (pubhandle == ea->ph[i])
    {
      unsigned e = ea->eseq[i][keyval];
      ea->eseq[i][keyval] = seq + ea->nkeys;
      return seq == e;
    }
  ea->ph = realloc (ea->ph, (ea->nph + 1) * sizeof (*ea->ph));
  ea->ph[ea->nph] = pubhandle;
  ea->eseq = realloc (ea->eseq, (ea->nph + 1) * sizeof (*ea->eseq));
  ea->eseq[ea->nph] = malloc (ea->nkeys * sizeof (*ea->eseq[ea->nph]));
  eseq = ea->eseq[ea->nph];
  for (unsigned i = 0; i < ea->nkeys; i++)
    eseq[i] = seq + (i - keyval) + (i <= keyval ? ea->nkeys : 0);
  ea->nph++;
  return 1;
}

static int subscriber_needs_access (DDS_Subscriber sub)
{
  DDS_SubscriberQos *qos;
  DDS_ReturnCode_t rc;
  int x;
  if ((qos = DDS_SubscriberQos__alloc ()) == NULL)
    return DDS_RETCODE_OUT_OF_RESOURCES;
  if ((rc = DDS_Subscriber_get_qos (sub, qos)) != DDS_RETCODE_OK)
    error ("DDS_Subscriber_get_qos: error %d (%s)\n", (int) rc, dds_strerror (rc));
  x = (qos->presentation.access_scope == DDS_GROUP_PRESENTATION_QOS && qos->presentation.coherent_access);
  DDS_free (qos);
  return x;
}

static void *subthread (void *vspec)
{
  const struct readerspec *spec = vspec;
  DDS_DataReader rd = spec->rd;
  DDS_Subscriber sub = DDS_DataReader_get_subscriber(rd);
  const int need_access = subscriber_needs_access (sub);
  DDS_WaitSet ws;
  DDS_ReadCondition rdcondA = 0, rdcondD = 0;
  DDS_StatusCondition stcond = 0;
  DDS_ReturnCode_t result = DDS_RETCODE_OK;
  uintptr_t exitcode = 0;
  char tag[256];

  DDS_TopicDescription td = DDS_DataReader_get_topicdescription(spec->rd);
  DDS_string tn = DDS_TopicDescription_get_name(td);
  snprintf(tag, sizeof(tag), "[%u:%s]", spec->idx, tn);
  DDS_free(tn);

  if (wait_hist_data)
  {
    printf("prewfh\n");
    sleep(10);
    printf("wfh\n");
    if ((result = DDS_DataReader_wait_for_historical_data (rd, &wait_hist_data_timeout)) != DDS_RETCODE_OK)
      error ("DDS_DataReader_wait_for_historical_data: %d (%s)\n", (int) result, dds_strerror (result));
  }

  ws = DDS_WaitSet__alloc ();
  if ((result = DDS_WaitSet_attach_condition (ws, termcond)) != DDS_RETCODE_OK)
    error ("DDS_WaitSet_attach_condition (termcomd): %d (%s)\n", (int) result, dds_strerror (result));
  switch (spec->mode)
  {
    case MODE_NONE:
    case MODE_ZEROLOAD:
      /* no triggers */
      break;
    case MODE_PRINT:
      /* complicated triggers */
      if ((rdcondA = DDS_DataReader_create_readcondition (rd, spec->use_take ? DDS_ANY_SAMPLE_STATE : DDS_NOT_READ_SAMPLE_STATE, DDS_ANY_VIEW_STATE, DDS_ALIVE_INSTANCE_STATE | DDS_NOT_ALIVE_NO_WRITERS_INSTANCE_STATE)) == NULL)
        error ("DDS_DataReader_create_readcondition (rdcondA)\n");
      if ((result = DDS_WaitSet_attach_condition (ws, rdcondA)) != DDS_RETCODE_OK)
        error ("DDS_WaitSet_attach_condition (rdcondA): %d (%s)\n", (int) result, dds_strerror (result));
      if ((rdcondD = DDS_DataReader_create_readcondition (rd, DDS_ANY_SAMPLE_STATE, DDS_ANY_VIEW_STATE, DDS_NOT_ALIVE_DISPOSED_INSTANCE_STATE)) == NULL)
        error ("DDS_DataReader_create_readcondition (rdcondD)\n");
      if ((result = DDS_WaitSet_attach_condition (ws, rdcondD)) != DDS_RETCODE_OK)
        error ("DDS_WaitSet_attach_condition (rdcondD): %d (%s)\n", (int) result, dds_strerror (result));
      break;
    case MODE_CHECK:
    case MODE_DUMP:
      if (!spec->polling)
      {
        /* fastest trigger we have */
        if ((stcond = DDS_DataReader_get_statuscondition (rd)) == NULL)
          error ("DDS_DataReader_get_statuscondition\n");
        if ((result = DDS_StatusCondition_set_enabled_statuses (stcond, DDS_DATA_AVAILABLE_STATUS)) != DDS_RETCODE_OK)
          error ("DDS_StatusCondition_set_enabled_statuses (stcond): %d (%s)\n", (int) result, dds_strerror (result));
        if ((result = DDS_WaitSet_attach_condition (ws, stcond)) != DDS_RETCODE_OK)
          error ("DDS_WaitSet_attach_condition (stcond): %d (%s)\n", (int) result, dds_strerror (result));
      }
      break;
  }

  {
    union {
      void *any;
      DDS_sequence_KeyedSeq *ks;
      DDS_sequence_Keyed32 *k32;
      DDS_sequence_Keyed64 *k64;
      DDS_sequence_Keyed128 *k128;
      DDS_sequence_Keyed256 *k256;
      DDS_sequence_OneULong *ou;
    } mseq;
    DDS_SampleInfoSeq *iseq;
    DDS_ConditionSeq *glist;
    DDS_Duration_t timeout;
    unsigned long long tstart = 0, tfirst = 0, tprint = 0;
    long long out_of_seq = 0, nreceived = 0, last_nreceived = 0;
    long long nreceived_bytes = 0, last_nreceived_bytes = 0;
    struct eseq_admin eseq_admin;
    struct hist *hist = hist_new (30, 10000, 0);
    init_eseq_admin(&eseq_admin, nkeyvals);
    mseq.any = DDS_sequence_octet__alloc();
    iseq = DDS_SampleInfoSeq__alloc ();
    glist = DDS_ConditionSeq__alloc ();
    timeout.sec = 0;
    timeout.nanosec = 100000000;

    while (!termflag && !once_mode)
    {
      unsigned long long tnow;
      unsigned gi;

      if (spec->polling)
      {
        const struct timespec d = { 0, 1000000 }; /* 1ms sleep interval, so a bit less than 1kHz poll freq */
        nanosleep (&d, NULL);
      }
      else if ((result = DDS_WaitSet_wait (ws, glist, &timeout)) != DDS_RETCODE_OK && result != DDS_RETCODE_TIMEOUT)
      {
        printf ("wait: error %d\n", (int) result);
        break;
      }

      for (gi = 0; gi < (spec->polling ? 1 : glist->_length); gi++)
      {
        const DDS_Condition cond = spec->polling ? 0 : glist->_buffer[gi];
        unsigned i;

        assert (spec->polling || cond == rdcondA || cond == rdcondD || cond == stcond || cond == termcond);
        if (cond == termcond)
          continue;

        if (spec->print_match_pre_read)
        {
          DDS_SubscriptionMatchedStatus status;
          uint32_t systemId, localId;
          DDS_DataReader_get_subscription_matched_status (rd, &status);
          instancehandle_to_id(&systemId, &localId, status.last_publication_handle);
          printf ("[pre-read: subscription-matched: total=(%d change %d) current=(%d change %d) handle=%" PRIx32 ":%" PRIx32 "]\n",
                  status.total_count, status.total_count_change,
                  status.current_count, status.current_count_change,
                  systemId, localId);
        }

        /* Always take NOT_ALIVE_DISPOSED data because it means the
         instance has reached its end-of-life.

         NO_WRITERS I usually don't care for (though there certainly
         are situations in which it is useful information).  But you
         can't have a NO_WRITERS with invalid_data set:

         - either the reader contains the instance without data in
         the disposed state, but in that case it stays in the
         NOT_ALIVED_DISPOSED state;

         - or the reader doesn't have the instance yet, in which
         case the unregister is silently discarded.

         However, receiving an unregister doesn't turn the sample
         into a NEW one, though.  So HOW AM I TO TRIGGER ON IT
         without triggering CONTINUOUSLY?
         */
        if (need_access && (result = DDS_Subscriber_begin_access (sub)) != DDS_RETCODE_OK)
          error ("DDS_Subscriber_begin_access: %d (%s)\n", (int) result, dds_strerror (result));

        if (spec->mode == MODE_CHECK || (spec->mode == MODE_DUMP && spec->use_take) || spec->polling) {
          result = DDS_DataReader_take (rd, mseq.any, iseq, spec->read_maxsamples, DDS_ANY_SAMPLE_STATE, DDS_ANY_VIEW_STATE, DDS_ANY_INSTANCE_STATE);
        } else if (spec->mode == MODE_DUMP) {
          result = DDS_DataReader_read (rd, mseq.any, iseq, spec->read_maxsamples, DDS_ANY_SAMPLE_STATE, DDS_ANY_VIEW_STATE, DDS_ANY_INSTANCE_STATE);
        } else if (spec->use_take || cond == rdcondD) {
          result = DDS_DataReader_take_w_condition (rd, mseq.any, iseq, spec->read_maxsamples, cond);
        } else {
          result = DDS_DataReader_read_w_condition (rd, mseq.any, iseq, spec->read_maxsamples, cond);
        }

        {
          DDS_ReturnCode_t end_access_result;
          if (need_access && (end_access_result = DDS_Subscriber_end_access (sub)) != DDS_RETCODE_OK)
            error ("DDS_Subscriber_end_access: %d (%s)\n", (int) end_access_result, dds_strerror (end_access_result));
        }

        if (result != DDS_RETCODE_OK)
        {
          if (spec->polling && result == DDS_RETCODE_NO_DATA)
            ; /* expected */
          else if (spec->mode == MODE_CHECK || spec->mode == MODE_DUMP || spec->polling)
            printf ("%s: %d (%s) on %s\n", (!spec->use_take && spec->mode == MODE_DUMP) ? "read" : "take", (int) result, dds_strerror (result), spec->polling ? "poll" : "stcond");
          else
            printf ("%s: %d (%s) on rdcond%s\n", spec->use_take ? "take" : "read", (int) result, dds_strerror (result), (cond == rdcondA) ? "A" : (cond == rdcondD) ? "D" : "?");
          continue;
        }

        tnow = nowll ();

        switch (spec->mode)
        {
          case MODE_PRINT:
          case MODE_DUMP:
            switch (spec->topicsel) {
              case UNSPEC: assert(0);
              case KS:   print_seq_KS (&tstart, tnow, rd, tag, iseq, mseq.ks); break;
              case K32:  print_seq_K32 (&tstart, tnow, rd, tag, iseq, mseq.k32); break;
              case K64:  print_seq_K64 (&tstart, tnow, rd, tag, iseq, mseq.k64); break;
              case K128: print_seq_K128 (&tstart, tnow, rd, tag, iseq, mseq.k128); break;
              case K256: print_seq_K256 (&tstart, tnow, rd, tag, iseq, mseq.k256); break;
              case OU:   print_seq_OU (&tstart, tnow, rd, tag, iseq, mseq.ou); break;
              case ARB:  print_seq_ARB (&tstart, tnow, rd, tag, iseq, mseq.any, spec->tgtp); break;
            }
            break;

          case MODE_CHECK:
            for (i = 0; i < iseq->_length; i++)
            {
              int keyval = 0;
              unsigned seq = 0;
              unsigned size = 0;
              if (!iseq->_buffer[i].valid_data)
                continue;
              switch (spec->topicsel)
              {
                case UNSPEC: assert(0);
                case KS:   { KeyedSeq *d = &mseq.ks->_buffer[i];   keyval = d->keyval; seq = d->seq; size = 12 + d->baggage._length; } break;
                case K32:  { Keyed32 *d  = &mseq.k32->_buffer[i];  keyval = d->keyval; seq = d->seq; size = 32; } break;
                case K64:  { Keyed64 *d  = &mseq.k64->_buffer[i];  keyval = d->keyval; seq = d->seq; size = 64; } break;
                case K128: { Keyed128 *d = &mseq.k128->_buffer[i]; keyval = d->keyval; seq = d->seq; size = 128; } break;
                case K256: { Keyed256 *d = &mseq.k256->_buffer[i]; keyval = d->keyval; seq = d->seq; size = 256; } break;
                case OU:   { OneULong *d = &mseq.ou->_buffer[i];   keyval = 0;         seq = d->seq; size = 4; } break;
                case ARB:  assert(0); break; /* can't check what we don't know */
              }
              if (check_eseq (&eseq_admin, seq, (unsigned)keyval, iseq->_buffer[i].publication_handle))
              {
                unsigned long long tsrc = (DDS_unsigned_long)iseq->_buffer[i].source_timestamp.sec * 1000000000ull + iseq->_buffer[i].source_timestamp.nanosec;
                unsigned long long tdelta = tnow - tsrc;
                hist_record (hist, tdelta, 1);
                if (latlog_fp)
                {
                  fwrite(&tnow, sizeof(tnow), 1, latlog_fp);
                  fwrite(&tsrc, sizeof(tsrc), 1, latlog_fp);
                }
              }
              else
              {
                out_of_seq++;
                if (spec->exit_on_out_of_seq)
                {
                  switch (spec->topicsel) {
                    case UNSPEC: assert(0);
                    case KS:   print_seq_KS (&tstart, tnow, rd, tag, iseq, mseq.ks); break;
                    case K32:  print_seq_K32 (&tstart, tnow, rd, tag, iseq, mseq.k32); break;
                    case K64:  print_seq_K64 (&tstart, tnow, rd, tag, iseq, mseq.k64); break;
                    case K128: print_seq_K128 (&tstart, tnow, rd, tag, iseq, mseq.k128); break;
                    case K256: print_seq_K256 (&tstart, tnow, rd, tag, iseq, mseq.k256); break;
                    case OU:   print_seq_OU (&tstart, tnow, rd, tag, iseq, mseq.ou); break;
                    case ARB:  print_seq_ARB (&tstart, tnow, rd, tag, iseq, mseq.any, spec->tgtp); break;
                  }
                  exitcode = 1;
                  terminate();
                }
              }
              if (nreceived == 0)
              {
                tfirst = tnow;
                tprint = tfirst;
              }
              nreceived++;
              nreceived_bytes += size;
              if (tnow - tprint >= 1000000000ll || termflag)
              {
                const unsigned long long tdelta_ns = tnow - tfirst;
                const unsigned long long tdelta_s0 = tdelta_ns / 1000000000;
                const unsigned tdelta_ms0 = ((tdelta_ns % 1000000000) + 500000) / 1000000;
                const unsigned long long tdelta_s = tdelta_s0 + (tdelta_ms0 == 1000);
                const unsigned tdelta_ms = tdelta_ms0 % 1000;
                const long long ndelta = nreceived - last_nreceived;
                const double rate_Mbps = (nreceived_bytes - last_nreceived_bytes) * 8 / 1e6;
                flockfile(stdout);
                printf ("%llu.%03u ntot %lld nseq %lld ndelta %lld rate %.2f Mb/s",
                        tdelta_s, tdelta_ms, nreceived, out_of_seq, ndelta, rate_Mbps);
                if (print_latency)
                  hist_print (hist, tnow - tprint, 1);
                else
                  printf ("\n");
                funlockfile(stdout);
                last_nreceived = nreceived;
                last_nreceived_bytes = nreceived_bytes;
                tprint = tnow;
              }
            }
            break;

          case MODE_NONE:
          case MODE_ZEROLOAD:
            break;
        }
        DDS_DataReader_return_loan(rd, mseq.any, iseq);
        if (spec->sleep_us)
          usleep (spec->sleep_us);
      }
    }
    DDS_free (glist);

    if (once_mode || (spec->do_final_take && (spec->mode == MODE_PRINT || spec->mode == MODE_DUMP)))
    {
      if (need_access && (result = DDS_Subscriber_begin_access (sub)) != DDS_RETCODE_OK)
        error ("DDS_Subscriber_begin_access: %d (%s)\n", (int) result, dds_strerror (result));

      result = DDS_DataReader_take (rd, mseq.any, iseq, DDS_LENGTH_UNLIMITED, DDS_ANY_SAMPLE_STATE, DDS_ANY_VIEW_STATE, DDS_ANY_INSTANCE_STATE);
      if (result == DDS_RETCODE_NO_DATA)
      {
        if (once_mode)
          exitcode = 1;
        else if (print_final_take_notice)
          printf ("-- final take: data reader empty --\n");
      }
      else if (result != DDS_RETCODE_OK)
      {
        if (once_mode)
          error ("read/take: %d (%s)\n", (int) result, dds_strerror (result));
        else if (print_final_take_notice)
          printf ("-- final take: %d (%s) --\n", (int) result, dds_strerror (result));
      }
      else
      {
        if (!once_mode && print_final_take_notice)
          printf ("-- final contents of data reader --\n");
        if (spec->mode == MODE_PRINT || spec->mode == MODE_DUMP)
        {
          switch (spec->topicsel)
          {
            case UNSPEC: assert(0);
            case KS:   print_seq_KS (&tstart, nowll (), rd, tag, iseq, mseq.ks); break;
            case K32:  print_seq_K32 (&tstart, nowll (), rd, tag, iseq, mseq.k32); break;
            case K64:  print_seq_K64 (&tstart, nowll (), rd, tag, iseq, mseq.k64); break;
            case K128: print_seq_K128 (&tstart, nowll (), rd, tag, iseq, mseq.k128); break;
            case K256: print_seq_K256 (&tstart, nowll (), rd, tag, iseq, mseq.k256); break;
            case OU:   print_seq_OU (&tstart, nowll (), rd, tag, iseq, mseq.ou); break;
            case ARB:  print_seq_ARB (&tstart, nowll (), rd, tag, iseq, mseq.any, spec->tgtp); break;
          }
        }
      }
      if (need_access && (result = DDS_Subscriber_end_access (sub)) != DDS_RETCODE_OK)
        error ("DDS_Subscriber_end_access: %d (%s)\n", (int) result, dds_strerror (result));
      DDS_DataReader_return_loan (rd, mseq.any, iseq);
    }

    DDS_free (iseq);
    DDS_free (mseq.any);
    if (spec->mode == MODE_CHECK)
      printf ("received: %lld, out of seq: %lld\n", nreceived, out_of_seq);
    fini_eseq_admin (&eseq_admin);
    hist_free (hist);
  }

  switch (spec->mode)
  {
    case MODE_NONE:
    case MODE_ZEROLOAD:
      break;
    case MODE_PRINT:
      DDS_WaitSet_detach_condition (ws, rdcondA);
      DDS_DataReader_delete_readcondition (rd, rdcondA);
      DDS_WaitSet_detach_condition (ws, rdcondD);
      DDS_DataReader_delete_readcondition (rd, rdcondD);
      break;
    case MODE_CHECK:
    case MODE_DUMP:
      if (!spec->polling)
        DDS_WaitSet_detach_condition (ws, stcond);
      break;
  }
  DDS_WaitSet_detach_condition(ws, termcond);
  DDS_free (ws);
  if (once_mode)
  {
    /* trigger EOF for writer side, so we actually do terminate */
    terminate();
  }
  return (void *) exitcode;
}

static void *autotermthread(void *varg __attribute__((unused)))
{
  unsigned long long tstop, tnow;
  DDS_ReturnCode_t result;
  DDS_ConditionSeq *glist;
  DDS_WaitSet ws;

  assert (dur > 0);

  glist = DDS_ConditionSeq__alloc ();
  glist->_maximum = 1;
  glist->_length = 0;
  glist->_buffer = DDS_ConditionSeq_allocbuf (glist->_maximum);

  tnow = nowll ();
  tstop = tnow + (unsigned long long) (1e9 * dur);

  ws = DDS_WaitSet__alloc ();
  if ((result = DDS_WaitSet_attach_condition (ws, termcond)) != DDS_RETCODE_OK)
    error ("DDS_WaitSet_attach_condition (termcomd): %d (%s)\n", (int) result, dds_strerror (result));

  tnow = nowll();
  while (!termflag && tnow < tstop)
  {
    unsigned long long dt = tstop - tnow;
    DDS_Duration_t timeout;
    timeout.sec = (DDS_long) (dt / 1000000000);
    timeout.nanosec = (DDS_unsigned_long) (dt % 1000000000);
    if ((result = DDS_WaitSet_wait (ws, glist, &timeout)) != DDS_RETCODE_OK && result != DDS_RETCODE_TIMEOUT)
    {
      printf ("wait: error %d\n", (int) result);
      break;
    }
    tnow = nowll();
  }

  DDS_WaitSet_detach_condition(ws, termcond);
  DDS_free(ws);
  return NULL;
}

static const char *execname (int argc, char *argv[])
{
  const char *p;
  if (argc == 0 || argv[0] == NULL)
    return "";
  else if ((p = strrchr(argv[0], '/')) != NULL)
    return p + 1;
  else
    return argv[0];
}

static char *read_line_from_textfile(FILE *fp)
{
  char *str = NULL;
  size_t sz = 0, n = 0;
  int c;
  while ((c = fgetc(fp)) != EOF && c != '\n') {
    if (n == sz) str = realloc(str, sz += 256);
    str[n++] = (char)c;
  }
  if (c != EOF || n > 0) {
    if (n == sz) str = realloc(str, sz += 1);
    str[n] = 0;
  } else if (ferror(fp)) {
    error("error reading file, errno = %d (%s)\n", errno, strerror(errno));
  }
  return str;
}

static int get_metadata (char **metadata, char **typename, char **keylist, const char *file)
{
  FILE *fp;
  if ((fp = fopen(file, "r")) == NULL)
    error("%s: can't open for reading metadata\n", file);
  *typename = read_line_from_textfile(fp);
  *keylist = read_line_from_textfile(fp);
  *metadata = read_line_from_textfile(fp);
  if (*typename == NULL || *keylist == NULL || *typename == NULL)
    error("%s: invalid metadata file\n", file);
  fclose(fp);
  return 1;
}

static DDS_Topic find_topic(DDS_DomainParticipant dp, const char *name, const DDS_Duration_t *timeout)
{
  DDS_ReturnCode_t result;
  DDS_Topic tp;
  int isbuiltin = 0;

  /* A historical accident has caused subtle issues with a generic reader for the built-in topics included in the DDS spec. */
  if (strcmp(name, "DCPSParticipant") == 0 || strcmp(name, "DCPSTopic") == 0 ||
      strcmp(name, "DCPSSubscription") == 0 || strcmp(name, "DCPSPublication") == 0) {
    DDS_Subscriber sub;
    if ((sub = DDS_DomainParticipant_get_builtin_subscriber(dp)) == NULL)
      error("DDS_DomainParticipant_get_builtin_subscriber failed\n");
    if (DDS_Subscriber_lookup_datareader(sub, name) == NULL)
      error("DDS_Subscriber_lookup_datareader failed\n");
    if ((result = DDS_Subscriber_delete_contained_entities(sub)) != DDS_RETCODE_OK)
      error("DDS_Subscriber_delete_contained_entities failed: error %d (%s)\n", (int) result, dds_strerror(result));
    //if ((result = DDS_DomainParticipant_delete_subscriber(dp, sub)) != DDS_RETCODE_OK)
    //  error("DDS_DomainParticipant_delete_subscriber failed: error %d (%s)\n", (int) result, dds_strerror(result));
    isbuiltin = 1;
  }

  if ((tp = DDS_DomainParticipant_find_topic(dp, name, timeout)) == NULL)
    error("topic %s not found\n", name);

  if (!isbuiltin) {
    char *tn = DDS_Topic_get_type_name(tp);
    char *kl = DDS_Topic_get_keylist(tp);
    char *md = DDS_Topic_get_metadescription(tp);
    DDS_ReturnCode_t result;
    DDS_TypeSupport ts;
    if ((ts = DDS_TypeSupport__alloc(tn, kl ? kl : "", md)) == NULL)
      error("DDS_TypeSupport__alloc(%s) failed\n", tn);
    if ((result = DDS_TypeSupport_register_type(ts, dp, tn)) != DDS_RETCODE_OK)
      error("DDS_TypeSupport_register_type(%s) failed: %d (%s)\n", tn, (int) result, dds_strerror(result));
    DDS_free(md);
    DDS_free(kl);
    DDS_free(tn);
    DDS_free(ts);

    /* Work around a double-free-at-shutdown issue caused by a find_topic
       without a type support having been register */
    if ((result = DDS_DomainParticipant_delete_topic(dp, tp)) != DDS_RETCODE_OK) {
      error("DDS_DomainParticipant_find_topic failed: %d (%s)\n", (int) result, dds_strerror(result));
    }
    if ((tp = DDS_DomainParticipant_find_topic(dp, name, timeout)) == NULL) {
      error("DDS_DomainParticipant_find_topic(2) failed\n");
    }
  }

  return tp;
}

static void set_systemid_env (void)
{
  uint32_t systemId, localId;
  char str[128];
  instancehandle_to_id (&systemId, &localId, DDS_Entity_get_instance_handle (dp));
  snprintf (str, sizeof (str), "%u", systemId);
  setenv ("SYSTEMID", str, 1);
  snprintf (str, sizeof (str), "__NODE%08x BUILT-IN PARTITION__", systemId);
  setenv ("NODE_BUILTIN_PARTITION", str, 1);
}

struct spec {
  DDS_Topic tp;
  DDS_Topic cftp;
  const char *topicname;
  const char *cftp_expr;
  char *metadata;
  char *typename;
  char *keylist;
  DDS_Duration_t findtopic_timeout;
  struct readerspec rd;
  struct writerspec wr;
  pthread_t rdtid;
  pthread_t wrtid;
};

static void addspec(unsigned whatfor, unsigned *specsofar, unsigned *specidx, struct spec **spec, int want_reader)
{
  if (*specsofar & whatfor)
  {
    struct spec *s;
    (*specidx)++;
    *spec = realloc(*spec, (*specidx + 1) * sizeof(**spec));
    s = &(*spec)[*specidx];
    s->tp = NULL;
    s->cftp = NULL;
    s->topicname = NULL;
    s->cftp_expr = NULL;
    s->metadata = NULL;
    s->typename = NULL;
    s->keylist = NULL;
    s->findtopic_timeout.sec = 10;
    s->findtopic_timeout.nanosec = 0;
    s->rd = def_readerspec;
    s->wr = def_writerspec;
    if (fdin == -1 && fdservsock == -1)
      s->wr.mode = WM_NONE;
    if (!want_reader)
      s->rd.mode = MODE_NONE;
    *specsofar = 0;
  }
  *specsofar |= whatfor;
}

static void set_print_mode (const char *optarg)
{
  char *copy = strdup(optarg), *cursor = copy, *tok;
  unsigned chop = 0;
  while ((tok = strsep(&cursor, ",")) != NULL) {
    int pos;
    int enable;
    if (strncmp(tok, "no", 2) == 0) {
      enable = 0; tok += 2;
    } else {
      enable = 1;
    }
    if (strcmp(tok, "type") == 0)
      printtype = enable;
    else if (strcmp(tok, "finaltake") == 0)
      print_final_take_notice = enable;
    else if (strcmp(tok, "latency") == 0)
      print_latency = enable;
    else if (strncmp(tok, "latency=", 8) == 0 && enable)
    {
      print_latency = enable;
      latlog_fp = fopen("latlog.bin","wb");
    }
    else if (strcmp(tok, "dense") == 0)
      print_mode = TGPM_DENSE;
    else if (strcmp(tok, "space") == 0)
      print_mode = TGPM_SPACE;
    else if (strcmp(tok, "fields") == 0)
      print_mode = TGPM_FIELDS;
    else if (strcmp(tok, "multiline") == 0)
      print_mode = TGPM_MULTILINE;
    else if (sscanf(tok, "chop:%u%n", &chop, &pos) == 1 && tok[pos] == 0)
      print_chop = chop;
    else if (strcmp(tok, "tcp") == 0)
      print_tcp = enable;
    else
    {
      static struct { const char *name; unsigned flag; } tab[] = {
        { "meta", ~0u },
        { "trad", PM_PID | PM_TIME | PM_PHANDLE | PM_STIME | PM_STATE },
        { "pid", PM_PID },
        { "topic", PM_TOPIC },
        { "time", PM_TIME },
        { "phandle", PM_PHANDLE },
        { "ihandle", PM_IHANDLE },
        { "stime", PM_STIME },
        { "rtime", PM_RTIME },
        { "dgen", PM_DGEN },
        { "nwgen", PM_NWGEN },
        { "ranks", PM_RANKS },
        { "state", PM_STATE }
      };
      size_t i;
      for (i = 0; i < sizeof(tab)/sizeof(tab[0]); i++)
        if (strcmp(tok, tab[i].name) == 0)
          break;
      if (i < sizeof(tab)/sizeof(tab[0]))
      {
        if (enable)
          print_metadata |= tab[i].flag;
        else
          print_metadata &= ~tab[i].flag;
      }
      else
      {
        fprintf (stderr, "-P %s: invalid print mode\n", optarg);
        exit (3);
      }
    }
  }
  free (copy);
}

int main (int argc, char *argv[])
{
  DDS_Subscriber sub = DDS_HANDLE_NIL;
  DDS_Publisher pub = DDS_HANDLE_NIL;
  struct DDS_DataReaderListener rdlistener;
  DDS_StatusMask rdstatusmask = DDS_STATUS_MASK_NONE;
  struct DDS_DataWriterListener wrlistener;
  DDS_StatusMask wrstatusmask = DDS_STATUS_MASK_NONE;
  struct qos *qos;
  const char *qtopic[argc];
  const char *qreader[2+argc];
  const char *qwriter[2+argc];
  const char *qpublisher[2+argc];
  const char *qsubscriber[2+argc];
  int nqtopic = 0, nqreader = 0, nqwriter = 0;
  int nqpublisher = 0, nqsubscriber = 0;
  int opt, pos;
  uintptr_t exitcode = 0;
  int want_reader = 1;
  int want_writer = 1;
  int disable_signal_handlers = 0;
  unsigned sleep_at_end_1 = 0;
  unsigned sleep_at_end_2 = 0;
  unsigned sleep_at_beginning = 0;
  pthread_t sigtid;
  pthread_t inptid;
#define SPEC_TOPICSEL 1
#define SPEC_TOPICNAME 2
  unsigned spec_sofar = 0;
  unsigned specidx = 0;
  unsigned i;
  int statusmask_set = 0;
  double wait_for_matching_reader_timeout = 0.0;
  const char *wait_for_matching_reader_arg = NULL;
  struct spec *spec = NULL;
  struct wrspeclist *wrspecs = NULL;
#if ! PRE_V6_5
  /* the type changed from v5 to v6, but considering it is such a rarely used option making it dependent on >= 6.5 is more practical, extending its applicability is always possible later */
  DDS_DomainId_t domainid = DDS_DOMAIN_ID_DEFAULT;
#endif
  memset (&sigtid, 0, sizeof(sigtid));
  memset (&inptid, 0, sizeof(inptid));

  if (strcmp(execname(argc, argv), "sub") == 0) {
    want_writer = 0; fdin = -1;
  } else if(strcmp(execname(argc, argv), "pub") == 0) {
    want_reader = 0;
  }

  save_argv0 (argv[0]);
  pid = (int) getpid ();

  memset (&rdlistener, 0, sizeof (rdlistener));
  rdlistener.on_liveliness_changed = rd_on_liveliness_changed;
  rdlistener.on_sample_lost = rd_on_sample_lost;
  rdlistener.on_sample_rejected = rd_on_sample_rejected;
  rdlistener.on_subscription_matched = rd_on_subscription_matched;
  rdlistener.on_requested_deadline_missed = rd_on_requested_deadline_missed;
  rdlistener.on_requested_incompatible_qos = rd_on_requested_incompatible_qos;

  memset (&wrlistener, 0, sizeof (wrlistener));
  wrlistener.on_offered_deadline_missed = wr_on_offered_deadline_missed;
  wrlistener.on_liveliness_lost = wr_on_liveliness_lost;
  wrlistener.on_publication_matched = wr_on_publication_matched;
  wrlistener.on_offered_incompatible_qos = wr_on_offered_incompatible_qos;

  qreader[0] = "k=all";
  qreader[1] = "R=10000/inf/inf";
  nqreader = 2;

  qwriter[0] = "k=all";
  qwriter[1] = "R=100/inf/inf";
  nqwriter = 2;

  spec_sofar = SPEC_TOPICSEL;
  specidx--;
  addspec(SPEC_TOPICSEL, &spec_sofar, &specidx, &spec, want_reader);
  spec_sofar = 0;
  assert(specidx == 0);

  while ((opt = getopt (argc, argv, "^:$!@*:f:FK:T:D:q:m:M:n:OP:rRs:S:U:W:w:z:")) != EOF)
  {
    switch (opt)
    {
      case '!':
        disable_signal_handlers = 1;
        break;
      case '^':
        sleep_at_beginning = (unsigned) atoi (optarg);
        break;
      case '@':
        spec[specidx].wr.duplicate_writer_flag = 1;
        break;
      case '*':
        if (sscanf (optarg, "%u:%u%n", &sleep_at_end_1, &sleep_at_end_2, &pos) == 2 && optarg[pos] == 0)
          ;
        else if (sscanf (optarg, "%u%n", &sleep_at_end_2, &pos) == 1 && optarg[pos] == 0)
          sleep_at_end_1 = 0;
        else
        {
          fprintf (stderr, "-* %s: invalid sleep-at-end-setting\n", optarg);
          exit (3);
        }
        break;
      case 'M':
        if (sscanf(optarg, "%lf:%n", &wait_for_matching_reader_timeout, &pos) != 1)
        {
          fprintf (stderr, "-M %s: invalid timeout\n", optarg);
          exit (3);
        }
        wait_for_matching_reader_arg = optarg + pos;
        break;
      case 'f':
#if PRE_V6_5
        fprintf (stderr, "setting domain id is not supported for OSPL versions pre v6.5\n");
        exit (3);
#else
        domainid = atoi (optarg);
#endif
        break;
      case 'F':
        setvbuf (stdout, (char *) NULL, _IOLBF, 0);
        break;
      case 'K':
        addspec(SPEC_TOPICSEL, &spec_sofar, &specidx, &spec, want_reader);
        if (strcmp (optarg, "KS") == 0)
          spec[specidx].rd.topicsel = spec[specidx].wr.topicsel = KS;
        else if (strcmp (optarg, "K32") == 0)
          spec[specidx].rd.topicsel = spec[specidx].wr.topicsel = K32;
        else if (strcmp (optarg, "K64") == 0)
          spec[specidx].rd.topicsel = spec[specidx].wr.topicsel = K64;
        else if (strcmp (optarg, "K128") == 0)
          spec[specidx].rd.topicsel = spec[specidx].wr.topicsel = K128;
        else if (strcmp (optarg, "K256") == 0)
          spec[specidx].rd.topicsel = spec[specidx].wr.topicsel = K256;
        else if (strcmp (optarg, "OU") == 0)
          spec[specidx].rd.topicsel = spec[specidx].wr.topicsel = OU;
        else if (strcmp (optarg, "ARB") == 0)
          spec[specidx].rd.topicsel = spec[specidx].wr.topicsel = ARB;
        else if (get_metadata(&spec[specidx].metadata, &spec[specidx].typename, &spec[specidx].keylist, optarg))
          spec[specidx].rd.topicsel = spec[specidx].wr.topicsel = ARB;
        else
        {
          fprintf (stderr, "-K %s: unknown type\n", optarg);
          exit (3);
        }
        break;
      case 'T': {
        char *p;
        addspec(SPEC_TOPICNAME, &spec_sofar, &specidx, &spec, want_reader);
        spec[specidx].topicname = strdup(optarg);
        if ((p = strchr(spec[specidx].topicname, ':')) != NULL) {
          double d;
          int pos, have_to = 0;
          *p++ = 0;
          if (strcmp (p, "inf") == 0 || strncmp (p, "inf:", 4) == 0) {
            have_to = 1;
            set_infinite_dds_duration (&spec[specidx].findtopic_timeout);
          } else if (sscanf (p, "%lf%n", &d, &pos) == 1 && (p[pos] == 0 || p[pos] == ':')) {
            if (double_to_dds_duration (&spec[specidx].findtopic_timeout, d) < 0)
              error ("-T %s: %s: duration invalid\n", optarg, p);
            have_to = 1;
          } else {
            /* assume content filter */
          }
          if (have_to && (p = strchr (p, ':')) != NULL) {
            p++;
          }
        }
        if (p != NULL) {
          spec[specidx].cftp_expr = p;
        }
        break;
      }
      case 'q':
        if (strncmp(optarg, "provider=", 9) == 0) {
          set_qosprovider (optarg+9);
        } else {
          unsigned long n = strspn(optarg, "atrwps");
          const char *colon = strchr(optarg, ':');
          if (colon == NULL || n == 0 || n != (unsigned long) (colon - optarg)) {
            fprintf (stderr, "-q %s: flags indicating to which entities QoS's apply must match regex \"[^atrwps]+:\"\n", optarg);
            exit(3);
          } else {
            const char *q = colon+1;
            for (const char *flag = optarg; flag != colon; flag++)
              switch (*flag) {
                case 't': qtopic[nqtopic++] = q; break;
                case 'r': qreader[nqreader++] = q; break;
                case 'w': qwriter[nqwriter++] = q; break;
                case 'p': qpublisher[nqpublisher++] = q; break;
                case 's': qsubscriber[nqsubscriber++] = q; break;
                case 'a':
                  qtopic[nqtopic++] = q;
                  qreader[nqreader++] = q;
                  qwriter[nqwriter++] = q;
                  qpublisher[nqpublisher++] = q;
                  qsubscriber[nqsubscriber++] = q;
                  break;
              break;
                default:
                  assert(0);
              }
          }
        }
        break;
      case 'D':
        dur = atof (optarg);
        break;
      case 'm':
        spec[specidx].rd.polling = 0;
        if (strcmp (optarg, "0") == 0)
        { spec[specidx].rd.mode = MODE_NONE; }
        else if (strcmp (optarg, "p") == 0)
        { spec[specidx].rd.mode = MODE_PRINT; }
        else if (strcmp (optarg, "pp") == 0)
        { spec[specidx].rd.mode = MODE_PRINT; spec[specidx].rd.polling = 1; }
        else if (strcmp (optarg, "c") == 0)
        { spec[specidx].rd.mode = MODE_CHECK; }
        else if (sscanf (optarg, "c:%d%n", &nkeyvals, &pos) == 1 && optarg[pos] == 0)
        { spec[specidx].rd.mode = MODE_CHECK; }
        else if (strcmp (optarg, "cp") == 0)
        { spec[specidx].rd.mode = MODE_CHECK; spec[specidx].rd.polling = 1; }
        else if (sscanf (optarg, "cp:%d%n", &nkeyvals, &pos) == 1 && optarg[pos] == 0)
        { spec[specidx].rd.mode = MODE_CHECK; spec[specidx].rd.polling = 1; }
        else if (strcmp (optarg, "x") == 0)
        { spec[specidx].rd.mode = MODE_CHECK; spec[specidx].rd.exit_on_out_of_seq = 1; }
        else if (sscanf (optarg, "x:%d%n", &nkeyvals, &pos) == 1 && optarg[pos] == 0)
        { spec[specidx].rd.mode = MODE_CHECK; spec[specidx].rd.exit_on_out_of_seq = 1; }
        else if (strcmp (optarg, "xp") == 0)
        { spec[specidx].rd.mode = MODE_CHECK; spec[specidx].rd.polling = 1; spec[specidx].rd.exit_on_out_of_seq = 1; }
        else if (sscanf (optarg, "xp:%d%n", &nkeyvals, &pos) == 1 && optarg[pos] == 0)
        { spec[specidx].rd.mode = MODE_CHECK; spec[specidx].rd.polling = 1; spec[specidx].rd.exit_on_out_of_seq = 1; }
        else if (strcmp (optarg, "z") == 0)
        { spec[specidx].rd.mode = MODE_ZEROLOAD; }
        else if (strcmp (optarg, "d") == 0)
        { spec[specidx].rd.mode = MODE_DUMP; }
        else if (strcmp (optarg, "dp") == 0)
        { spec[specidx].rd.mode = MODE_DUMP; spec[specidx].rd.polling = 1; }
        else
        {
          fprintf (stderr, "-m %s: invalid mode\n", optarg);
          exit (3);
        }
        break;
      case 'w': {
        int port;
        spec[specidx].wr.writerate = 0.0;
        spec[specidx].wr.burstsize = 1;
        if (strcmp (optarg, "-") == 0)
        {
          if (fdin > 0) close (fdin);
          if (fdservsock != -1) { close (fdservsock); fdservsock = -1; }
          fdin = 0;
          spec[specidx].wr.mode = WM_INPUT;
        }
        else if (sscanf (optarg, "%d%n", &nkeyvals, &pos) == 1 && optarg[pos] == 0)
        {
          spec[specidx].wr.mode = (nkeyvals == 0) ? WM_NONE : WM_AUTO;
        }
        else if (sscanf (optarg, "%d:%lf*%u%n", &nkeyvals, &spec[specidx].wr.writerate, &spec[specidx].wr.burstsize, &pos) == 3 && optarg[pos] == 0)
        {
          spec[specidx].wr.mode = (nkeyvals == 0) ? WM_NONE : WM_AUTO;
        }
        else if (sscanf (optarg, "%d:%lf%n", &nkeyvals, &spec[specidx].wr.writerate, &pos) == 2 && optarg[pos] == 0)
        {
          spec[specidx].wr.mode = (nkeyvals == 0) ? WM_NONE : WM_AUTO;
        }
        else if (sscanf (optarg, ":%d%n", &port, &pos) == 1 && optarg[pos] == 0)
        {
          if (fdin > 0) close (fdin);
          if (fdservsock != -1) { close (fdservsock); fdservsock = -1; }
          fdservsock = open_tcpserver_sock (port);
          fdin = -1;
          spec[specidx].wr.mode = WM_INPUT;
        }
        else
        {
          if (fdin > 0) close (fdin);
          if (fdservsock != -1) { close (fdservsock); fdservsock = -1; }
          if ((fdin = open (optarg, O_RDONLY)) < 0)
          {
            fprintf (stderr, "%s: can't open\n", optarg);
            exit (3);
          }
          spec[specidx].wr.mode = WM_INPUT;
        }
        break;
      }
      case 'n':
        spec[specidx].rd.read_maxsamples = atoi (optarg);
        break;
      case 'O':
        once_mode = 1;
        break;
      case 'P':
        set_print_mode (optarg);
        break;
      case 'R':
        spec[specidx].rd.use_take = 0;
        break;
      case '$':
        if (!spec[specidx].rd.do_final_take)
          spec[specidx].rd.do_final_take = 1;
        else
          extra_readers_at_end = 1;
        break;
      case 'r':
        spec[specidx].wr.register_instances = 1;
        break;
      case 's':
        spec[specidx].rd.sleep_us = 1000u * (unsigned) atoi (optarg);
        break;
      case 'W':
        {
          double t;
          wait_hist_data = 1;
          if (strcmp (optarg, "inf") == 0)
            set_infinite_dds_duration (&wait_hist_data_timeout);
          else if (sscanf (optarg, "%lf%n", &t, &pos) == 1 && optarg[pos] == 0 && t >= 0)
            double_to_dds_duration (&wait_hist_data_timeout, t);
          else
          {
            fprintf (stderr, "-W %s: invalid duration\n", optarg);
            exit (3);
          }
        }
        break;
      case 'S':
        {
          static const struct { const char *a; const char *n; int isrd; DDS_StatusMask m; } s[] = {
            { "pr",  "pre-read", 1, 0 },
            { "sl",  "sample-lost", 1, DDS_SAMPLE_LOST_STATUS },
            { "sr",  "sample-rejected", 1, DDS_SAMPLE_REJECTED_STATUS },
            { "lc",  "liveliness-changed", 1, DDS_LIVELINESS_CHANGED_STATUS },
            { "sm",  "subscription-matched", 1, DDS_SUBSCRIPTION_MATCHED_STATUS },
            { "ll",  "liveliness-lost", 0, DDS_LIVELINESS_LOST_STATUS },
            { "odm", "offered-deadline-missed", 0, DDS_OFFERED_DEADLINE_MISSED_STATUS },
            { "pm",  "publication-matched", 0, DDS_PUBLICATION_MATCHED_STATUS },
            { "rdm", "requested-deadline-missed", 1, DDS_REQUESTED_DEADLINE_MISSED_STATUS },
            { "riq", "requested-incompatible-qos", 1, DDS_REQUESTED_INCOMPATIBLE_QOS_STATUS },
            { "oiq", "offered-incompatible-qos", 0, DDS_OFFERED_INCOMPATIBLE_QOS_STATUS }
          };
          char *copy = strdup (optarg), *tok, *lasts;
          if (copy == NULL)
            abort ();
          statusmask_set = 1;
          tok = strtok_r (copy, ",", &lasts);
          while (tok)
          {
            int i;
            for (i = 0; i < (int) (sizeof (s) / sizeof (*s)); i++)
              if (strcmp (tok, s[i].a) == 0 || strcmp (tok, s[i].n) == 0)
                break;
            if (i == 0)
              spec[specidx].rd.print_match_pre_read = 1;
            else if (i < (int) (sizeof (s) / sizeof (*s)))
            {
              DDS_StatusMask *x = s[i].isrd ? &rdstatusmask : &wrstatusmask;
              *x |= s[i].m;
            }
            else
            {
              fprintf (stderr, "-S %s: invalid event\n", tok);
              exit (3);
            }
            tok = strtok_r (NULL, ",", &lasts);
          }
          free (copy);
        }
        break;
      case 'z': {
        /* payload is int32 int32 seq<octet>, which we count as 16+N,
         for a 4 byte sequence length */
        int tmp = atoi (optarg);
        if (tmp != 0 && tmp < 12)
        {
          fprintf (stderr, "-z %s: minimum is 12\n", optarg);
          exit (3);
        }
        else if (tmp == 0)
          spec[specidx].wr.baggagesize = 0;
        else
          spec[specidx].wr.baggagesize = (unsigned) (tmp - 12);
        break;
      }
      default:
        usage (argv[0]);
    }
  }
  if (argc - optind < 1)
  {
    usage (argv[0]);
  }

  for (i = 0; i <= specidx; i++)
  {
    assert (spec[i].rd.topicsel == spec[i].wr.topicsel);
    if (spec[i].topicname != NULL)
    {
      if (spec[i].rd.topicsel == UNSPEC)
        spec[i].rd.topicsel = spec[i].wr.topicsel = ARB;
    }
    else
    {
      if (spec[i].rd.topicsel == UNSPEC)
        spec[i].rd.topicsel = spec[i].wr.topicsel = KS;
      switch (spec[i].rd.topicsel)
      {
        case UNSPEC: assert(0);
        case KS: spec[i].topicname = "PubSub"; break;
        case K32: spec[i].topicname = "PubSub32"; break;
        case K64: spec[i].topicname = "PubSub64"; break;
        case K128: spec[i].topicname = "PubSub128"; break;
        case K256: spec[i].topicname = "PubSub256"; break;
        case OU: spec[i].topicname = "PubSubOU"; break;
        case ARB: error ("-K ARB requires specifying a topic name\n"); break;
      }
      assert (spec[i].topicname != NULL);
    }
    assert(spec[i].rd.topicsel != UNSPEC && spec[i].rd.topicsel == spec[i].wr.topicsel);
  }

  if (wrstatusmask == DDS_STATUS_MASK_NONE)
  {
    want_writer = 0;
    want_reader = 0;
    for (i = 0; i <= specidx; i++)
    {
      if (spec[i].rd.mode != MODE_NONE)
        want_reader = 1;
      switch(spec[i].wr.mode)
      {
        case WM_NONE:
          break;
        case WM_AUTO:
          want_writer = 1;
          if (spec[i].wr.topicsel == ARB)
            error ("auto-write mode requires non-ARB topic\n");
          break;
        case WM_INPUT:
          want_writer = 1;
      }
    }
  }

  if (!statusmask_set)
  {
    wrstatusmask = DDS_OFFERED_INCOMPATIBLE_QOS_STATUS;
    rdstatusmask = DDS_REQUESTED_INCOMPATIBLE_QOS_STATUS;
  }

  for (i = 0; i <= specidx; i++)
  {
    if (spec[i].rd.topicsel == OU)
    {
      /* by definition only 1 instance for OneULong type */
      nkeyvals = 1;
      if (spec[i].rd.topicsel == ARB)
      {
        if (((spec[i].rd.mode != MODE_PRINT || spec[i].rd.mode != MODE_DUMP) && spec[i].rd.mode != MODE_NONE) || (fdin == -1 && fdservsock == -1))
          error ("-K ARB requires readers in PRINT or DUMP mode and writers in interactive mode\n");
        if (nqtopic != 0 && spec[i].metadata == NULL)
          error ("-K ARB disallows specifying topic QoS when using find_topic\n");
      }
    }
    if (spec[i].rd.mode == MODE_ZEROLOAD)
    {
      /* need to change to keep-last-1 (unless overridden by user) */
      qreader[0] = "k=1";
    }
  }

#if PRE_V6_5
  common_init (argv[0]);
#else
  common_init_domainid (argv[0], domainid);
#endif
  set_systemid_env ();

  if (sleep_at_beginning)
    sleep (sleep_at_beginning);

  {
    char *ps[argc - optind];
    for (i = 0; i < (unsigned) (argc - optind); i++)
      ps[i] = expand_envvars (argv[(unsigned) optind + i]);
    if (want_reader)
    {
      qos = new_subqos ();
      setqos_from_args (qos, nqsubscriber, qsubscriber);
      sub = new_subscriber (qos, (unsigned) (argc - optind), (const char **) ps);
      free_qos (qos);
    }
    if (want_writer)
    {
      qos = new_pubqos ();
      setqos_from_args (qos, nqpublisher, qpublisher);
      pub = new_publisher (qos, (unsigned) (argc - optind), (const char **) ps);
      free_qos (qos);
    }
    for (i = 0; i < (unsigned) (argc - optind); i++)
      free (ps[i]);
  }

  for (i = 0; i <= specidx; i++)
  {
    qos = new_tqos ();
    setqos_from_args (qos, nqtopic, qtopic);
    switch (spec[i].rd.topicsel)
    {
      case UNSPEC: assert(0); break;
      case KS:   spec[i].tp = new_topic (spec[i].topicname, ts_KeyedSeq, qos); break;
      case K32:  spec[i].tp = new_topic (spec[i].topicname, ts_Keyed32, qos); break;
      case K64:  spec[i].tp = new_topic (spec[i].topicname, ts_Keyed64, qos); break;
      case K128: spec[i].tp = new_topic (spec[i].topicname, ts_Keyed128, qos); break;
      case K256: spec[i].tp = new_topic (spec[i].topicname, ts_Keyed256, qos); break;
      case OU:   spec[i].tp = new_topic (spec[i].topicname, ts_OneULong, qos); break;
      case ARB:
        if (spec[i].metadata == NULL) {
          if ((spec[i].tp = find_topic(dp, spec[i].topicname, &spec[i].findtopic_timeout)) == NULL)
            error("topic %s not found\n", spec[i].topicname);
        } else  {
          DDS_ReturnCode_t result;
          DDS_TypeSupport ts = NULL;
          if ((ts = DDS_TypeSupport__alloc(spec[i].typename, spec[i].keylist, spec[i].metadata)) == NULL)
            error("DDS_TypeSupport__alloc(%s) failed\n", spec[i].typename);
          if ((result = DDS_TypeSupport_register_type(ts, dp, spec[i].typename)) != DDS_RETCODE_OK)
            error("DDS_TypeSupport_register_type(%s) failed: %d (%s)\n", spec[i].typename, (int) result, dds_strerror(result));
          spec[i].tp = new_topic (spec[i].topicname, ts, qos);
          DDS_free(ts);
        }
        spec[i].rd.tgtp = spec[i].wr.tgtp = tgnew(spec[i].tp, printtype);
        break;
    }
    assert (spec[i].tp != NULL);
    assert (spec[i].rd.topicsel != ARB || spec[i].rd.tgtp != NULL);
    assert (spec[i].wr.topicsel != ARB || spec[i].wr.tgtp != NULL);
    free_qos (qos);

    if (spec[i].cftp_expr == NULL)
      spec[i].cftp = spec[i].tp;
    else
    {
      char name[40], *expr = expand_envvars(spec[i].cftp_expr);
      DDS_StringSeq *params = DDS_StringSeq__alloc();
      snprintf (name, sizeof (name), "cft%u", i);
      if ((spec[i].cftp = DDS_DomainParticipant_create_contentfilteredtopic(dp, name, spec[i].tp, expr, params)) == NULL)
        error("DDS_DomainParticipant_create_contentfiltered_topic failed\n");
      DDS_free(params);
      free(expr);
    }

    if (spec[i].rd.mode != MODE_NONE)
    {
      qos = new_rdqos (sub, spec[i].cftp);
      setqos_from_args (qos, nqreader, qreader);
      spec[i].rd.rd = new_datareader_listener (qos, &rdlistener, rdstatusmask);
      free_qos (qos);
    }

    if (spec[i].wr.mode != WM_NONE)
    {
      qos = new_wrqos (pub, spec[i].tp);
      setqos_from_args (qos, nqwriter, qwriter);
      spec[i].wr.wr = new_datawriter_listener (qos, &wrlistener, wrstatusmask);
      if (spec[i].wr.duplicate_writer_flag)
      {
        if ((spec[i].wr.dupwr = DDS_Publisher_create_datawriter (pub, spec[i].tp, qos_datawriter(qos), NULL, DDS_STATUS_MASK_NONE)) == NULL)
          error ("DDS_Publisher_create_datawriter failed\n");
      }
      free_qos (qos);
    }
  }

  /* In case of coherent subscription */
  DDS_Entity_enable(sub);

  if (want_writer && wait_for_matching_reader_arg)
  {
    struct qos *q = NULL;
    uint64_t tnow = nowll();
    uint64_t tend = tnow + (uint64_t) (wait_for_matching_reader_timeout >= 0 ? (wait_for_matching_reader_timeout * 1e9 + 0.5) : 0);
    DDS_InstanceHandleSeq *sh = DDS_InstanceHandleSeq__alloc();
    DDS_InstanceHandle_t pphandle;
    DDS_ReturnCode_t ret;
    DDS_ParticipantBuiltinTopicData *ppdata = DDS_ParticipantBuiltinTopicData__alloc();
    const DDS_UserDataQosPolicy *udqos;
    unsigned m;
    if ((pphandle = DDS_DomainParticipant_get_instance_handle(dp)) == DDS_HANDLE_NIL)
      error("DDS_DomainParticipant_get_instance_handle failed\n");
    if ((ret = DDS_DomainParticipant_get_discovered_participant_data(dp, ppdata, pphandle)) != DDS_RETCODE_OK)
      error("DDS_DomainParticipant_get_discovered_participant_data failed: %d (%s)\n", (int) ret, dds_strerror(ret));
    q = new_wrqos(pub, spec[0].tp);
    qos_user_data(q, wait_for_matching_reader_arg);
    udqos = &qos_datawriter(q)->user_data;
    do {
      for (i = 0, m = specidx + 1; i <= specidx; i++)
      {
        if (spec[i].wr.mode == WM_NONE)
          --m;
        else if ((ret = DDS_DataWriter_get_matched_subscriptions(spec[i].wr.wr, sh)) != DDS_RETCODE_OK)
          error("DDS_DataWriter_get_matched_subscriptions failed: %d (%s)\n", (int) ret, dds_strerror(ret));
        else
        {
          unsigned j;
          for(j = 0; j < sh->_length; j++)
          {
            DDS_SubscriptionBuiltinTopicData *d = DDS_SubscriptionBuiltinTopicData__alloc();
            if ((ret = DDS_DataWriter_get_matched_subscription_data(spec[i].wr.wr, d, sh->_buffer[j])) != DDS_RETCODE_OK)
              error("DDS_DataWriter_get_matched_subscription_data(wr %u ih %llx) failed: %d (%s)\n", specidx, sh->_buffer[j], (int) ret, dds_strerror(ret));
            if (memcmp(d->participant_key, ppdata->key, sizeof(ppdata->key)) != 0 &&
                d->user_data.value._length == udqos->value._length &&
                (d->user_data.value._length == 0 || memcmp(d->user_data.value._buffer, udqos->value._buffer, udqos->value._length) == 0))
            {
              --m;
              DDS_free(d);
              break;
            }
            DDS_free(d);
          }
      }
      }
      tnow = nowll();
      if (m != 0 && tnow < tend)
      {
        uint64_t tdelta = (tend-tnow) < T_SECOND/10 ? tend-tnow : T_SECOND/10;
        os_time delay;
        delay.tv_sec = (os_timeSec) (tdelta / T_SECOND);
        delay.tv_nsec = (os_int32) (tdelta % T_SECOND);
        os_nanoSleep(delay);
        tnow = nowll();
      }
    } while(m != 0 && tnow < tend);
    free_qos(q);
    DDS_free(ppdata);
    DDS_free(sh);
    if (m != 0)
      error("timed out waiting for matching subscriptions\n");
  }

  if((termcond = DDS_GuardCondition__alloc()) == NULL)
    error("DDS_GuardCondition__alloc failed\n");

  if (pipe (termpipe) != 0)
    error("pipe(termpipe): errno %d\n", errno);

  if (!disable_signal_handlers)
  {
    if (pipe (sigpipe) != 0)
      error("pipe(sigpipe): errno %d\n", errno);
    pthread_create(&sigtid, NULL, sigthread, NULL);
    signal (SIGINT, sigh);
    signal (SIGTERM, sigh);
  }

  if (want_writer)
  {
    for (i = 0; i <= specidx; i++)
    {
      struct wrspeclist *wsl;
      switch (spec[i].wr.mode)
      {
        case WM_NONE:
          break;
        case WM_AUTO:
          pthread_create(&spec[i].wrtid, NULL, pubthread_auto, &spec[i].wr);
          break;
        case WM_INPUT:
          wsl = malloc(sizeof(*wsl));
          spec[i].wr.tpname = DDS_Topic_get_name(DDS_DataWriter_get_topic(spec[i].wr.wr));
          wsl->spec = &spec[i].wr;
          if (wrspecs) {
            wsl->next = wrspecs->next;
            wrspecs->next = wsl;
          } else {
            wsl->next = wsl;
          }
          wrspecs = wsl;
          break;
      }
    }
    if (wrspecs) /* start with first wrspec */
    {
      wrspecs = wrspecs->next;
      pthread_create(&inptid, NULL, pubthread, wrspecs);
    }
  }
  else if (dur > 0) /* note: abusing inptid */
  {
    pthread_create(&inptid, NULL, autotermthread, NULL);
  }
  for (i = 0; i <= specidx; i++)
  {
    if (spec[i].rd.mode != MODE_NONE)
    {
      spec[i].rd.idx = i;
      pthread_create(&spec[i].rdtid, NULL, subthread, &spec[i].rd);
    }
  }

  if (want_writer || dur > 0)
  {
    int term_called = 0;
    if (!want_writer || wrspecs)
    {
      pthread_join(inptid, NULL);
      term_called = 1;
      terminate ();
    }
    for (i = 0; i <= specidx; i++)
    {
      if (spec[i].wr.mode == WM_AUTO)
        pthread_join(spec[i].wrtid, NULL);
    }
    if (!term_called)
      terminate ();
  }
  if (want_reader)
  {
    void *ret;
    exitcode = 0;
    for (i = 0; i <= specidx; i++)
    {
      if (spec[i].rd.mode != MODE_NONE)
      {
        pthread_join (spec[i].rdtid, &ret);
        if ((uintptr_t) ret > exitcode)
          exitcode = (uintptr_t) ret;
      }
    }
    if (extra_readers_at_end)
    {
      /* FIXME: doesn't work for group coherent data */
      printf ("extra-at-end:\n");
      once_mode = 1;
      for (i = 0; i <= specidx; i++)
      {
        qos = new_rdqos (sub, spec[i].cftp);
        setqos_from_args (qos, nqreader, qreader);
        spec[i].rd.rd = new_datareader_listener (qos, &rdlistener, rdstatusmask);
        free_qos (qos);
        subthread (&spec[i].rd);
      }
    }
  }

  if (!disable_signal_handlers)
  {
    const char c = 0;
    write(sigpipe[1], &c, 1);
    pthread_join(sigtid, NULL);
    for(i = 0; i < 2; i++)
    {
      close(sigpipe[i]);
      close(termpipe[i]);
    }
  }

  if (wrspecs)
  {
    struct wrspeclist *m;
    m = wrspecs->next;
    wrspecs->next = NULL;
    wrspecs = m;
    while ((m = wrspecs) != NULL)
    {
      wrspecs = wrspecs->next;
      free(m);
    }
  }

  if (latlog_fp)
    fclose(latlog_fp);

  for (i = 0; i <= specidx; i++)
  {
    assert(spec[i].wr.tgtp == spec[i].rd.tgtp); /* so no need to free both */
    if (spec[i].rd.tgtp)
      tgfree(spec[i].rd.tgtp);
    if (spec[i].wr.tpname)
      DDS_free(spec[i].wr.tpname);
  }
  DDS_free(termcond);
  if (sleep_at_end_1)
    sleep (sleep_at_end_1);
  common_fini ();
  if (sleep_at_end_2)
    sleep (sleep_at_end_2);
  return (int) exitcode;
}
