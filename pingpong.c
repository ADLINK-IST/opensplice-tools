/*
 *                         OpenSplice DDS
 *
 *   This software and documentation are Copyright 2006 to 2009 PrismTech
 *   Limited and its licensees. All rights reserved. See file:
 *
 *                     $OSPL_HOME/LICENSE
 *
 *   for full copyright notice and license terms.
 *
 */

#include <time.h>
#include <string.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>

#include <dds_dcps.h>

#include "common.h"
#include "testtype.h"

enum side {
  SIDE_UNSET,
  SIDE_BOTH,
  SIDE_PING,
  SIDE_PONG
};

static const char *argv0;
static unsigned payloadsize = 0;
static int randomize_payloadsize = 0;
static int unreliable = 0;
static int nroundtrips = 0;
static int register_instances = 1;
static int use_statuscondition = 1;
static int do_sleeps = 0;
static char *ping_wr_part = NULL;
static char *pong_wr_part = NULL;
static unsigned long long tstart;
static unsigned long long tend = 0x7fffffffffffffffll;
static DDS_Topic tp;

static int sigpipe[2];
static pthread_t sigh_tid;
static DDS_GuardCondition sigguard[2];
static FILE *outfp = NULL;

static void *sigh_thread (void *arg)
{
  /* assuming 4-byte reads/writes are atomic (I think that's been true
     since the early days of Unix) */
  int n, sig;
  (void)arg;
  while ((n = (int)read (sigpipe[0], &sig, sizeof (sig))) > 0 || (n == -1 && errno == EINTR))
    if (n > 0)
    {
      if (sig == -1)
        break;
      else
      {
        int i;
        for (i = 0; i < 2; i++)
          DDS_GuardCondition_set_trigger_value (sigguard[i], 1);
      }
    }
  return 0;
}

static void sigh (int sig)
{
  /* see also sigh_thread comment */
  write (sigpipe[1], &sig, sizeof (sig));
}

static void usage (const char *argv0)
{
  printf ("usage: %s [OPTIONS] { PART_PRE | PING_WR_PART PONG_WR_PART }\n\
\n\
Common options:\n\
  -T TOPIC  set topic name to TOPIC (default: PingPong)\n\
  -Q QOS    set topic qos, see below\n\
  -s SIDE   SIDE is one of:\n\
              ping   generate messages for pong to echo\n\
              pong   echoes whatever it receives\n\
              both   run in a single thread measuring overhead\n\
              sp     run ping & pong as threads in a single process\n\
            if the executable is 'ping' or 'pong', the default side\n\
            is set accordingly, else there is no default\n\
  -R        use read condition instead of statuscondition\n\
  -S        insert 10ms sleeps in ping\n\
  -u        unreliable communications\n\
  -n        no registering of instances with the writers\n\
\n\
Ping-specific options:\n\
  -D DUR    run for at most DUR seconds\n\
  -N CNT    perform at most CNT round-trips\n\
  -o FILE   outputs all measured round-trip times to text FILE\n\
  -z SIZE   ping payload size in bytes\n\
  -Z SIZE   random ping payload sizes, but at most SIZE bytes\n\
\n\
%s\
\n\
Partitions used are PING_WR_PART and PONG_WR_PART: ping publishes in\n\
the first partition and subscribes in the second, pong subscribes in\n\
the first and publishes in the second.\n\
\n\
For a simple latency test, one may simply specify a single partition\n\
prefix, PART_PRE, which is then used to construct PING_WR_PART and\n\
PONG_WR_PART by appending _PING and _PONG to the specified prefix.\n\
\n\
If SIDE = both, only PING_WR_PART is used.\n\
", argv0, qos_arg_usagestr);
  exit (1);
}

#define MAX_PONGS 16

struct latency_admin {
  double dtsum, dtmin;
  int count;
  uint32_t seq;
};

struct pings_pong_admin {
  DDS_InstanceHandle_t pubhandles[MAX_PONGS];
  struct latency_admin lats[MAX_PONGS];
  unsigned long long tprint;
};

static void reinit_latency_admin (struct latency_admin *la)
{
  la->dtsum = 0;
  la->dtmin = 1e10;
  la->count = 0;
}

static void init_latency_admin (struct latency_admin *la)
{
  reinit_latency_admin (la);
  la->seq = 0;
}

static void init_pings_pong_admin (struct pings_pong_admin *ppa)
{
  int i;
  for (i = 0; i < MAX_PONGS; i++)
  {
    ppa->pubhandles[i] = DDS_HANDLE_NIL;
    init_latency_admin(&ppa->lats[i]);
  }
  ppa->tprint = 0;
}

static void instancehandle_to_id (uint32_t *systemId, uint32_t *localId, DDS_InstanceHandle_t h)
{
  /* Undocumented and unsupported trick */
  union { struct { uint32_t systemId, localId; } s; DDS_InstanceHandle_t h; } u;
  u.h = h;
  *systemId = u.s.systemId & ~0x80000000;
  *localId = u.s.localId;
}

static void print_latencies (struct pings_pong_admin *ppa)
{
  unsigned long long t = nowll ();
  if (ppa->tprint == 0)
    ppa->tprint = t;
  else if (t >= ppa->tprint + 1000000000)
  {
    int i;
    ppa->tprint = t;
    for (i = 0; i < MAX_PONGS; i++)
      if (ppa->pubhandles[i] != DDS_HANDLE_NIL)
      {
        struct latency_admin *la = &ppa->lats[i];
        uint32_t systemId, localId;
        instancehandle_to_id (&systemId, &localId, ppa->pubhandles[i]);
        printf ("%" PRIx32 ":%" PRIx32 ":  %d rtts %g us avg %g us min\n", systemId, localId, la->count, 1e6 * la->dtsum / la->count, la->dtmin * 1e6);
        reinit_latency_admin (la);
      }
  }
}

static void record_latency (struct latency_admin *la, unsigned sz, double dt, uint32_t seq)
{
  if (outfp)
    fprintf (outfp, "%u %.9e\n", sz, dt);

  if (dt < la->dtmin)
    la->dtmin = dt;
  la->dtsum += dt;
  la->count++;
  la->seq = seq;
}

static unsigned randomsize (unsigned max)
{
  unsigned sz = (unsigned) (exp (drand48 () * log (max)) + 0.5);
  assert (sz >= 0 && sz <= max);
  return sz;
}

static DDS_ReturnCode_t ping_once (KeyedSeqDataWriter wr, KeyedSeq *d0, DDS_InstanceHandle_t handle)
{
  DDS_Time_t tstamp;
  DDS_ReturnCode_t result;
  if (randomize_payloadsize)
    d0->baggage._length = randomsize (d0->baggage._maximum);
  d0->seq++;
  nowll_as_ddstime (&tstamp);
  if ((result = KeyedSeqDataWriter_write_w_timestamp (wr, d0, handle, &tstamp)) != DDS_RETCODE_OK)
    fprintf (stderr, "write: error %d (%s)\n", (int) result, dds_strerror (result));
  return result;
}

struct pingpong_arg {
  DDS_GuardCondition sigguard;
  enum side side;
};

static void unregister_pong (struct pings_pong_admin *adm, DDS_InstanceHandle_t pubhandle)
{
  int i;
  for (i = 0; i < MAX_PONGS; i++)
  {
    if (adm->pubhandles[i] == pubhandle)
    {
      adm->pubhandles[i] = DDS_HANDLE_NIL;
      return;
    }
  }
}

static int lookup_pong (struct pings_pong_admin *ppa, DDS_InstanceHandle_t pubhandle)
{
  int i, newidx = -1;
  uint32_t systemId, localId;
  for (i = 0; i < MAX_PONGS; i++)
  {
    if (ppa->pubhandles[i] == pubhandle)
      return i;
    else if (ppa->pubhandles[i] == DDS_HANDLE_NIL && newidx < 0)
      newidx = i;
  }
  instancehandle_to_id (&systemId, &localId, pubhandle);
  printf ("new pong: %" PRIx32 ":%" PRIx32 "\n", systemId, localId);
  if (newidx == -1)
    error ("max pongs reached\n");
  ppa->pubhandles[newidx] = pubhandle;
  init_latency_admin (&ppa->lats[newidx]);
  return newidx;
}

static int all_pongs_responded (const struct pings_pong_admin *ppa)
{
  int i, j;
  for (i = 0; i < MAX_PONGS; i++)
  {
    if (ppa->pubhandles[i] != DDS_HANDLE_NIL)
      break;
  }
  if (i == MAX_PONGS)
    return 1;
  for (j = i + 1; j < MAX_PONGS; j++)
  {
    if (ppa->pubhandles[j] != DDS_HANDLE_NIL && ppa->lats[j].seq != ppa->lats[i].seq)
      return 0;
  }
  return 1;
}

static void pingpong (enum side side, DDS_GuardCondition sigguard)
{
  DDS_Publisher p;
  DDS_Subscriber s;
  KeyedSeqDataWriter wr;
  KeyedSeqDataReader rd;
  DDS_WaitSet ws;
  void *cond; /* Read or Status */
  DDS_InstanceHandle_t handle;
  DDS_ReturnCode_t result;
  const char *partrd;
  const char *partwr;
  struct qos *qos;
  struct pings_pong_admin ppa;
  init_pings_pong_admin(&ppa);

  switch (side)
  {
    case SIDE_UNSET:
      abort ();
    case SIDE_PING:
      partwr = ping_wr_part;
      partrd = pong_wr_part;
      break;
    case SIDE_PONG:
      partwr = pong_wr_part;
      partrd = ping_wr_part;
      break;
    case SIDE_BOTH:
      partwr = partrd = ping_wr_part;
      break;
  }
  p = new_publisher (NULL, 1, &partwr);
  s = new_subscriber (NULL, 1, &partrd);

  qos = new_rdqos (s, tp);
  qos_history (qos, "all");
  qos_reliability (qos, unreliable ? "n" : "y:inf");
  rd = new_datareader (qos);
  free_qos (qos);

  qos = new_wrqos (p, tp);
  qos_history (qos, "all");
  qos_reliability (qos, unreliable ? "n" : "y:inf");
  wr = new_datawriter (qos);
  free_qos (qos);

  ws = DDS_WaitSet__alloc ();
  if ((result = DDS_WaitSet_attach_condition (ws, sigguard)) != DDS_RETCODE_OK)
    error ("DDS_WaitSet_attach_condition(<sigguard>) failed: %d (%s)\n", (int) result, dds_strerror (result));
  if (use_statuscondition)
  {
    cond = DDS_DataReader_get_statuscondition (rd);
    DDS_StatusCondition_set_enabled_statuses (cond, DDS_DATA_AVAILABLE_STATUS);
  }
  else
  {
    cond = DDS_DataReader_create_readcondition (rd, DDS_ANY_SAMPLE_STATE, DDS_ANY_VIEW_STATE, DDS_ANY_INSTANCE_STATE);
  }
  if ((result = DDS_WaitSet_attach_condition (ws, cond)) != DDS_RETCODE_OK)
    error ("DDS_WaitSet_attach_condition(<cond>) failed: %d (%s)\n", (int) result, dds_strerror (result));

  /* first register instance */
  if (!register_instances)
    handle = DDS_HANDLE_NIL;
  else
  {
    KeyedSeq tmpl;
    memset (&tmpl, 0, sizeof (tmpl));
    tmpl.keyval = 0; /* superfluous, but explicit */
    handle = KeyedSeqDataWriter_register_instance (wr, &tmpl);
    if (handle == DDS_HANDLE_NIL)
      error ("register_instance: error\n");
  }

  /* main loop */
  {
    DDS_sequence_KeyedSeq *mseq = DDS_sequence_KeyedSeq__alloc ();
    DDS_SampleInfoSeq *iseq = DDS_SampleInfoSeq__alloc ();
    DDS_ConditionSeq *glist = DDS_ConditionSeq__alloc ();
    DDS_Duration_t timeout;
    KeyedSeq *d0 = NULL;
    int terminate = 0;

    if (side == SIDE_PONG)
    {
      timeout.sec = DDS_DURATION_INFINITE_SEC;
      timeout.nanosec = DDS_DURATION_INFINITE_NSEC;
    }
    else
    {
      timeout.sec = 2;
      timeout.nanosec = 0;
      d0 = KeyedSeq__alloc ();
      d0->keyval = 0;
      d0->seq = 0;
      d0->baggage._maximum = payloadsize;
      d0->baggage._length = d0->baggage._maximum;
      d0->baggage._buffer = DDS_sequence_octet_allocbuf (d0->baggage._maximum);
      d0->baggage._release = 0;
      memset (d0->baggage._buffer, 0xee, d0->baggage._maximum);
      if (ping_once (wr, d0, handle) != DDS_RETCODE_OK)
        goto out;
    }

    while (!terminate)
    {
      unsigned gi;

      if ((result = DDS_WaitSet_wait (ws, glist, &timeout)) != DDS_RETCODE_OK && result != DDS_RETCODE_TIMEOUT) {
        fprintf (stderr, "%s: wait: error %d (%s)\n", argv0, (int) result, dds_strerror (result));
        goto out;
      }

      if (result == DDS_RETCODE_TIMEOUT && side != SIDE_PONG)
      {
        printf ("wait: timeout\n");
        if (ping_once (wr, d0, handle) != DDS_RETCODE_OK)
          goto out;
      }

      //usleep(1000);
      for (gi = 0; gi < glist->_length; gi++)
      {
        DDS_unsigned_long i;

        if (glist->_buffer[gi] == sigguard)
        {
          terminate = 1;
        }

        if (glist->_buffer[gi] == cond)
        {
          result = KeyedSeqDataReader_take (rd, mseq, iseq, DDS_LENGTH_UNLIMITED, DDS_ANY_SAMPLE_STATE, DDS_ANY_VIEW_STATE, DDS_ANY_INSTANCE_STATE);
          if (result != DDS_RETCODE_OK)
          {
            fprintf (stderr, "%s: take: error %d (%s)\n", argv0, (int) result, dds_strerror (result));
            goto out;
          }

          for (i = 0; i < iseq->_length; i++)
          {
            if (!iseq->_buffer[i].valid_data)
            {
              if (side == SIDE_PING)
                unregister_pong (&ppa, iseq->_buffer[i].publication_handle);
              /* ignore invalid data */
              continue;
            }
            else if (side == SIDE_PONG)
            {
              static os_uint32 last_seq = 0;
              const KeyedSeq * const d1 = &mseq->_buffer[i];
              if (d1->seq != last_seq + 1)
                printf ("seq input: %u expected: %u\n", (unsigned) d1->seq, (unsigned) (last_seq + 1));
              last_seq = d1->seq;
              result = KeyedSeqDataWriter_write_w_timestamp (wr, d1, handle, &iseq->_buffer[i].source_timestamp);
            }
            else
            {
              const KeyedSeq * const d1 = &mseq->_buffer[i];
              unsigned long long t0, t1;
              int pongidx;
              t1 = nowll ();
              t0 = (unsigned long long) (iseq->_buffer[i].source_timestamp.sec * 1000000000ll + iseq->_buffer[i].source_timestamp.nanosec);
              pongidx = lookup_pong (&ppa, iseq->_buffer[i].publication_handle);
              record_latency (&ppa.lats[pongidx], d1->baggage._length, (t1 - t0) / 1e9, d1->seq);
            }
          }
          KeyedSeqDataReader_return_loan (rd, mseq, iseq);

          print_latencies (&ppa);
          if (side != SIDE_PONG && all_pongs_responded(&ppa))
          {
            if ((nroundtrips > 0 && --nroundtrips == 0) || nowll() >= tend)
              terminate = 1;
            if (do_sleeps)
            {
              struct timespec ts = { 0, 10 * 1000 * 1000 };
              nanosleep (&ts, NULL);
            }
            result = ping_once (wr, d0, handle);
            if (result != DDS_RETCODE_OK)
            {
              fprintf (stderr, "%s: write: error %d (%s)\n", argv0, (int) result, dds_strerror (result));
              goto out;
            }
          }
        }
      }
    }
  out:
    if (side != SIDE_PONG)
    {
      DDS_free (d0->baggage._buffer);
      DDS_free (d0);
    }
    DDS_free (glist);
    DDS_free (iseq);
    DDS_free (mseq);
  }

  if ((result = DDS_WaitSet_detach_condition (ws, sigguard)) != DDS_RETCODE_OK)
    error ("DDS_WaitSet_detach_condition(<sigguard>) failed: %d (%s)\n", result, dds_strerror (result));
  if ((result = DDS_WaitSet_detach_condition (ws, cond)) != DDS_RETCODE_OK)
    error ("DDS_WaitSet_detach_condition(<cond>) failed: %d (%s)\n", result, dds_strerror (result));

  if (!use_statuscondition)
    DDS_DataReader_delete_readcondition (rd, cond);
  DDS_free (ws);

  DDS_Subscriber_delete_datareader (s, rd);
  DDS_Publisher_delete_datawriter (p, wr);
  DDS_DomainParticipant_delete_subscriber (dp, s);
  DDS_DomainParticipant_delete_publisher (dp, p);
}

static void *pingpong_thread (void *varg)
{
  struct pingpong_arg *arg = varg;
  pingpong (arg->side, arg->sigguard);
  return NULL;
}

int main (int argc, char *argv[])
{
  struct pingpong_arg a0, a1;
  const char *topicname = "PingPong";
  const char *qtopic[argc];
  struct qos *qos;
  int nqtopic = 0;
  int i, opt;

  argv0 = argv[0];
  tstart = nowll ();

  a0.side = a1.side = SIDE_UNSET;

  while ((opt = getopt (argc, argv, "D:nN:o:Q:Rs:ST:uz:Z:")) != EOF)
    switch (opt)
    {
      case 'T':
        topicname = optarg;
        break;
      case 'Q':
        qtopic[nqtopic++] = optarg;
        break;
      case 'D':
        tend = tstart + (unsigned long long) (1e9 * atof (optarg));
        break;
      case 'n':
        register_instances = 0;
        break;
      case 'N':
        nroundtrips = atoi (optarg);
        break;
      case 'o':
        if ((outfp = fopen (optarg, "w")) == NULL)
        {
          fprintf (stderr, "%s: can't open file for writing\n", optarg);
          return 1;
        }
        break;
      case 's':
        if (strcmp (optarg, "ping") == 0)
          a0.side = SIDE_PING;
        else if (strcmp (optarg, "pong") == 0)
          a0.side = SIDE_PONG;
        else if (strcmp (optarg, "both") == 0)
          a0.side = SIDE_BOTH;
        else if (strcmp (optarg, "sp") == 0)
        {
          a0.side = SIDE_PING;
          a1.side = SIDE_PONG;
        }
        else
        {
          fprintf (stderr, "%c%s: valid values are 'ping', 'pong', 'both' and 'sp'\n", opt, optarg);
          return 1;
        }
        break;
      case 'S':
        do_sleeps = 1;
        break;
      case 'R':
        use_statuscondition = 0;
        break;
      case 'u':
        unreliable = 1;
        break;
      case 'z':
      case 'Z':
        payloadsize = (unsigned) atoi (optarg);
        randomize_payloadsize = (opt == 'Z');
        break;
      default:
        usage (argv[0]);
    }
  if (optind + 1 == argc) {
    size_t sz = strlen (argv[optind]) + 6;
    ping_wr_part = malloc (sz);
    pong_wr_part = malloc (sz);
    sprintf (ping_wr_part, "%s_PING", argv[optind]);
    sprintf (pong_wr_part, "%s_PONG", argv[optind]);
  } else if (optind + 2 == argc) {
    ping_wr_part = argv[optind];
    pong_wr_part = argv[optind+1];
  } else {
    usage (argv[0]);
  }

  if (a0.side == SIDE_UNSET)
  {
    char *basename, *end;
    size_t len;
    if ((basename = strrchr (argv[0], '/')) != NULL)
      basename++;
    else
      basename = argv[0];
    if ((end = strchr (basename, '-')) != NULL)
      len = (size_t) (end - basename);
    else
      len = strlen (basename);
    if (strncmp (basename, "ping", len) == 0 && (basename[len] == '-' || basename[len] == 0))
      a0.side = SIDE_PING;
    else if (strncmp (basename, "pong", len) == 0 && (basename[len] == '-' || basename[len] == 0))
      a0.side = SIDE_PONG;
    else
    {
      fprintf (stderr, "%s: don't know which side I'm on\n", argv[0]);
      return 1;
    }
  }
  assert (a0.side == SIDE_PING || a0.side == SIDE_PONG || a0.side == SIDE_BOTH);

  common_init (argv[0]);

  qos = new_tqos ();
  qos_destination_order(qos, "r");
  setqos_from_args (qos, nqtopic, qtopic);
  tp = new_topic_KeyedSeq (topicname, qos);
  free_qos (qos);

  if (pipe (sigpipe) == -1)
  {
    perror ("pipe");
    return 1;
  }
  pthread_create (&sigh_tid, NULL, sigh_thread, NULL);
  signal (SIGINT, sigh);
  signal (SIGTERM, sigh);

  for (i = 0; i < 2; i++)
    if ((sigguard[i] = DDS_GuardCondition__alloc ()) == NULL)
      error ("DDS_GuardCondition__alloc failed\n");
  a0.sigguard = sigguard[0];
  a1.sigguard = sigguard[1];

  switch (a1.side)
  {
    case SIDE_UNSET:
      pingpong (a0.side, sigguard[0]);
      break;
    case SIDE_PONG:
      {
        pthread_t s0_tid, s1_tid;
        pthread_create (&s0_tid, NULL, pingpong_thread, &a0);
        pthread_create (&s1_tid, NULL, pingpong_thread, &a1);
        pthread_join (s0_tid, NULL);
        DDS_GuardCondition_set_trigger_value (a1.sigguard, 1);
        pthread_join (s1_tid, NULL);
      }
      break;
    default:
      abort ();
  }

  {
    int sig = -1;
    write (sigpipe[1], &sig, sizeof (sig));
    pthread_join (sigh_tid, NULL);
  }

  for (i = 0; i < 2; i++)
    DDS_free (sigguard[i]);

  common_fini ();
  return 0;
}
