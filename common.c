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
#include <dlfcn.h>
#include <time.h>
#include <string.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <signal.h>
#include <unistd.h>
#include <stdarg.h>

#include "dds_dcps.h"
#include "testtype.h"
#include "common.h"
#include "porting.h"

#ifndef DDS_DOMAIN_ID_DEFAULT /* pre-6.0 */
#define DDS_DOMAIN_ID_DEFAULT 0
#endif

enum qostype {
  QT_TOPIC,
  QT_PUBLISHER,
  QT_SUBSCRIBER,
  QT_READER,
  QT_WRITER
};

struct qos {
  enum qostype qt;
  union {
    struct {
      DDS_TopicQos *q;
    } topic;
    struct {
      DDS_PublisherQos *q;
    } pub;
    struct {
      DDS_SubscriberQos *q;
    } sub;
    struct {
      DDS_Topic t;
      DDS_Subscriber s;
      DDS_DataReaderQos *q;
    } rd;
    struct {
      DDS_Topic t;
      DDS_Publisher p;
      DDS_DataWriterQos *q;
    } wr;
  } u;
};

DDS_DomainParticipantFactory dpf = DDS_OBJECT_NIL;
DDS_DomainParticipant dp = DDS_OBJECT_NIL;
DDS_TypeSupport ts_KeyedSeq = DDS_OBJECT_NIL;
DDS_TypeSupport ts_Keyed32 = DDS_OBJECT_NIL;
DDS_TypeSupport ts_Keyed64 = DDS_OBJECT_NIL;
DDS_TypeSupport ts_Keyed128 = DDS_OBJECT_NIL;
DDS_TypeSupport ts_Keyed256 = DDS_OBJECT_NIL;
DDS_TypeSupport ts_OneULong = DDS_OBJECT_NIL;
const char *saved_argv0;

unsigned long long nowll (void)
{
  os_time t = os_timeGet ();
  return (unsigned long long) (t.tv_sec * 1000000000ll + t.tv_nsec);
}

void nowll_as_ddstime (DDS_Time_t *t)
{
  os_time ost = os_timeGet ();
  t->sec = ost.tv_sec;
  t->nanosec = (DDS_unsigned_long) ost.tv_nsec;
}

void bindelta (unsigned long long *bins, unsigned long long d, unsigned repeat)
{
  int bin = 0;
  while (d)
  {
    bin++;
    d >>= 1;
  }
  bins[bin] += repeat;
}

void binprint (unsigned long long *bins, unsigned long long telapsed)
{
  unsigned long long n;
  unsigned i, minbin = BINS_LENGTH- 1, maxbin = 0;
  n = 0;
  for (i = 0; i < BINS_LENGTH; i++)
  {
    n += bins[i];
    if (bins[i] && i < minbin)
      minbin = i;
    if (bins[i] && i > maxbin)
      maxbin = i;
  }
  printf ("< 2**n | %llu in %.06fs avg %.1f/s\n", n, telapsed * 1e-9, n / (telapsed * 1e-9));
  for (i = minbin; i <= maxbin; i++)
  {
    static const char ats[] = "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@";
    double pct = 100.0 * (double) bins[i] / n;
    int nats = (int) ((pct / 100.0) * (sizeof (ats) - 1));
    printf ("%2d: %6.2f%% %*.*s\n", i, pct, nats, nats, ats);
  }
}

struct hist {
  unsigned nbins;
  uint64_t binwidth;
  uint64_t bin0; /* bins are [bin0,bin0+binwidth),[bin0+binwidth,bin0+2*binwidth) */
  uint64_t binN; /* bin0 + nbins*binwidth */
  uint64_t min, max; /* min and max observed since last reset */
  uint64_t under, over; /* < bin0, >= binN */
  uint64_t bins[];
};

struct hist *hist_new (unsigned nbins, uint64_t binwidth, uint64_t bin0)
{
  struct hist *h = malloc (sizeof (*h) + nbins * sizeof (*h->bins));
  h->nbins = nbins;
  h->binwidth = binwidth;
  h->bin0 = bin0;
  h->binN = h->bin0 + h->nbins * h->binwidth;
  hist_reset (h);
  return h;
}

void hist_free (struct hist *h)
{
  free (h);
}

void hist_reset_minmax (struct hist *h)
{
  h->min = UINT64_MAX;
  h->max = 0;
}

void hist_reset (struct hist *h)
{
  hist_reset_minmax (h);
  h->under = 0;
  h->over = 0;
  memset (h->bins, 0, h->nbins * sizeof (*h->bins));
}

void hist_record (struct hist *h, uint64_t x, unsigned weight)
{
  if (x < h->min)
    h->min = x;
  if (x > h->max)
    h->max = x;
  if (x < h->bin0)
    h->under += weight;
  else if (x >= h->binN)
    h->over += weight;
  else
    h->bins[(x - h->bin0) / h->binwidth] += weight;
}

static void xsnprintf (char *buf, size_t bufsz, size_t *p, const char *fmt, ...)
{
  if (*p < bufsz)
  {
    int n;
    va_list ap;
    va_start(ap, fmt);
    n = vsnprintf(buf + *p, bufsz - *p, fmt, ap);
    va_end(ap);
    *p += (size_t)n;
  }
}

void hist_print (struct hist *h, uint64_t dt, int reset)
{
  char l[h->nbins + 200];
  char hist[h->nbins+1];
  double dt_s = dt / 1e9, avg;
  uint64_t peak = 0, cnt = h->under + h->over;
  size_t p = 0;
  hist[h->nbins] = 0;
  for (unsigned i = 0; i < h->nbins; i++)
  {
    cnt += h->bins[i];
    if (h->bins[i] > peak)
      peak = h->bins[i];
  }

  const uint64_t p1 = peak / 100;
  const uint64_t p10 = peak / 10;
  const uint64_t p20 = 1 * peak / 5;
  const uint64_t p40 = 2 * peak / 5;
  const uint64_t p60 = 3 * peak / 5;
  const uint64_t p80 = 4 * peak / 5;
  for (unsigned i = 0; i < h->nbins; i++)
  {
    if (h->bins[i] == 0) hist[i] = ' ';
    else if (h->bins[i] <= p1) hist[i] = '.';
    else if (h->bins[i] <= p10) hist[i] = '_';
    else if (h->bins[i] <= p20) hist[i] = '-';
    else if (h->bins[i] <= p40) hist[i] = '=';
    else if (h->bins[i] <= p60) hist[i] = 'x';
    else if (h->bins[i] <= p80) hist[i] = 'X';
    else hist[i] = '@';
  }

  avg = cnt / dt_s;
  if (avg < 999.5)
    xsnprintf (l, sizeof(l), &p, "%5.3g", avg);
  else if (avg < 1e6)
    xsnprintf (l, sizeof(l), &p, "%4.3gk", avg / 1e3);
  else
    xsnprintf (l, sizeof(l), &p, "%4.3gM", avg / 1e6);
  xsnprintf (l, sizeof(l), &p, "/s (");

  if (cnt < (uint64_t) 10e3)
    xsnprintf (l, sizeof(l), &p, "%5"PRIu64" ", cnt);
  else if (cnt < (uint64_t) 1e6)
    xsnprintf (l, sizeof(l), &p, "%5.1fk", cnt / 1e3);
  else
    xsnprintf (l, sizeof(l), &p, "%5.1fM", cnt / 1e6);

  xsnprintf (l, sizeof(l), &p, " in %.1fs) ", dt_s);

  if (h->min == UINT64_MAX)
    xsnprintf (l, sizeof(l), &p, " inf ");
  else if (h->min < 1000)
    xsnprintf (l, sizeof(l), &p, "%3"PRIu64"n ", h->min);
  else if (h->min + 500 < 1000000)
    xsnprintf (l, sizeof(l), &p, "%3"PRIu64"u ", (h->min + 500) / 1000);
  else if (h->min + 500000 < 1000000000)
    xsnprintf (l, sizeof(l), &p, "%3"PRIu64"m ", (h->min + 500000) / 1000000);
  else
    xsnprintf (l, sizeof(l), &p, "%3"PRIu64"s ", (h->min + 500000000) / 1000000000);

  if (h->bin0 > 0)
  {
    int pct = (cnt == 0) ? 0 : 100 * (int) ((h->under + cnt/2) / cnt);
    xsnprintf (l, sizeof(l), &p, "%3d%% ", pct);
  }

  {
    int pct = (cnt == 0) ? 0 : 100 * (int) ((h->over + cnt/2) / cnt);
    xsnprintf (l, sizeof(l), &p, "|%s| %3d%%", hist, pct);
  }

  if (h->max < 1000)
    xsnprintf (l, sizeof(l), &p, " %3"PRIu64"n", h->max);
  else if (h->max + 500 < 1000000)
    xsnprintf (l, sizeof(l), &p, " %3"PRIu64"u", (h->max + 500) / 1000);
  else if (h->max + 500000 < 1000000000)
    xsnprintf (l, sizeof(l), &p, " %3"PRIu64"m", (h->max + 500000) / 1000000);
  else
    xsnprintf (l, sizeof(l), &p, " %3"PRIu64"s", (h->max + 500000000) / 1000000000);

  (void) p;
  puts (l);
  if (reset)
    hist_reset (h);
}

void error (const char *fmt, ...)
{
  va_list ap;
  fprintf (stderr, "%s: error: ", saved_argv0);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  exit (2);
}

const char *dds_strerror (DDS_ReturnCode_t code)
{
  switch (code)
  {
    case DDS_RETCODE_OK: return "ok";
    case DDS_RETCODE_ERROR: return "error";
    case DDS_RETCODE_UNSUPPORTED: return "unsupported";
    case DDS_RETCODE_BAD_PARAMETER: return "bad parameter";
    case DDS_RETCODE_PRECONDITION_NOT_MET: return "precondition not met";
    case DDS_RETCODE_OUT_OF_RESOURCES: return "out of resources";
    case DDS_RETCODE_NOT_ENABLED: return "not enabled";
    case DDS_RETCODE_IMMUTABLE_POLICY: return "immutable policy";
    case DDS_RETCODE_INCONSISTENT_POLICY: return "inconsistent policy";
    case DDS_RETCODE_ALREADY_DELETED: return "already deleted";
    case DDS_RETCODE_TIMEOUT: return "timeout";
    case DDS_RETCODE_NO_DATA: return "no data";
    case DDS_RETCODE_ILLEGAL_OPERATION: return "illegal operation";
    default: return "(undef)";
  }
}

DDS_TypeSupport register_type (const char *name)
{
  static const char a_suf[] = "TypeSupport__alloc";
  static const char r_suf[] = "TypeSupport_register_type";
  char a_name[strlen (name) + sizeof (a_suf)];
  char r_name[strlen (name) + sizeof (r_suf)];
  union { void *d; DDS_TypeSupport (*f) (void); } a;
  union { void *d; DDS_ReturnCode_t (*f) (DDS_TypeSupport ts, DDS_DomainParticipant dp, const char *name); } r;
  DDS_TypeSupport ts;
  DDS_ReturnCode_t result;
  (void) snprintf (a_name, sizeof (a_name), "%s%s", name, a_suf);
  (void) snprintf (r_name, sizeof (r_name), "%s%s", name, r_suf);
  if ((a.d = dlsym (RTLD_DEFAULT, a_name)) == NULL || (r.d = dlsym (RTLD_DEFAULT, r_name)) == NULL)
    error ("register_type: %s() or %s() not found in image\n", a_name, r_name);
  if ((ts = a.f ()) == NULL)
    error ("%s", a_name);
  if ((result = r.f (ts, dp, name)) != DDS_RETCODE_OK)
    error ("KeyedSeqTypeSupport_register_type: %d (%s)\n", (int) result, dds_strerror (result));
  return ts;
}

void save_argv0 (const char *argv0)
{
  saved_argv0 = argv0;
}

static void register_default_types (void)
{
  ts_KeyedSeq = register_type ("KeyedSeq");
  ts_Keyed32 = register_type ("Keyed32");
  ts_Keyed64 = register_type ("Keyed64");
  ts_Keyed128 = register_type ("Keyed128");
  ts_Keyed256 = register_type ("Keyed256");
  ts_OneULong = register_type ("OneULong");
}

int common_init (const char *argv0)
{
  save_argv0 (argv0);
  if ((dpf = DDS_DomainParticipantFactory_get_instance ()) == NULL)
    error ("DDS_DomainParticipantFactory_get_instance\n");
  dp = DDS_DomainParticipantFactory_create_participant (dpf, DDS_DOMAIN_ID_DEFAULT, DDS_PARTICIPANT_QOS_DEFAULT, NULL, DDS_STATUS_MASK_NONE);
  if (dp == DDS_HANDLE_NIL)
    error ("DDS_DomainParticipantFactory_create_participant\n");
  register_default_types ();
  return 0;
}

#if ! PRE_V6_5
int common_init_domainid (const char *argv0, DDS_DomainId_t domainid)
{
  save_argv0 (argv0);
  if ((dpf = DDS_DomainParticipantFactory_get_instance ()) == NULL)
    error ("DDS_DomainParticipantFactory_get_instance\n");
  dp = DDS_DomainParticipantFactory_create_participant (dpf, domainid, DDS_PARTICIPANT_QOS_DEFAULT, NULL, DDS_STATUS_MASK_NONE);
  if (dp == DDS_HANDLE_NIL)
    error ("DDS_DomainParticipantFactory_create_participant\n");
  register_default_types ();
  return 0;
}
#endif

void common_fini (void)
{
  DDS_ReturnCode_t rc;
  DDS_free (ts_KeyedSeq);
  DDS_free (ts_Keyed32);
  DDS_free (ts_Keyed64);
  DDS_free (ts_Keyed128);
  DDS_free (ts_Keyed256);
  DDS_free (ts_OneULong);
  if ((rc = DDS_DomainParticipant_delete_contained_entities (dp)) != DDS_RETCODE_OK)
    error ("DDS_DomainParticipant_delete_contained_entities: %d (%s)\n", (int) rc, dds_strerror (rc));
  DDS_DomainParticipantFactory_delete_participant (dpf, dp);
}

DDS_ReturnCode_t set_PartitionQosPolicy (DDS_PartitionQosPolicy *qp, unsigned npartitions, const char *partitions[])
{
  unsigned i;
  qp->name._length = npartitions;
  qp->name._maximum = npartitions;
  DDS_free (qp->name._buffer);
  if (npartitions == 0)
    qp->name._buffer = NULL;
  else
  {
    qp->name._release = 1;
    if ((qp->name._buffer = DDS_StringSeq_allocbuf (qp->name._maximum)) == NULL)
      return DDS_RETCODE_OUT_OF_RESOURCES;
    for (i = 0; i < qp->name._length; i++)
    {
      if ((qp->name._buffer[i] = DDS_string_alloc ((unsigned) strlen (partitions[i]) + 1)) == NULL)
        return DDS_RETCODE_OUT_OF_RESOURCES;
      strcpy (qp->name._buffer[i], partitions[i]);
    }
  }
  return DDS_RETCODE_OK;
}

DDS_ReturnCode_t change_publisher_partitions (DDS_Publisher pub, unsigned npartitions, const char *partitions[])
{
  DDS_PublisherQos *qos;
  DDS_ReturnCode_t rc;
  if ((qos = DDS_PublisherQos__alloc ()) == NULL)
    return DDS_RETCODE_OUT_OF_RESOURCES;
  if ((rc = DDS_Publisher_get_qos (pub, qos)) != DDS_RETCODE_OK)
    goto out;
  if ((rc = set_PartitionQosPolicy (&qos->partition, npartitions, partitions)) != DDS_RETCODE_OK)
    goto out;
  rc = DDS_Publisher_set_qos (pub, qos);
 out:
  DDS_free (qos);
  return rc;
}

DDS_ReturnCode_t change_subscriber_partitions (DDS_Subscriber pub, unsigned npartitions, const char *partitions[])
{
  DDS_SubscriberQos *qos;
  DDS_ReturnCode_t rc;
  if ((qos = DDS_SubscriberQos__alloc ()) == NULL)
    return DDS_RETCODE_OUT_OF_RESOURCES;
  if ((rc = DDS_Subscriber_get_qos (pub, qos)) != DDS_RETCODE_OK)
    goto out;
  if ((rc = set_PartitionQosPolicy (&qos->partition, npartitions, partitions)) != DDS_RETCODE_OK)
    goto out;
  rc = DDS_Subscriber_set_qos (pub, qos);
 out:
  DDS_free (qos);
  return rc;
}

static DDS_TopicQos *get_topic_qos (DDS_Topic t)
{
  DDS_TopicQos *tQos;
  DDS_ReturnCode_t result;
  DDS_Topic t2;
  if ((tQos = DDS_TopicQos__alloc ()) == NULL)
    error ("DDS_TopicQos__alloc\n");
  if ((result = DDS_Topic_get_qos (t, tQos)) == DDS_RETCODE_OK)
    ;
  else if (result != DDS_RETCODE_ILLEGAL_OPERATION && result != DDS_RETCODE_BAD_PARAMETER)
    error ("DDS_Topic_get_qos: %d (%s)\n", (int) result, dds_strerror (result));
  else if ((t2 = DDS_ContentFilteredTopic_get_related_topic (t)) == NULL)
    error ("DDS_Topic_get_qos: %d (%s) (and topic passed in is not a content-filtered topic)\n", (int) result, dds_strerror (result));
  else if ((result = DDS_Topic_get_qos (t2, tQos)) == DDS_RETCODE_OK)
    ;
  else
    error ("DDS_Topic_get_qos: %d (%s) (on content-filtered topic's related topic)\n", (int) result, dds_strerror (result));
  return tQos;
}

struct qos *new_tqos (void)
{
  struct qos *a;
  if ((a = malloc (sizeof (*a))) == NULL)
    error ("new_tqos: malloc\n");
  a->qt = QT_TOPIC;
  if ((a->u.topic.q = DDS_TopicQos__alloc ()) == NULL)
    error ("DDS_TopicQos__alloc\n");
  DDS_DomainParticipant_get_default_topic_qos (dp, a->u.topic.q);

  /* Not all defaults are those of DCPS: */
  a->u.topic.q->reliability.kind = DDS_RELIABLE_RELIABILITY_QOS;
  a->u.topic.q->reliability.max_blocking_time.sec = 1;
  a->u.topic.q->reliability.max_blocking_time.nanosec = 0;
  a->u.topic.q->destination_order.kind = DDS_BY_SOURCE_TIMESTAMP_DESTINATIONORDER_QOS;
  return a;
}

struct qos *new_pubqos (void)
{
  struct qos *a;
  if ((a = malloc (sizeof (*a))) == NULL)
    error ("new_pubqos: malloc\n");
  a->qt = QT_PUBLISHER;
  if ((a->u.pub.q = DDS_PublisherQos__alloc ()) == NULL)
    error ("DDS_PublisherQos__alloc\n");
  DDS_DomainParticipant_get_default_publisher_qos (dp, a->u.pub.q);
  return a;
}

struct qos *new_subqos (void)
{
  struct qos *a;
  if ((a = malloc (sizeof (*a))) == NULL)
    error ("new_subqos: malloc\n");
  a->qt = QT_SUBSCRIBER;
  if ((a->u.sub.q = DDS_SubscriberQos__alloc ()) == NULL)
    error ("DDS_SubscriberQos__alloc\n");
  DDS_DomainParticipant_get_default_subscriber_qos (dp, a->u.sub.q);
  return a;
}

struct qos *new_rdqos (DDS_Subscriber s, DDS_Topic t)
{
  DDS_TopicQos *tQos = get_topic_qos (t);
  struct qos *a;
  if ((a = malloc (sizeof (*a))) == NULL)
    error ("new_rdqos: malloc\n");
  a->qt = QT_READER;
  a->u.rd.t = t;
  a->u.rd.s = s;
  if ((a->u.rd.q = DDS_DataReaderQos__alloc ()) == NULL)
    error ("DDS_DataReaderQos__alloc\n");
  DDS_Subscriber_get_default_datareader_qos (s, a->u.rd.q);
  DDS_Subscriber_copy_from_topic_qos (s, a->u.rd.q, tQos);
  DDS_free (tQos);
  return a;
}

struct qos *new_wrqos (DDS_Publisher p, DDS_Topic t)
{
  DDS_TopicQos *tQos = get_topic_qos (t);
  struct qos *a;
  if ((a = malloc (sizeof (*a))) == NULL)
    error ("new_wrqos: malloc\n");
  a->qt = QT_WRITER;
  a->u.wr.t = t;
  a->u.wr.p = p;
  if ((a->u.wr.q = DDS_DataWriterQos__alloc ()) == NULL)
    error ("DDS_DataWriterQos__alloc\n");
  DDS_Publisher_get_default_datawriter_qos (p, a->u.wr.q);
  DDS_Publisher_copy_from_topic_qos (p, a->u.wr.q, tQos);
  DDS_free (tQos);

  /* Not all defaults are those of DCPS: */
  a->u.wr.q->writer_data_lifecycle.autodispose_unregistered_instances = 0;
  return a;
}

void free_qos (struct qos *a)
{
  switch (a->qt)
  {
    case QT_TOPIC: DDS_free (a->u.topic.q); break;
    case QT_PUBLISHER: DDS_free (a->u.pub.q); break;
    case QT_SUBSCRIBER: DDS_free (a->u.sub.q); break;
    case QT_READER: DDS_free (a->u.rd.q); break;
    case QT_WRITER: DDS_free (a->u.rd.q); break;
  }
  free (a);
}

DDS_Topic new_topic (const char *name, DDS_TypeSupport ts, const struct qos *a)
{
  DDS_Topic tp;
  char *tname;
  if (a->qt != QT_TOPIC)
    error ("new_topic called with non-topic qos\n");
  if ((tname = DDS_TypeSupport_get_type_name (ts)) == NULL)
    error ("DDS_TypeSupport_get_type_name\n");
  if ((tp = DDS_DomainParticipant_create_topic (dp, name, tname, a->u.topic.q, NULL, DDS_STATUS_MASK_NONE)) == NULL)
    error ("DDS_DomainParticipant_create_topic %s\n", name);
  DDS_free (tname);
  return tp;
}

DDS_Topic new_topic_KeyedSeq (const char *name, const struct qos *a)
{
  return new_topic (name, ts_KeyedSeq, a);
}

DDS_Topic new_topic_Keyed32 (const char *name, const struct qos *a)
{
  return new_topic (name, ts_Keyed32, a);
}

DDS_Topic new_topic_Keyed64 (const char *name, const struct qos *a)
{
  return new_topic (name, ts_Keyed64, a);
}

DDS_Topic new_topic_Keyed128 (const char *name, const struct qos *a)
{
  return new_topic (name, ts_Keyed128, a);
}

DDS_Topic new_topic_Keyed256 (const char *name, const struct qos *a)
{
  return new_topic (name, ts_Keyed256, a);
}

DDS_Topic new_topic_OneULong (const char *name, const struct qos *a)
{
  return new_topic (name, ts_OneULong, a);
}

DDS_Publisher new_publisher (const struct qos *a, unsigned npartitions, const char **partitions)
{
  DDS_Publisher p;
  DDS_PublisherQos *pQos;
  if ((pQos = DDS_PublisherQos__alloc ()) == NULL)
    error ("DDS_PublisherQos__alloc\n");
  if (a == NULL)
  {
    DDS_DomainParticipant_get_default_publisher_qos (dp, pQos);
  }
  else
  {
    if (a->qt != QT_PUBLISHER)
      error ("new_topic called with non-publisher qos\n");
    pQos->presentation = a->u.pub.q->presentation;
    pQos->entity_factory = a->u.pub.q->entity_factory;
  }
  set_PartitionQosPolicy (&pQos->partition, npartitions, partitions);
  if ((p = DDS_DomainParticipant_create_publisher (dp, pQos, NULL, DDS_STATUS_MASK_NONE)) == NULL)
    error ("DDS_DomainParticipant_create_publisher\n");
  DDS_free (pQos);
  return p;
}

DDS_Publisher new_publisher1 (const struct qos *a, const char *partition)
{
  return new_publisher(a, 1, &partition);
}

DDS_Subscriber new_subscriber (const struct qos *a, unsigned npartitions, const char **partitions)
{
  DDS_SubscriberQos *sQos;
  DDS_Subscriber s;
  if ((sQos = DDS_SubscriberQos__alloc ()) == NULL)
    error ("DDS_SubscriberQos__alloc\n");
  if (a == NULL)
  {
    DDS_DomainParticipant_get_default_subscriber_qos (dp, sQos);
  }
  else
  {
    if (a->qt != QT_SUBSCRIBER)
      error ("new_topic called with non-subscriber qos\n");
    sQos->presentation = a->u.sub.q->presentation;
    sQos->entity_factory = a->u.sub.q->entity_factory;
  }
  set_PartitionQosPolicy (&sQos->partition, npartitions, partitions);
  if ((s = DDS_DomainParticipant_create_subscriber (dp, sQos, NULL, DDS_STATUS_MASK_NONE)) == NULL)
    error ("DDS_DomainParticipant_create_subscriber\n");
  DDS_free (sQos);
  return s;
}

DDS_Subscriber new_subscriber1 (const struct qos *a, const char *partition)
{
  return new_subscriber(a, 1, &partition);
}

DDS_DataWriter new_datawriter_listener (const struct qos *a, const struct DDS_DataWriterListener *l, DDS_StatusMask mask)
{
  DDS_PublisherQos *pqos;
  DDS_DataWriter wr;
  if (a->qt != QT_WRITER)
    error ("new_datawriter called with non-writer qos\n");
  pqos = DDS_PublisherQos__alloc ();
  DDS_Publisher_get_qos (a->u.wr.p, pqos);
  if (pqos->presentation.access_scope == DDS_GROUP_PRESENTATION_QOS && pqos->presentation.coherent_access)
    a->u.wr.q->resource_limits.max_samples =
      a->u.wr.q->resource_limits.max_instances =
      a->u.wr.q->resource_limits.max_samples_per_instance = DDS_LENGTH_UNLIMITED;
  DDS_free (pqos);
  if ((wr = DDS_Publisher_create_datawriter (a->u.wr.p, a->u.wr.t, a->u.wr.q, l, mask)) == NULL)
    error ("DDS_Publisher_create_datawriter\n");
  return wr;
}

DDS_DataWriter new_datawriter (const struct qos *a)
{
  return new_datawriter_listener (a, NULL, DDS_STATUS_MASK_NONE);
}

DDS_DataReader new_datareader_listener (const struct qos *a, const struct DDS_DataReaderListener *l, DDS_StatusMask mask)
{
  DDS_SubscriberQos *sqos;
  DDS_DataReader rd;
  if (a->qt != QT_READER)
    error ("new_datareader called with non-reader qos\n");
  sqos = DDS_SubscriberQos__alloc ();
  DDS_Subscriber_get_qos (a->u.rd.s, sqos);
  if (sqos->presentation.access_scope == DDS_GROUP_PRESENTATION_QOS && sqos->presentation.coherent_access)
    a->u.rd.q->resource_limits.max_samples =
      a->u.rd.q->resource_limits.max_instances =
      a->u.rd.q->resource_limits.max_samples_per_instance = DDS_LENGTH_UNLIMITED;
  DDS_free (sqos);
  if ((rd = DDS_Subscriber_create_datareader (a->u.rd.s, a->u.rd.t, a->u.rd.q, l, mask)) == NULL)
    error ("DDS_Subscriber_create_datareader\n");
  return rd;
}

DDS_DataReader new_datareader (const struct qos *a)
{
  return new_datareader_listener (a, NULL, DDS_STATUS_MASK_NONE);
}

static void inapplicable_qos(const struct qos *a, const char *n)
{
  const char *en = "?";
  switch (a->qt)
  {
    case QT_TOPIC: en = "topic"; break;
    case QT_PUBLISHER: en = "publisher"; break;
    case QT_SUBSCRIBER: en = "subscriber"; break;
    case QT_WRITER: en = "writer"; break;
    case QT_READER: en = "reader"; break;
  }
  fprintf(stderr, "warning: %s entity ignoring inapplicable QoS \"%s\"\n", en, n);
}

#define GET_QOS_TRW(a, n) (((a)->qt == QT_TOPIC) ? (void *) &(a)->u.topic.q->n : ((a)->qt == QT_READER) ? (void *) &(a)->u.rd.q->n : ((a)->qt == QT_WRITER) ? (void *) &(a)->u.wr.q->n : (inapplicable_qos((a), #n), (void *) 0))
#define GET_QOS_TW(a, n) (((a)->qt == QT_TOPIC) ? &(a)->u.topic.q->n : ((a)->qt == QT_WRITER) ? (void *) &(a)->u.wr.q->n : (inapplicable_qos((a), #n), (void *) 0))
#define GET_QOS_RW(a, n) (((a)->qt == QT_READER) ? (void *) &(a)->u.rd.q->n : ((a)->qt == QT_WRITER) ? (void *) &(a)->u.wr.q->n : (inapplicable_qos((a), #n), (void *) 0))
#define GET_QOS_T(a, n) (((a)->qt != QT_TOPIC) ? (inapplicable_qos((a), #n), (void *) 0) : (void *) &(a)->u.topic.q->n)
#define GET_QOS_R(a, n) (((a)->qt != QT_READER) ? (inapplicable_qos((a), #n), (void *) 0) : (void *) &(a)->u.rd.q->n)
#define GET_QOS_W(a, n) (((a)->qt != QT_WRITER) ? (inapplicable_qos((a), #n), (void *) 0) : (void *) &(a)->u.wr.q->n)
#define GET_QOS_PS(a, n) (((a)->qt == QT_PUBLISHER) ? (void *) (&(a)->u.pub.q->n) : ((a)->qt == QT_SUBSCRIBER) ? (void *) (&(a)->u.sub.q->n) : (inapplicable_qos((a), #n), (void *) 0))

const DDS_DataWriterQos *qos_datawriter(const struct qos *a)
{
  return a->qt == QT_WRITER ? a->u.wr.q : NULL;
}

void qos_durability (struct qos *a, const char *arg)
{
  DDS_DurabilityQosPolicy *qp = GET_QOS_TRW (a, durability);
  if (qp == NULL)
    return;
  if (strcmp (arg, "v") == 0)
    qp->kind = DDS_VOLATILE_DURABILITY_QOS;
  else if (strcmp (arg, "tl") == 0)
    qp->kind = DDS_TRANSIENT_LOCAL_DURABILITY_QOS;
  else if (strcmp (arg, "t") == 0)
    qp->kind = DDS_TRANSIENT_DURABILITY_QOS;
  else if (strcmp (arg, "p") == 0)
    qp->kind = DDS_PERSISTENT_DURABILITY_QOS;
  else
    error ("durability qos: %s: invalid\n", arg);
}

void qos_history (struct qos *a, const char *arg)
{
  DDS_HistoryQosPolicy *qp = GET_QOS_TRW (a, history);
  int hist_depth, pos;
  if (qp == NULL)
    return;
  if (strcmp (arg, "all") == 0)
  {
    qp->kind = DDS_KEEP_ALL_HISTORY_QOS;
    qp->depth = DDS_LENGTH_UNLIMITED;
  }
  else if (sscanf (arg, "%d%n", &hist_depth, &pos) == 1 && arg[pos] == 0 && hist_depth > 0)
  {
    qp->kind = DDS_KEEP_LAST_HISTORY_QOS;
    qp->depth = hist_depth;
  }
  else
  {
    error ("history qos: %s: invalid\n", arg);
  }
}

void qos_destination_order (struct qos *a, const char *arg)
{
  DDS_DestinationOrderQosPolicy *qp = GET_QOS_TRW (a, destination_order);
  if (qp == NULL)
    return;
  if (strcmp (arg, "r") == 0)
    qp->kind = DDS_BY_RECEPTION_TIMESTAMP_DESTINATIONORDER_QOS;
  else if (strcmp (arg, "s") == 0)
    qp->kind = DDS_BY_SOURCE_TIMESTAMP_DESTINATIONORDER_QOS;
  else
    error ("destination order qos: %s: invalid\n", arg);
}

void qos_ownership (struct qos *a, const char *arg)
{
  DDS_OwnershipQosPolicy *qp = GET_QOS_TRW (a, ownership);
  int strength, pos;
  if (qp == NULL)
    return;
  if (strcmp (arg, "s") == 0)
    qp->kind = DDS_SHARED_OWNERSHIP_QOS;
  else if (strcmp (arg, "x") == 0)
    qp->kind = DDS_EXCLUSIVE_OWNERSHIP_QOS;
  else if (sscanf (arg, "x:%d%n", &strength, &pos) == 1 && arg[pos] == 0)
  {
    DDS_OwnershipStrengthQosPolicy *qps = GET_QOS_W (a, ownership_strength);
    qp->kind = DDS_EXCLUSIVE_OWNERSHIP_QOS;
    if (qps) qps->value = strength;
  }
  else
  {
    error ("ownership qos: %s invalid\n", arg);
  }
}

void qos_transport_priority (struct qos *a, const char *arg)
{
  DDS_TransportPriorityQosPolicy *qp = GET_QOS_W (a, transport_priority);
  int pos;
  if (qp == NULL)
    return;
  if (sscanf (arg, "%d%n", &qp->value, &pos) != 1 || arg[pos] != 0)
    error ("transport_priority qos: %s invalid\n", arg);
}

static unsigned char gethexchar (const char **str)
{
  unsigned char v = 0;
  int empty = 1;
  while (**str)
  {
    switch (**str)
    {
      case '0': case '1': case '2': case '3': case '4':
      case '5': case '6': case '7': case '8': case '9':
        v = 16 * v + (unsigned char) **str - '0';
        (*str)++;
        break;
      case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
        v = 16 * v + (unsigned char) **str - 'a' + 10;
        (*str)++;
        break;
      case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
        v = 16 * v + (unsigned char) **str - 'A' + 10;
        (*str)++;
        break;
      default:
        if (empty)
          error ("empty \\x escape");
        goto done;
    }
    empty = 0;
  }
 done:
  return v;
}

static unsigned char getoctchar (const char **str)
{
  unsigned char v = 0;
  int nseen = 0;
  while (**str && nseen < 3)
  {
    if (**str >= '0' && **str <= '7')
    {
      v = 8 * v + (unsigned char) **str - '0';
      (*str)++;
      nseen++;
    }
    else
    {
      if (nseen == 0)
        error ("empty \\ooo escape");
      break;
    }
  }
  return v;
}

static void *unescape (const char *str, size_t *len)
{
  /* results in a blob without explicit terminator, i.e., can't get
     any longer than strlen(str) */
  unsigned char *x = malloc (strlen (str)), *p = x;
  while (*str)
  {
    if (*str != '\\')
      *p++ = (unsigned char) *str++;
    else
    {
      str++;
      switch (*str)
      {
        case '\\': case ',': case '\'': case '"': case '?':
          *p++ = (unsigned char) *str;
          str++;
          break;
        case 'x':
          str++;
          *p++ = gethexchar (&str);
          break;
        case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7':
          *p++ = getoctchar (&str);
          break;
        case 'a': *p++ = '\a'; str++; break;
        case 'b': *p++ = '\b'; str++; break;
        case 'f': *p++ = '\f'; str++; break;
        case 'n': *p++ = '\n'; str++; break;
        case 'r': *p++ = '\r'; str++; break;
        case 't': *p++ = '\t'; str++; break;
        case 'v': *p++ = '\v'; str++; break;
        case 'e': *p++ = 0x1b; str++; break;
        default:
          error ("invalid escape string: %s\n", str);
          break;
      }
    }
  }
  *len = (size_t) (p - x);
  return x;
}

void qos_user_data (struct qos *a, const char *arg)
{
  DDS_UserDataQosPolicy *qp = GET_QOS_RW (a, user_data);
  size_t len;
  if (qp == NULL)
    return;
  else
  {
    void *unesc = unescape (arg, &len);
    qp->value._length = qp->value._maximum = (unsigned) len;
    if (qp->value._length == 0)
      qp->value._buffer = NULL;
    else
    {
      qp->value._buffer = malloc (qp->value._length);
      memcpy (qp->value._buffer, unesc, qp->value._length);
    }
    qp->value._release = 0;
    free (unesc);
  }
}

int double_to_dds_duration (DDS_Duration_t *dd, double d)
{
  int sec;
  unsigned nanosec;
  if (d < 0)
    return -1;
  sec = (int) floor (d);
  nanosec = (unsigned) floor (1e9 * (d - sec) + 0.5);
  assert (nanosec <= 1000000000);
  if (sec < 0 || sec > (int) 0x7fffffff || (sec == (int) 0x7fffffff && nanosec == 1000000000))
  {
    sec = (int) 0x7fffffff;
    nanosec = 999999999;
  }
  else if (nanosec == 1000000000)
  {
    nanosec = 0;
    sec++;
  }
  dd->sec = sec;
  dd->nanosec = nanosec;
  return 0;
}

void set_infinite_dds_duration (DDS_Duration_t *dd)
{
  dd->sec = DDS_DURATION_INFINITE_SEC;
  dd->nanosec = DDS_DURATION_INFINITE_NSEC;
}

void qos_reliability (struct qos *a, const char *arg)
{
  DDS_ReliabilityQosPolicy *qp = GET_QOS_TRW (a, reliability);
  const char *argp = arg;
  if (qp == NULL)
    return;
  switch (*argp++)
  {
    case 'n':
      qp->kind = DDS_BEST_EFFORT_RELIABILITY_QOS;
      break;
    case 'y':
      qp->kind = DDS_RELIABLE_RELIABILITY_QOS;
      break;
    case 's':
      qp->kind = DDS_RELIABLE_RELIABILITY_QOS;
      qp->synchronous = 1;
      break;
    default:
      error ("reliability qos: %s: invalid\n", arg);
  }
  if (qp->kind == DDS_RELIABLE_RELIABILITY_QOS && *argp == ':')
  {
    double max_blocking_time;
    int pos;
    if (strcmp (argp, ":inf") == 0)
    {
      set_infinite_dds_duration (&qp->max_blocking_time);
      argp += 4;
    }
    else if (sscanf (argp, ":%lf%n", &max_blocking_time, &pos) == 1 && argp[pos] == 0)
    {
      if (max_blocking_time <= 0 || double_to_dds_duration (&qp->max_blocking_time, max_blocking_time) < 0)
        error ("reliability qos: %s: max blocking time out of range\n", arg);
      argp += pos;
    }
    else
    {
      error ("reliability qos: %s: invalid max_blocking_time\n", arg);
    }
  }
  if (*argp != 0)
  {
    error ("reliability qos: %s: invalid\n", arg);
  }
}

void qos_liveliness (struct qos *a, const char *arg)
{
  DDS_LivelinessQosPolicy *qp = GET_QOS_TRW (a, liveliness);
  double lease_duration;
  char mode;
  int pos;
  if (qp == NULL)
    return;
  if (strcmp (arg, "a") == 0)
  {
    qp->kind = DDS_AUTOMATIC_LIVELINESS_QOS;
    set_infinite_dds_duration (&qp->lease_duration);
  }
  else if (sscanf (arg, "%c:%lf%n", &mode, &lease_duration, &pos) == 2 && arg[pos] == 0)
  {
    if (lease_duration <= 0 || double_to_dds_duration (&qp->lease_duration, lease_duration) < 0)
      error ("liveliness qos: %s: lease duration out of range\n", arg);
    switch (mode)
    {
      case 'a': qp->kind = DDS_AUTOMATIC_LIVELINESS_QOS; break;
      case 'p': qp->kind = DDS_MANUAL_BY_PARTICIPANT_LIVELINESS_QOS; break;
      case 'w': qp->kind = DDS_MANUAL_BY_TOPIC_LIVELINESS_QOS; break;
      default:  error ("liveliness qos: %c: invalid mode\n", mode);
    }
  }
  else
  {
    error ("liveliness qos: %s: invalid\n", arg);
  }
}

static void qos_simple_duration (DDS_Duration_t *dd, const char *name, const char *arg)
{
  double duration;
  int pos;
  if (strcmp (arg, "inf") == 0)
    set_infinite_dds_duration (dd);
  else if (sscanf (arg, "%lf%n", &duration, &pos) == 1 && arg[pos] == 0)
  {
    if (double_to_dds_duration (dd, duration) < 0)
      error ("%s qos: %s: duration invalid\n", name, arg);
  }
  else
  {
    error ("%s qos: %s: invalid\n", name, arg);
  }
}

void qos_latency_budget (struct qos *a, const char *arg)
{
  DDS_LatencyBudgetQosPolicy *qp = GET_QOS_TRW (a, latency_budget);
  if (qp == NULL)
    return;
  qos_simple_duration (&qp->duration, "latency_budget", arg);
}

void qos_deadline (struct qos *a, const char *arg)
{
  DDS_DeadlineQosPolicy *qp = GET_QOS_TRW (a, deadline);
  if (qp == NULL)
    return;
  qos_simple_duration (&qp->period, "deadline", arg);
}

void qos_lifespan (struct qos *a, const char *arg)
{
  DDS_LifespanQosPolicy *qp = GET_QOS_TW (a, lifespan);
  if (qp == NULL)
    return;
  qos_simple_duration (&qp->duration, "lifespan", arg);
}

static int one_resource_limit (DDS_long *val, const char **arg)
{
  int pos;
  if (strncmp (*arg, "inf", 3) == 0)
  {
    *val = DDS_LENGTH_UNLIMITED;
    (*arg) += 3;
    return 1;
  }
  else if (sscanf (*arg, "%d%n", val, &pos) == 1)
  {
    (*arg) += pos;
    return 1;
  }
  else
  {
    return 0;
  }
}

void qos_resource_limits (struct qos *a, const char *arg)
{
  DDS_ResourceLimitsQosPolicy *qp = GET_QOS_TRW (a, resource_limits);
  const char *argp = arg;
  if (qp == NULL)
    return;

  if (!one_resource_limit (&qp->max_samples, &argp))
    goto err;
  if (*argp++ != '/')
    goto err;
  if (!one_resource_limit (&qp->max_instances, &argp))
    goto err;
  if (*argp++ != '/')
    goto err;
  if (!one_resource_limit (&qp->max_samples_per_instance, &argp))
    goto err;
  if (*argp != 0)
    goto err;
  return;
 err:
  error ("resource limits qos: %s: invalid\n", arg);
}

void qos_durability_service (struct qos *a, const char *arg)
{
  DDS_DurabilityServiceQosPolicy *qp = GET_QOS_T (a, durability_service);
  const char *argp = arg;
  double service_cleanup_delay;
  int pos, hist_depth;

  if (qp == NULL)
    return;

  qp->service_cleanup_delay.sec = 0;
  qp->service_cleanup_delay.nanosec = 0;
  qp->history_depth = 1;
  qp->history_kind = DDS_KEEP_LAST_HISTORY_QOS;
  qp->max_samples = DDS_LENGTH_UNLIMITED;
  qp->max_instances = DDS_LENGTH_UNLIMITED;
  qp->max_samples_per_instance = DDS_LENGTH_UNLIMITED;

  argp = arg;
  if (strncmp (argp, "inf", 3) == 0) {
    set_infinite_dds_duration (&qp->service_cleanup_delay);
    pos = 3;
  } else if (sscanf (argp, "%lf%n", &service_cleanup_delay, &pos) == 1) {
    if (service_cleanup_delay < 0 || double_to_dds_duration (&qp->service_cleanup_delay, service_cleanup_delay) < 0)
      error ("durability service qos: %s: service cleanup delay out of range\n", arg);
  } else {
    goto err;
  }
  if (argp[pos] == 0) return; else if (argp[pos] != '/') goto err;
  argp += pos + 1;

  if (strncmp (argp, "all", 3) == 0) {
    qp->history_kind = DDS_KEEP_ALL_HISTORY_QOS;
    pos = 3;
  } else if (sscanf (argp, "%d%n", &hist_depth, &pos) == 1 && hist_depth > 0) {
    qp->history_depth = hist_depth;
  } else {
    goto err;
  }
  if (argp[pos] == 0) return; else if (argp[pos] != '/') goto err;
  argp += pos + 1;

  if (!one_resource_limit (&qp->max_samples, &argp))
    goto err;
  if (*argp++ != '/')
    goto err;
  if (!one_resource_limit (&qp->max_instances, &argp))
    goto err;
  if (*argp++ != '/')
    goto err;
  if (!one_resource_limit (&qp->max_samples_per_instance, &argp))
    goto err;
  if (*argp != 0)
    goto err;
  return;
err:
  error ("resource limits qos: %s: invalid\n", arg);
}

void qos_presentation (struct qos *a, const char *arg)
{
  DDS_PresentationQosPolicy *qp = GET_QOS_PS (a, presentation);
  const char *flags;
  if (qp == NULL)
    return;
  if (*arg == 'i') {
    qp->access_scope = DDS_INSTANCE_PRESENTATION_QOS;
    qp->coherent_access = 0;
    qp->ordered_access = 0;
  } else if (*arg == 't') {
    qp->access_scope = DDS_TOPIC_PRESENTATION_QOS;
    qp->coherent_access = 1;
    qp->ordered_access = 0;
  } else if (*arg == 'g') {
    qp->access_scope = DDS_GROUP_PRESENTATION_QOS;
    qp->coherent_access = 1;
    qp->ordered_access = 0;
  } else {
    error ("presentation qos: %s: invalid\n", arg);
  }
  flags = arg + 1;
  if (*flags == 'c') {
    qp->coherent_access = 1;
    flags++;
  } else if (*flags == 'n' && *(flags+1) == 'c') {
    qp->coherent_access = 0;
    flags += 2;
  }
  if (*flags == 'a') {
    qp->ordered_access = 1;
    flags++;
  }
  if (*flags) {
    error ("presentation qos: %s: invalid\n", arg);
  }
}

void qos_autodispose_unregistered_instances (struct qos *a, const char *arg)
{
  DDS_WriterDataLifecycleQosPolicy *qp = GET_QOS_W (a, writer_data_lifecycle);
  if (qp == NULL)
    return;
  if (strcmp (arg, "n") == 0)
    qp->autodispose_unregistered_instances = 0;
  else if (strcmp (arg, "y") == 0)
    qp->autodispose_unregistered_instances = 1;
  else
    error ("autodispose_unregistered_instances qos: %s: invalid\n", arg);
}

void qos_autopurge_disposed_samples_delay (struct qos *a, const char *arg)
{
  DDS_ReaderDataLifecycleQosPolicy *qp = GET_QOS_R (a, reader_data_lifecycle);
  if (qp == NULL)
    return;
  qos_simple_duration(&qp->autopurge_disposed_samples_delay, "autopurge_disposed_samples_delay", arg);
}

static unsigned split_string (char ***xs, const char *in, const char *sep)
{
  char *incopy = strdup (in), *cursor = incopy, *tok;
  unsigned n = 0;
  *xs = NULL;
  while ((tok = strsep (&cursor, sep)) != NULL)
  {
    *xs = realloc (*xs, (n+1) * sizeof (**xs));
    (*xs)[n++] = tok;
  }
  return n;
}

void qos_subscription_keys (struct qos *a, const char *arg)
{
  DDS_SubscriptionKeyQosPolicy *qp = GET_QOS_R (a, subscription_keys);
  char **xs;
  unsigned i, n;
  if (qp == NULL)
    return;
  n = split_string (&xs, arg, "/");
  qp->use_key_list = 1;
  qp->key_list._maximum = qp->key_list._length = n;
  qp->key_list._buffer = DDS_StringSeq_allocbuf (n);
  qp->key_list._release = 1;
  for (i = 0; i < n; i++)
  {
    qp->key_list._buffer[i] = DDS_string_dup (xs[i]);
    free (xs[i]);
  }
  free (xs);
}

void qos_autoenable (struct qos *a, const char *arg)
{
  DDS_EntityFactoryQosPolicy *qp = GET_QOS_PS (a, entity_factory);
  if (qp == NULL)
    return;
  if (strcmp (arg, "n") == 0)
    qp->autoenable_created_entities = 0;
  else if (strcmp (arg, "y") == 0)
    qp->autoenable_created_entities = 1;
  else
    error ("autodispose_unregistered_instances qos: %s: invalid\n", arg);
}

const char *qos_arg_usagestr = "\
QOS (not all are universally applicable):\n\
  A={a|p:S|w:S}   liveliness (automatic, participant or writer, S in seconds)\n\
  d={v|tl|t|p}    durability (default: v)\n\
  D=P             deadline P in seconds (default: inf)\n\
  k={all|N}       KEEP_ALL or KEEP_LAST N\n\
  l=D             latency budget in seconds (default: 0)\n\
  L=D             lifespan in seconds (default: inf)\n\
  o=[r|s]         order by reception or source timestamp (default: s)\n\
  O=[s|x[:S]]     ownership: shared or exclusive, strength S (default: s)\n\
  p=PRIO          transport priority (default: 0)\n\
  P={i|t|g}[c|nc][a]   instance, topic or group access scope\n\
                  c|nc coherent_access = true|false (default is true\n\
                       for topic/group, false for instance)\n\
                  a    set ordered_access = true\n\
  r={y[:T]|s[:T]|n}  reliability, T is max blocking time in seconds,\n\
                  s is reliable+synchronous (default: y:1)\n\
  R=S/I/SpI       resource limits (samples, insts, S/I; default: inf/inf/inf)\n\
  S=C[/H[/S/I/SpI]] durability_service (cleanup delay, history, resource limits)\n\
  u={y|n}         autodispose unregistered instances (default: n)\n\
  U=TEXT          set user_data to TEXT\n\
  V=K0:K1:K2      set subscription keys\n\
  x=D             auto-purge disposed instances delay\n\
";

#if PRE_V6_5
void *qosprov = NULL;

void set_qosprovider (const char *arg)
{
}
#else
DDS_QosProvider qosprov = NULL;

void set_qosprovider (const char *arg)
{
  const char *p = strchr (arg, ',');
  const char *xs = strstr (arg, "://");
  char *profile = NULL;
  const char *uri;
  if (p == NULL || xs == NULL || p >= xs)
    uri = arg;
  else {
    uri = p+1;
    profile = strdup(arg);
    profile[p-uri] = 0;
  }
  if ((qosprov = DDS_QosProvider__alloc(uri, profile)) == NULL)
    error("DDS_QosProvider__alloc(%s,%s) failed\n", uri, profile ? profile : "(null)");
  free(profile);
}
#endif

void setqos_from_args (struct qos *q, int n, const char *args[])
{
  int i;
  for (i = 0; i < n; i++)
  {
    char *args_copy = strdup (args[i]), *cursor = args_copy;
    const char *arg;
    while ((arg = strsep (&cursor, ",")) != NULL)
    {
      if (arg[0] && arg[1] == '=') {
        const char *a = arg + 2;
        switch (arg[0]) {
          case 'A': qos_liveliness (q, a); break;
          case 'd': qos_durability (q, a); break;
          case 'D': qos_deadline (q, a); break;
          case 'k': qos_history (q, a); break;
          case 'l': qos_latency_budget (q, a); break;
          case 'L': qos_lifespan (q, a); break;
          case 'o': qos_destination_order (q, a); break;
          case 'O': qos_ownership (q, a); break;
          case 'p': qos_transport_priority (q, a); break;
          case 'P': qos_presentation (q, a); break;
          case 'r': qos_reliability (q, a); break;
          case 'R': qos_resource_limits (q, a); break;
          case 'S': qos_durability_service (q, a); break;
          case 'u': qos_autodispose_unregistered_instances (q, a); break;
          case 'U': qos_user_data (q, a); break;
          case 'V': qos_subscription_keys (q, a); break;
          case 'x': qos_autopurge_disposed_samples_delay (q, a); break;
          default:
            fprintf (stderr, "%s: unknown QoS\n", arg);
            exit (1);
        }
      } else if (qosprov == NULL) {
        fprintf (stderr, "QoS specification %s requires a QoS provider but none set\n", arg);
        exit (1);
      } else {
#if ! PRE_V6_5
        DDS_ReturnCode_t result;
        if (*arg == 0)
          arg = NULL;
        switch (q->qt) {
          case QT_TOPIC:
            if ((result = DDS_QosProvider_get_topic_qos(qosprov, q->u.topic.q, arg)) != DDS_RETCODE_OK)
              error ("DDS_QosProvider_get_topic_qos(%s): error %d (%s)\n", arg, (int) result, dds_strerror(result));
            break;
          case QT_PUBLISHER:
            if ((result = DDS_QosProvider_get_publisher_qos(qosprov, q->u.pub.q, arg)) != DDS_RETCODE_OK)
              error ("DDS_QosProvider_get_publisher_qos(%s): error %d (%s)\n", arg, (int) result, dds_strerror(result));
            break;
          case QT_SUBSCRIBER:
            if ((result = DDS_QosProvider_get_subscriber_qos(qosprov, q->u.sub.q, arg)) != DDS_RETCODE_OK)
              error ("DDS_QosProvider_get_subscriber_qos(%s): error %d (%s)\n", arg, (int) result, dds_strerror(result));
            break;
          case QT_WRITER:
            if ((result = DDS_QosProvider_get_datawriter_qos(qosprov, q->u.wr.q, arg)) != DDS_RETCODE_OK)
              error ("DDS_QosProvider_get_datawriter_qos(%s): error %d (%s)\n", arg, (int) result, dds_strerror(result));
            break;
          case QT_READER:
            if ((result = DDS_QosProvider_get_datareader_qos(qosprov, q->u.rd.q, arg)) != DDS_RETCODE_OK)
              error ("DDS_QosProvider_get_datareader_qos(%s): error %d (%s)\n", arg, (int) result, dds_strerror(result));
            break;
        }
#endif
      }
    }
    free (args_copy);
  }
}

