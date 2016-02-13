/*
 *                         OpenSplice DDS
 *
 *   This software and documentation are Copyright 2006 to 2011 PrismTech
 *   Limited and its licensees. All rights reserved. See file:
 *
 *                     $OSPL_HOME/LICENSE
 *
 *   for full copyright notice and license terms.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <ctype.h>
#include <regex.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <string.h>
#include <signal.h>
#include <errno.h>

#include "dds_dcps.h"

#ifndef DDS_DOMAIN_ID_DEFAULT
#define DDS_DOMAIN_ID_DEFAULT 0
#endif

#define MAX_DR 32

static int sigpipe[2];
static pthread_t sigh_tid;
static DDS_GuardCondition sigguard;
static const char *argv0;
static int monitor_flag = 0;
static regex_t topic_filter_regex;
static regex_t key_filter_regex;
static regex_t participant_key_filter_regex;
static regex_t group_key_filter_regex;

static void *sigh_thread (void *arg __attribute__ ((unused)))
{
  /* assuming 4-byte reads/writes are atomic (I think that's been true
     since the early days of Unix) */
  int sig;
  ssize_t n;
  while ((n = read (sigpipe[0], &sig, sizeof (sig))) > 0 || (n == -1 && errno == EINTR))
    if (n > 0)
    {
      if (sig != -1)
        DDS_GuardCondition_set_trigger_value (sigguard, 1);
      else
        break;
    }
  return 0;
}

static void sigh (int sig __attribute__ ((unused)))
{
  /* see also sigh_thread */
  write (sigpipe[1], &sig, sizeof (sig));
}

/* Poor man's generics -- I really think DDS got this one wrong ... */
#define TOPIC_HANDLER(fn_, name_, hh_) int fn_ (const char *hdr, DDS_DataReader dr) { \
    DDS_ReturnCode_t rc;                                                \
    DDS_sequence_DDS_##name_##BuiltinTopicData *dseq =                  \
      DDS_sequence_DDS_##name_##BuiltinTopicData__alloc ();             \
    DDS_SampleInfoSeq *iseq = DDS_SampleInfoSeq__alloc ();              \
    unsigned i;                                                         \
    rc = DDS_##name_##BuiltinTopicDataDataReader_take (                 \
            dr, dseq, iseq, DDS_LENGTH_UNLIMITED,                       \
            DDS_ANY_SAMPLE_STATE, DDS_ANY_VIEW_STATE, DDS_ANY_INSTANCE_STATE); \
    if (rc != DDS_RETCODE_OK && rc != DDS_RETCODE_NO_DATA)              \
    {                                                                   \
      fprintf (stderr, "%s: DDS_"#name_"BuiltinTopicDataDataReader_take failed (%d)\n", \
               argv0, rc);                                              \
      exit (1);                                                         \
    }                                                                   \
    for (i = 0; i < dseq->_length; i++)                                 \
    {                                                                   \
      DDS_##name_##BuiltinTopicData *data;                              \
      data = &dseq->_buffer[i];                                         \
      hh_ (&iseq->_buffer[i], data, iseq->_buffer[i].valid_data, &hdr); \
    }                                                                   \
    if ((rc = DDS_##name_##BuiltinTopicDataDataReader_return_loan (dr, dseq, iseq)) \
        != DDS_RETCODE_OK)                                              \
    {                                                                   \
      fprintf (stderr, "%s: DDS_"#name_"BuiltinTopicDataDataReader_return_loan failed (%d)\n", \
               argv0, rc);                                              \
      exit (1);                                                         \
    }                                                                   \
    DDS_free (iseq);                                                    \
    DDS_free (dseq);                                                    \
    return 0;                                                           \
  }

void print_header(const char **hdr)
{
  if (*hdr)
  {
    printf ("%s:\n", *hdr);
    *hdr = NULL;
  }
}

unsigned printable_seq_length (const unsigned char *as, unsigned n)
{
  unsigned i;
  for (i = 0; i < n; i++)
    if (!isprint (as[i]))
      break;
  return i;
}

void print_octetseq (const DDS_sequence_octet *v)
{
  unsigned i, n;
  const char *sep = "";
  printf ("%d<", v->_length);
  i = 0;
  while (i < v->_length)
  {
    if ((n = printable_seq_length (v->_buffer + i, v->_length - i)) < 4)
    {
      while (n--)
        printf ("%s%d", sep, v->_buffer[i++]);
    }
    else
    {
      printf ("\"%*.*s\"", n, n, v->_buffer + i);
      i += n;
    }
    sep = ",";
  }
  printf (">");
}

void qp_user_data (const DDS_UserDataQosPolicy *q)
{
  printf ("  user_data: value = ");
  print_octetseq (&q->value);
  printf ("\n");
}

void qp_group_data (const DDS_GroupDataQosPolicy *q)
{
  printf ("  group_data: value = ");
  print_octetseq (&q->value);
  printf ("\n");
}

void qp_topic_data (const DDS_TopicDataQosPolicy *q)
{
  printf ("  topic_data: value = ");
  print_octetseq (&q->value);
  printf ("\n");
}

void qp_transport_priority (const DDS_TransportPriorityQosPolicy *q)
{
  printf ("  transport_priority: priority = %d\n", q->value);
}

void qp_durability (const DDS_DurabilityQosPolicy *q)
{
  char buf[40];
  char *k;
  switch (q->kind)
  {
    case DDS_VOLATILE_DURABILITY_QOS: k = "volatile"; break;
    case DDS_TRANSIENT_LOCAL_DURABILITY_QOS: k = "transient-local"; break;
    case DDS_TRANSIENT_DURABILITY_QOS: k = "transient"; break;
    case DDS_PERSISTENT_DURABILITY_QOS: k = "persistent"; break;
    default: snprintf (buf, sizeof (buf), "invalid (%d)", (int) q->kind); k = buf; break;
  }
  printf ("  durability: kind = %s\n", k);
}

void qp_presentation (const DDS_PresentationQosPolicy *q)
{
  char buf[40];
  char *k;
  switch (q->access_scope)
  {
    case DDS_INSTANCE_PRESENTATION_QOS: k = "instance"; break;
    case DDS_TOPIC_PRESENTATION_QOS: k = "topic"; break;
    case DDS_GROUP_PRESENTATION_QOS: k = "group"; break;
    default: snprintf (buf, sizeof (buf), "invalid (%d)", (int) q->access_scope); k = buf; break;
  }
  printf ("  presentation: scope = %s, coherent_access = %s, ordered_access = %s\n", k,
          q->coherent_access ? "true" : "false",
          q->ordered_access ? "true" : "false");
}

int duration_is_infinite (const DDS_Duration_t *d)
{
  return d->sec == DDS_DURATION_INFINITE_SEC && d->nanosec == DDS_DURATION_INFINITE_NSEC;
}

void qp_deadline (const DDS_DeadlineQosPolicy *q)
{
  printf ("  deadline: period = ");
  if (duration_is_infinite (&q->period))
    printf ("infinite\n");
  else
    printf ("%d.%09d\n", q->period.sec, q->period.nanosec);
}

void qp_latency_budget (const DDS_LatencyBudgetQosPolicy *q)
{
  printf ("  latency_budget: duration = ");
  if (duration_is_infinite (&q->duration))
    printf ("infinite\n");
  else
    printf ("%d.%09d\n", q->duration.sec, q->duration.nanosec);
}

void qp_ownership (const DDS_OwnershipQosPolicy *q)
{
  char buf[40];
  char *k;
  switch (q->kind)
  {
    case DDS_SHARED_OWNERSHIP_QOS: k = "shared"; break;
    case DDS_EXCLUSIVE_OWNERSHIP_QOS: k = "exclusive"; break;
    default: snprintf (buf, sizeof (buf), "invalid (%d)", (int) q->kind); k = buf; break;
  }
  printf ("  ownership: kind = %s\n", k);
}

void qp_ownership_strength (const DDS_OwnershipStrengthQosPolicy *q)
{
  printf ("  ownership_strength: value = %d\n", q->value);
}

void qp_liveliness (const DDS_LivelinessQosPolicy *q)
{
  char buf[40];
  char *k;
  switch (q->kind)
  {
    case DDS_AUTOMATIC_LIVELINESS_QOS: k = "automatic"; break;
    case DDS_MANUAL_BY_PARTICIPANT_LIVELINESS_QOS: k = "manual-by-participant"; break;
    case DDS_MANUAL_BY_TOPIC_LIVELINESS_QOS: k = "manual-by-topic"; break;
    default: snprintf (buf, sizeof (buf), "invalid (%d)", (int) q->kind); k = buf; break;
  }
  printf ("  liveliness: kind = %s, lease_duration = ", k);
  if (duration_is_infinite (&q->lease_duration))
    printf ("infinite\n");
  else
    printf ("%d.%09d\n", q->lease_duration.sec, q->lease_duration.nanosec);
}

void qp_time_based_filter (const DDS_TimeBasedFilterQosPolicy *q)
{
  printf ("  time_based_filter: minimum_separation = ");
  if (duration_is_infinite (&q->minimum_separation))
    printf ("infinite\n");
  else
    printf ("%d.%09d\n", q->minimum_separation.sec, q->minimum_separation.nanosec);
}

void qp_lifespan (const DDS_LifespanQosPolicy *q)
{
  printf ("  lifespan: duration = ");
  if (duration_is_infinite (&q->duration))
    printf ("infinite\n");
  else
    printf ("%d.%09d\n", q->duration.sec, q->duration.nanosec);
}

void qp_partition (const DDS_PartitionQosPolicy *q)
{
  const DDS_StringSeq *s = &q->name;
  printf ("  partition: name = ");
  if (s->_length == 0)
    printf ("(default)");
  else if (s->_length == 1)
    printf ("%s", s->_buffer[0]);
  else
  {
    unsigned i;
    printf ("{");
    for (i = 0; i < s->_length; i++)
      printf ("%s%s", (i > 0) ? "," : "", s->_buffer[i]);
    printf ("}");
  }
  printf ("\n");
}

void qp_reliability (const DDS_ReliabilityQosPolicy *q)
{
  char buf[40];
  char *k;
  switch (q->kind)
  {
    case DDS_BEST_EFFORT_RELIABILITY_QOS: k = "best-effort"; break;
    case DDS_RELIABLE_RELIABILITY_QOS: k = "reliable"; break;
    default: snprintf (buf, sizeof (buf), "invalid (%d)", (int) q->kind); k = buf; break;
  }
  printf ("  reliability: kind = %s, max_blocking_time = ", k);
  if (duration_is_infinite (&q->max_blocking_time))
    printf ("infinite");
  else
    printf ("%d.%09d", q->max_blocking_time.sec, q->max_blocking_time.nanosec);
  printf (", synchronous = %s\n", q->synchronous ? "true" : "false");
}

void qp_destination_order (const DDS_DestinationOrderQosPolicy *q)
{
  char buf[40];
  char *k;
  switch (q->kind)
  {
    case DDS_BY_RECEPTION_TIMESTAMP_DESTINATIONORDER_QOS: k = "by-reception-timestamp"; break;
    case DDS_BY_SOURCE_TIMESTAMP_DESTINATIONORDER_QOS: k = "by-source-timestamp"; break;
    default: snprintf (buf, sizeof (buf), "invalid (%d)", (int) q->kind); k = buf; break;
  }
  printf ("  destination_order: kind = %s\n", k);
}

void qp_history_kind_1 (DDS_HistoryQosPolicyKind kind, int indent)
{
  char buf[40];
  char *k;
  switch (kind)
  {
    case DDS_KEEP_LAST_HISTORY_QOS: k = "keep-last"; break;
    case DDS_KEEP_ALL_HISTORY_QOS: k = "keep-all"; break;
    default: snprintf (buf, sizeof (buf), "invalid (%d)", (int) kind); k = buf; break;
  }
  printf ("%*.*shistory: kind = %s", indent, indent, "", k);
}

void qp_history (const DDS_HistoryQosPolicy *q)
{
  qp_history_kind_1 (q->kind, 2);
  if (q->kind == DDS_KEEP_LAST_HISTORY_QOS)
    printf (", depth = %d\n", q->depth);
  else
    printf (", (depth = %d)\n", q->depth);
}

void qp_resource_limits_1 (int max_samples, int max_instances, int max_samples_per_instance, int indent)
{
  printf ("%*.*sresource_limits: max_samples = ", indent, indent, "");
  if (max_samples == DDS_LENGTH_UNLIMITED)
    printf ("unlimited");
  else
    printf ("%d", max_samples);
  printf (", max_instances = ");
  if (max_instances == DDS_LENGTH_UNLIMITED)
    printf ("unlimited");
  else
    printf ("%d", max_instances);
  printf (", max_samples_per_instance = ");
  if (max_samples_per_instance == DDS_LENGTH_UNLIMITED)
    printf ("unlimited\n");
  else
    printf ("%d\n", max_samples_per_instance);
}

void qp_resource_limits (const DDS_ResourceLimitsQosPolicy *q)
{
  qp_resource_limits_1 (q->max_samples, q->max_instances, q->max_samples_per_instance, 2);
}

void qp_entity_factory (const DDS_EntityFactoryQosPolicy *q)
{
  printf ("  entity_factory: autoenable_created_entities = %s\n",
          q->autoenable_created_entities ? "true" : "false");
}

void qp_writer_data_lifecycle (const DDS_WriterDataLifecycleQosPolicy *q)
{
  printf ("  writer_data_lifecycle: autodispose_unregistered_instances = %s, autopurge_suspended_samples_delay = ",
          q->autodispose_unregistered_instances ? "true" : "false");
  if (duration_is_infinite (&q->autopurge_suspended_samples_delay))
    printf ("infinite");
  else
    printf ("%d.%09d", q->autopurge_suspended_samples_delay.sec, q->autopurge_suspended_samples_delay.nanosec);
  printf (", autounregister_instance_delay = ");
  if (duration_is_infinite (&q->autounregister_instance_delay))
    printf ("infinite\n");
  else
    printf ("%d.%09d\n", q->autounregister_instance_delay.sec, q->autounregister_instance_delay.nanosec);
}

void qp_durability_service (const DDS_DurabilityServiceQosPolicy *q)
{
  printf ("  durability_service:\n");
  printf ("    service_cleanup_delay = ");
  if (duration_is_infinite (&q->service_cleanup_delay))
    printf ("infinite\n");
  else
    printf ("%d.%09d\n", q->service_cleanup_delay.sec, q->service_cleanup_delay.nanosec);
  qp_history_kind_1 (q->history_kind, 4); printf ("\n");
  printf ("    history_depth = %d\n", q->history_depth);
  qp_resource_limits_1 (q->max_samples, q->max_instances, q->max_samples_per_instance, 4);
}

#ifdef _DDS_sequence_DDS_CMPublisherBuiltinTopicData_defined
void qp_reader_data_lifecycle (const DDS_ReaderDataLifecycleQosPolicy *q)
{
  printf ("  reader_data_lifecycle: autopurge_nowriter_samples_delay = ");
  if (duration_is_infinite (&q->autopurge_nowriter_samples_delay))
    printf ("infinite");
  else
    printf ("%d.%09d", q->autopurge_nowriter_samples_delay.sec, q->autopurge_nowriter_samples_delay.nanosec);
  printf (", autopurge_disposed_samples_delay = ");
  if (duration_is_infinite (&q->autopurge_disposed_samples_delay))
    printf ("infinite");
  else
    printf ("%d.%09d", q->autopurge_disposed_samples_delay.sec, q->autopurge_disposed_samples_delay.nanosec);
  printf (", enable_invalid_samples = %s\n", q->enable_invalid_samples ? "true" : "false");
}

void qp_subscription_keys (const DDS_UserKeyQosPolicy *q)
{
  printf ("  subscription_keys: enable = %s expression = %s\n", q->enable ? "true" : "false", q->expression);
}

void qp_reader_lifespan (const DDS_ReaderLifespanQosPolicy *q)
{
  printf ("  reader_lifespan: use_lifespan = %s, ", q->use_lifespan ? "true" : "false");
  if (!q->use_lifespan)
    printf ("(");
  printf ("duration = ");
  if (duration_is_infinite (&q->duration))
    printf ("infinite");
  else
    printf ("%d.%09d", q->duration.sec, q->duration.nanosec);
  if (!q->use_lifespan)
    printf (")");
  printf ("\n");
}

void qp_share (const DDS_ShareQosPolicy *q)
{
  printf ("  share: enable = %s", q->enable ? "true" : "false");
  if (q->enable)
    printf (", name = %s", q->name);
  printf ("\n");
}
#endif

#if defined (_DDS_sequence_DDS_CMParticipantBuiltinTopicData_defined) || defined (_DDS_sequence_DDS_CMPublisherBuiltinTopicData_defined)
void qp_product_data (const DDS_ProductDataQosPolicy *q)
{
  printf ("  product_data: value = %s\n", q->value);
}
#endif

void print_info (const DDS_SampleInfo *info, const char **hdr)
{
  print_header(hdr);
  if (monitor_flag)
  {
    char st[12];
    const char *s = st;
    switch (info->instance_state)
    {
      case DDS_ALIVE_INSTANCE_STATE: s = "+"; break;
      case DDS_NOT_ALIVE_DISPOSED_INSTANCE_STATE: s = "-"; break;
      case DDS_NOT_ALIVE_NO_WRITERS_INSTANCE_STATE: s = "u"; break;
      default:
        sprintf (st, "%d:", info->instance_state);
        break;
    }
    printf ("%s", s);
  }
}

int topic_filter_matches(const char *topic)
{
  int r = regexec(&topic_filter_regex, topic, 0, NULL, 0);
  if (r == 0)
    return 1;
  else if (r == REG_NOMATCH)
    return 0;
  else
  {
    char errbuf[256];
    regerror (r, &topic_filter_regex, errbuf, sizeof (errbuf));
    fprintf (stderr, "regexec(%s) error: %s\n", topic, errbuf);
    abort();
  }
}

int key_filter_matches (regex_t *filter, const DDS_BuiltinTopicKey_t key)
{
  char tmp[128];
  int r;
  snprintf (tmp, sizeof (tmp), "%u:%u:%u\n", (unsigned) key[0], (unsigned) key[1], (unsigned) key[2]);
  if ((r = regexec(filter, tmp, 0, NULL, 0)) == 0)
    return 1;
  else if (r == REG_NOMATCH)
    return 0;
  else
  {
    char errbuf[256];
    regerror (r, filter, errbuf, sizeof (errbuf));
    fprintf (stderr, "regexec(%s) error: %s\n", tmp, errbuf);
    abort();
  }
}

void print_topic (const DDS_SampleInfo *info, const DDS_TopicBuiltinTopicData *d, int valid_data, const char **hdr)
{
  if (!key_filter_matches(&key_filter_regex, d->key))
    return;
  if (valid_data && !topic_filter_matches(d->name))
    return;
  print_info (info, hdr);
  printf ("TOPIC:\n");
  printf ("  key = %u:%u:%u\n", (unsigned) d->key[0], (unsigned) d->key[1], (unsigned) d->key[2]);
  if (!valid_data)
    return;
  printf ("  name = %s\n", d->name);
  printf ("  type_name = %s\n", d->type_name);
  qp_durability (&d->durability);
  qp_deadline (&d->deadline);
  qp_latency_budget (&d->latency_budget);
  qp_liveliness (&d->liveliness);
  qp_reliability (&d->reliability);
  qp_transport_priority (&d->transport_priority);
  qp_lifespan (&d->lifespan);
  qp_destination_order (&d->destination_order);
  qp_history (&d->history);
  qp_resource_limits (&d->resource_limits);
  qp_ownership (&d->ownership);
  qp_topic_data (&d->topic_data);
}

void print_participant (const DDS_SampleInfo *info, const DDS_ParticipantBuiltinTopicData *d, int valid_data, const char **hdr)
{
  if (!key_filter_matches(&key_filter_regex, d->key))
    return;
  print_info (info, hdr);
  printf ("PARTICIPANT:\n");
  printf ("  key = %u:%u:%u\n", (unsigned) d->key[0], (unsigned) d->key[1], (unsigned) d->key[2]);
  if (!valid_data)
    return;
  qp_user_data (&d->user_data);
}

void print_subscription (const DDS_SampleInfo *info, const DDS_SubscriptionBuiltinTopicData *d, int valid_data, const char **hdr)
{
  if (!key_filter_matches(&key_filter_regex, d->key))
    return;
  if (valid_data && !(topic_filter_matches(d->topic_name) && key_filter_matches(&participant_key_filter_regex, d->participant_key)))
    return;
  print_info (info, hdr);
  printf ("SUBSCRIPTION:\n");
  printf ("  key = %u:%u:%u\n", (unsigned) d->key[0], (unsigned) d->key[1], (unsigned) d->key[2]);
  if (!valid_data)
    return;
  printf ("  participant_key = %u:%u:%u\n", (unsigned) d->participant_key[0], (unsigned) d->participant_key[1], (unsigned) d->participant_key[2]);
  printf ("  topic_name = %s\n", d->topic_name);
  printf ("  type_name = %s\n", d->type_name);
  qp_durability (&d->durability);
  qp_deadline (&d->deadline);
  qp_latency_budget (&d->latency_budget);
  qp_liveliness (&d->liveliness);
  qp_reliability (&d->reliability);
  qp_ownership (&d->ownership);
  qp_destination_order (&d->destination_order);
  qp_user_data (&d->user_data);
  qp_time_based_filter (&d->time_based_filter);
  qp_presentation (&d->presentation);
  qp_partition (&d->partition);
  qp_topic_data (&d->topic_data);
  qp_group_data (&d->group_data);
}

void print_publication (const DDS_SampleInfo *info, const DDS_PublicationBuiltinTopicData *d, int valid_data, const char **hdr)
{
  if (!key_filter_matches(&key_filter_regex, d->key))
    return;
  if (valid_data && !(topic_filter_matches(d->topic_name) && key_filter_matches(&participant_key_filter_regex, d->participant_key)))
    return;
  print_info (info, hdr);
  printf ("PUBLICATION:\n");
  printf ("  key = %u:%u:%u\n", (unsigned) d->key[0], (unsigned) d->key[1], (unsigned) d->key[2]);
  if (!valid_data)
    return;
  printf ("  participant_key = %u:%u:%u\n", (unsigned) d->participant_key[0], (unsigned) d->participant_key[1], (unsigned) d->participant_key[2]);
  printf ("  topic_name = %s\n", d->topic_name);
  printf ("  type_name = %s\n", d->type_name);
  qp_durability (&d->durability);
  qp_deadline (&d->deadline);
  qp_latency_budget (&d->latency_budget);
  qp_liveliness (&d->liveliness);
  qp_reliability (&d->reliability);
  qp_lifespan (&d->lifespan);
  qp_destination_order (&d->destination_order);
  qp_user_data (&d->user_data);
  qp_ownership (&d->ownership);
  qp_ownership_strength (&d->ownership_strength);
  qp_presentation (&d->presentation);
  qp_partition (&d->partition);
  qp_topic_data (&d->topic_data);
  qp_group_data (&d->group_data);
}

#ifdef _DDS_sequence_DDS_CMParticipantBuiltinTopicData_defined
void print_cmparticipant (const DDS_SampleInfo *info, const DDS_CMParticipantBuiltinTopicData *d, int valid_data, const char **hdr)
{
  if (!key_filter_matches(&key_filter_regex, d->key))
    return;
  print_info (info, hdr);
  printf ("CMPARTICIPANT:\n");
  printf ("  key = %u:%u:%u\n", (unsigned) d->key[0], (unsigned) d->key[1], (unsigned) d->key[2]);
  if (!valid_data)
    return;
  qp_product_data (&d->product);
}
#endif

#ifdef _DDS_sequence_DDS_CMPublisherBuiltinTopicData_defined
void print_cmpublisher (const DDS_SampleInfo *info, const DDS_CMPublisherBuiltinTopicData *d, int valid_data, const char **hdr)
{
  if (!key_filter_matches(&key_filter_regex, d->key))
    return;
  if (valid_data && !key_filter_matches(&participant_key_filter_regex, d->participant_key))
    return;
  print_info (info, hdr);
  printf ("CMPUBLISHER:\n");
  printf ("  key = %u:%u:%u\n", (unsigned) d->key[0], (unsigned) d->key[1], (unsigned) d->key[2]);
  if (!valid_data)
    return;
  printf ("  participant_key = %u:%u:%u\n", (unsigned) d->participant_key[0], (unsigned) d->participant_key[1], (unsigned) d->participant_key[2]);
  printf ("  name = %s\n", d->name);
  qp_entity_factory (&d->entity_factory);
  qp_partition (&d->partition);
  qp_product_data (&d->product);
}

void print_cmsubscriber (const DDS_SampleInfo *info, const DDS_CMSubscriberBuiltinTopicData *d, int valid_data, const char **hdr)
{
  if (!key_filter_matches(&key_filter_regex, d->key))
    return;
  if (valid_data && !key_filter_matches(&participant_key_filter_regex, d->participant_key))
    return;
  print_info (info, hdr);
  printf ("CMSUBSCRIBER:\n");
  printf ("  key = %u:%u:%u\n", (unsigned) d->key[0], (unsigned) d->key[1], (unsigned) d->key[2]);
  if (!valid_data)
    return;
  printf ("  participant_key = %u:%u:%u\n", (unsigned) d->participant_key[0], (unsigned) d->participant_key[1], (unsigned) d->participant_key[2]);
  printf ("  name = %s\n", d->name);
  qp_entity_factory (&d->entity_factory);
  qp_partition (&d->partition);
  qp_share (&d->share);
  qp_product_data (&d->product);
}

void print_cmdatawriter (const DDS_SampleInfo *info, const DDS_CMDataWriterBuiltinTopicData *d, int valid_data, const char **hdr)
{
  if (!key_filter_matches(&key_filter_regex, d->key))
    return;
  if (valid_data && !key_filter_matches(&group_key_filter_regex, d->publisher_key))
    return;
  print_info (info, hdr);
  printf ("CMDATAWRITER:\n");
  printf ("  key = %u:%u:%u\n", (unsigned) d->key[0], (unsigned) d->key[1], (unsigned) d->key[2]);
  if (!valid_data)
    return;
  printf ("  publisher_key = %u:%u:%u\n", (unsigned) d->publisher_key[0], (unsigned) d->publisher_key[1], (unsigned) d->publisher_key[2]);
  printf ("  name = %s\n", d->name);
  qp_history (&d->history);
  qp_resource_limits (&d->resource_limits);
  qp_writer_data_lifecycle (&d->writer_data_lifecycle);
  qp_product_data (&d->product);
}

void print_cmdatareader (const DDS_SampleInfo *info, const DDS_CMDataReaderBuiltinTopicData *d, int valid_data, const char **hdr)
{
  if (!key_filter_matches(&key_filter_regex, d->key))
    return;
  if (valid_data && !key_filter_matches(&group_key_filter_regex, d->subscriber_key))
    return;
  print_info (info, hdr);
  printf ("CMDATAREADER:\n");
  printf ("  key = %u:%u:%u\n", (unsigned) d->key[0], (unsigned) d->key[1], (unsigned) d->key[2]);
  if (!valid_data)
    return;
  printf ("  subscriber_key = %u:%u:%u\n", (unsigned) d->subscriber_key[0], (unsigned) d->subscriber_key[1], (unsigned) d->subscriber_key[2]);
  printf ("  name = %s\n", d->name);
  qp_history (&d->history);
  qp_resource_limits (&d->resource_limits);
  qp_reader_data_lifecycle (&d->reader_data_lifecycle);
  qp_subscription_keys (&d->subscription_keys);
  qp_reader_lifespan (&d->reader_lifespan);
  qp_share (&d->share);
  qp_product_data (&d->product);
}
#endif

TOPIC_HANDLER (h_DCPSTopic, Topic, print_topic)
TOPIC_HANDLER (h_DCPSParticipant, Participant, print_participant)
TOPIC_HANDLER (h_DCPSSubscription, Subscription, print_subscription)
TOPIC_HANDLER (h_DCPSPublication, Publication, print_publication)
#ifdef _DDS_sequence_DDS_CMParticipantBuiltinTopicData_defined
TOPIC_HANDLER (h_CMParticipant, CMParticipant, print_cmparticipant)
#endif
#ifdef _DDS_sequence_DDS_CMPublisherBuiltinTopicData_defined
TOPIC_HANDLER (h_CMPublisher, CMPublisher, print_cmpublisher)
TOPIC_HANDLER (h_CMSubscriber, CMSubscriber, print_cmsubscriber)
TOPIC_HANDLER (h_CMDataWriter, CMDataWriter, print_cmdatawriter)
TOPIC_HANDLER (h_CMDataReader, CMDataReader, print_cmdatareader)
#endif

static struct topictab {
  const char *name;
  int (*handler) (const char *fmt, DDS_DataReader rd);
} topictab[] = {
  { "DCPSTopic", h_DCPSTopic },
  { "DCPSParticipant", h_DCPSParticipant },
  { "DCPSSubscription", h_DCPSSubscription },
  { "DCPSPublication", h_DCPSPublication },
#ifdef _DDS_sequence_DDS_CMParticipantBuiltinTopicData_defined
  { "CMParticipant", h_CMParticipant },
#endif
#ifdef _DDS_sequence_DDS_CMPublisherBuiltinTopicData_defined
  { "CMPublisher", h_CMPublisher },
  { "CMSubscriber", h_CMSubscriber },
  { "CMDataWriter", h_CMDataWriter },
  { "CMDataReader", h_CMDataReader },
#endif
  { 0, 0 }
};

static void usage (void)
{
  int i;
  fprintf (stderr, "\
usage: %s [OPTIONS] TOPIC...\n\
\n\
OPTIONS:\n\
  -m                 monitor and print changes:\n\
  -t TOPIC           only topics matching TOPIC (POSIX extended regex)\n\
  -k KEY             only entities with key KEY (POSIX extended regex)\n\
  -p KEY             only entities with participant key KEY (POSIX extended regex)\n\
  -g KEY             only entities with subscriber/publisher KEY (POSIX extended regex)\n\
\n\
TOPIC:\n\
", argv0);
  for (i = 0; topictab[i].name; i++)
    fprintf (stderr, "  %s\n", topictab[i].name);
  exit (1);
}

int main(int argc, char *argv[])
{
  DDS_Subscriber sub;
  DDS_DataReader dr[MAX_DR];
  DDS_ReadCondition rdcond[MAX_DR];
  DDS_WaitSet ws;
  DDS_Duration_t waitForSamplesToTakeTimeout = DDS_DURATION_INFINITE;
  DDS_Duration_t waitForHistoricalDataTimeout = DDS_DURATION_INFINITE;
  DDS_ConditionSeq *gl = NULL;
  DDS_ReturnCode_t rc;
  DDS_DomainParticipantFactory dpf;
  DDS_DomainParticipant dp;
  const char *topic_filter = ".*";
  const char *key_filter = ".*";
  const char *participant_key_filter = ".*";
  const char *group_key_filter = ".*";
  unsigned lsidx, ntopics;
  char **topics;
  int opt;
  int topictab_idx[MAX_DR];

  argv0 = argv[0];
  while ((opt = getopt (argc, argv, "mt:k:p:g:")) != EOF)
    switch (opt)
    {
      case 'm':
        monitor_flag = 1;
        break;
      case 't':
        topic_filter = optarg;
        break;
      case 'k':
        key_filter = optarg;
        break;
      case 'p':
        participant_key_filter = optarg;
        break;
      case 'g':
        group_key_filter = optarg;
        break;
      default:
        usage ();
    }
  ntopics = (unsigned) (argc - optind);
  topics = &argv[optind];
  if (ntopics < 1)
    usage ();
  else if (ntopics > MAX_DR)
  {
    fprintf (stderr, "%s: too many arguments\n", argv[0]);
    return 1;
  }
  for (lsidx = 0; lsidx < ntopics; lsidx++)
  {
    int i;
    for (i = 0; topictab[i].name; i++)
      if (strcmp (topictab[i].name, topics[lsidx]) == 0)
        break;
    if (topictab[i].name == NULL)
    {
      fprintf (stderr, "%s: %s: unknown topic\n", argv[0], topics[lsidx]);
      return 1;
    }
    topictab_idx[lsidx] = i;
  }

  {
    int r;
    if ((r = regcomp (&topic_filter_regex, topic_filter, REG_EXTENDED)) != 0)
    {
      char errbuf[256];
      regerror (r, &topic_filter_regex, errbuf, sizeof (errbuf));
      fprintf (stderr, "%s: %s\n", topic_filter, errbuf);
      return 1;
    }
    if ((r = regcomp (&key_filter_regex, key_filter, REG_EXTENDED)) != 0)
    {
      char errbuf[256];
      regerror (r, &key_filter_regex, errbuf, sizeof (errbuf));
      fprintf (stderr, "%s: %s\n", key_filter, errbuf);
      return 1;
    }
    if ((r = regcomp (&participant_key_filter_regex, participant_key_filter, REG_EXTENDED)) != 0)
    {
      char errbuf[256];
      regerror (r, &participant_key_filter_regex, errbuf, sizeof (errbuf));
      fprintf (stderr, "%s: %s\n", participant_key_filter, errbuf);
      return 1;
    }
    if ((r = regcomp (&group_key_filter_regex, group_key_filter, REG_EXTENDED)) != 0)
    {
      char errbuf[256];
      regerror (r, &group_key_filter_regex, errbuf, sizeof (errbuf));
      fprintf (stderr, "%s: %s\n", group_key_filter, errbuf);
      return 1;
    }
  }

  if ((dpf = DDS_DomainParticipantFactory_get_instance ()) == 0)
  {
    printf ("%s: DDS_DomainParticipantFactory_get_instance failed\n", argv[0]);
    return 1;
  }
  if ((dp = DDS_DomainParticipantFactory_create_participant (dpf, DDS_DOMAIN_ID_DEFAULT, DDS_PARTICIPANT_QOS_DEFAULT, NULL, DDS_STATUS_MASK_NONE)) == 0)
  {
    printf ("%s: DDS_DomainParticipantFactory_create_participant failed\n", argv[0]);
    return 1;
  }

  if ((sub = DDS_DomainParticipant_get_builtin_subscriber (dp)) == NULL)
  {
    fprintf (stderr, "%s: DDS_DomainParticipant_get_builtin_subscriber failed\n", argv[0]);
    return 1;
  }
  /* First create all data readers, then wait for historical data, I'm
     guessing that way is potentially faster & never slower than doing
     them one at a time. */
  for (lsidx = 0; lsidx < ntopics; lsidx++)
  {
    const char *topic = topics[lsidx];
    if ((dr[lsidx] = DDS_Subscriber_lookup_datareader (sub, topic)) == NULL)
    {
      fprintf (stderr, "%s: DDS_Subscriber_lookup_datareader(%s) failed\n", argv[0], topic);
      return 1;
    }
  }
  if (waitForHistoricalDataTimeout.sec != 0 || waitForHistoricalDataTimeout.nanosec != 0)
  {
    for (lsidx = 0; lsidx < ntopics; lsidx++)
    {
      const char *topic = topics[lsidx];
      if ((rc = DDS_DataReader_wait_for_historical_data (dr[lsidx], &waitForHistoricalDataTimeout)) != DDS_RETCODE_OK)
      {
        fprintf (stderr, "%s: DDS_DataReader_wait_for_historical_data(%s) failed (%d)\n", argv[0], topic, rc);
      }
    }
  }

  /* Each data reader has a condition in the waitset */
  if ((ws = DDS_WaitSet__alloc ()) == 0)
  {
    fprintf (stderr, "%s: DDS_WaitSet__alloc failed\n", argv[0]);
    return 1;
  }
  for (lsidx = 0; lsidx < ntopics; lsidx++)
  {
    const char *topic = topics[lsidx];
    if ((rdcond[lsidx] = DDS_DataReader_create_readcondition (dr[lsidx], DDS_ANY_SAMPLE_STATE, DDS_ANY_VIEW_STATE, DDS_ANY_INSTANCE_STATE)) == 0)
    {
      fprintf (stderr, "%s: DDS_DataReader_create_readcondition(%s) failed\n", argv[0], topic);
      return 1;
    }
    if ((rc = DDS_WaitSet_attach_condition (ws, rdcond[lsidx])) != DDS_RETCODE_OK)
    {
      fprintf (stderr, "%s: DDS_WaitSet_attach_condition(%s) failed (%d)\n", argv[0], topic, rc);
      return 1;
    }
  }
  if ((gl = DDS_ConditionSeq__alloc ()) == 0)
  {
    fprintf (stderr, "%s: DDS_ConditionSeq__alloc failed\n", argv[0]);
    return 1;
  }
  gl->_maximum = ntopics + 1;
  gl->_length = 0;
  if ((gl->_buffer = DDS_ConditionSeq_allocbuf (gl->_maximum)) == 0)
  {
    fprintf (stderr, "%s: DDS_ConditionSeq__allocbuf failed\n", argv[0]);
    return 1;
  }

  if (!monitor_flag)
  {
    waitForSamplesToTakeTimeout.sec = DDS_DURATION_ZERO_SEC;
    waitForSamplesToTakeTimeout.nanosec = DDS_DURATION_ZERO_NSEC;
  }
  else
  {
    waitForSamplesToTakeTimeout.sec = DDS_DURATION_INFINITE_SEC;
    waitForSamplesToTakeTimeout.nanosec = DDS_DURATION_INFINITE_NSEC;
  }

  if ((sigguard = DDS_GuardCondition__alloc ()) == NULL)
  {
    fprintf (stderr, "%s: DDS_GuardCondition__alloc failed\n", argv[0]);
    return 1;
  }
  if ((rc = DDS_WaitSet_attach_condition (ws, sigguard)) != DDS_RETCODE_OK)
  {
    fprintf (stderr, "%s: DDS_WaitSet_attach_condition(<sigguard>) failed (%d)\n", argv[0], rc);
    return 1;
  }

  if (pipe (sigpipe) == -1)
  {
    perror ("pipe");
    return 1;
  }
  pthread_create (&sigh_tid, NULL, sigh_thread, NULL);
  signal (SIGINT, sigh);
  signal (SIGTERM, sigh);

  {
    unsigned i, j;
    int terminate = 0;
    do {
      if ((rc = DDS_WaitSet_wait (ws, gl, &waitForSamplesToTakeTimeout)) != DDS_RETCODE_OK)
      {
        fprintf (stderr, "%s: DDS_WaitSet_wait failed (%d)\n", argv[0], rc);
        return 1;
      }
      /* There must be more elegant way (if there isn't there should be!) */
      for (i = 0; i < ntopics; i++)
      {
        for (j = 0; j < gl->_length; j++)
          if (gl->_buffer[j] == rdcond[i])
            topictab[topictab_idx[i]].handler ((ntopics > 1) ? topics[i] : NULL, dr[i]);
      }
      for (i = 0; i < gl->_length; i++)
      {
        if (gl->_buffer[i] == sigguard)
        {
          terminate = 1;
          break;
        }
      }
    } while (!terminate && monitor_flag);
  }

  /* stop signal handler thread */
  {
    int sig = -1;
    write (sigpipe[1], &sig, sizeof (sig));
    pthread_join (sigh_tid, NULL);
  }

  DDS_free (gl);
  if ((rc = DDS_WaitSet_detach_condition (ws, sigguard)) != DDS_RETCODE_OK)
  {
    fprintf (stderr, "%s: DDS_WaitSet_detach_condition(<sigguard>) failed (%d)\n", argv[0], rc);
    return 1;
  }
  DDS_free (sigguard);
  for (lsidx = 0; lsidx < ntopics; lsidx++)
  {
    if ((rc = DDS_WaitSet_detach_condition (ws, rdcond[lsidx])) != DDS_RETCODE_OK)
    {
      fprintf (stderr, "%s: DDS_WaitSet_detach_condition(%s) failed (%d)\n", argv[0], topics[lsidx], rc);
      return 1;
    }
    DDS_DataReader_delete_readcondition (dr[lsidx], rdcond[lsidx]);
  }
  DDS_free (ws);
  if ((rc = DDS_DomainParticipant_delete_contained_entities (dp)) != DDS_RETCODE_OK)
  {
    fprintf (stderr, "%s: DDS_DomainParticipant_delete_contained_entities failed (%d)\n", argv[0], rc);
    return 1;
  }
  if ((rc = DDS_DomainParticipantFactory_delete_participant (dpf, dp)) != DDS_RETCODE_OK)
  {
    fprintf (stderr, "%s: DDS_DomainParticipantFactory_delete_participant failed (%d)\n", argv[0], rc);
    return 1;
  }
  return 0;
}
