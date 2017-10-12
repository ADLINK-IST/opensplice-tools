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
#define _ISOC99_SOURCE
#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "tglib.h"

static DDS_Topic find_topic(DDS_DomainParticipant dp, const char *name, const DDS_Duration_t *timeout)
{
  DDS_Topic tp;

  /* A historical accident has caused subtle issues with a generic reader for the built-in topics included in the DDS spec (as well as a problem for DCPSDelivery). */
  if (strcmp(name, "DCPSParticipant") == 0 || strcmp(name, "DCPSTopic") == 0 ||
      strcmp(name, "DCPSSubscription") == 0 || strcmp(name, "DCPSPublication") == 0)
  {
    DDS_Subscriber sub;
    if ((sub = DDS_DomainParticipant_get_builtin_subscriber(dp)) == NULL)
      error("DDS_DomainParticipant_get_builtin_subscriber failed\n");
    if ((DDS_Subscriber_lookup_datareader(sub, name)) == NULL)
      error("DDS_Subscriber_lookup_datareader failed\n");
    if ((tp = DDS_DomainParticipant_find_topic(dp, name, timeout)) == NULL)
      error("topic %s not found\n", name);
  }
  else if (strcmp(name, "DCPSDelivery") == 0)
  {
    /* workaround for a bug */
    static const char *tn = "kernelModule::v_deliveryInfo";
    static const char *kl = "";
    static const char *md = "<MetaData version=\"1.0.0\"><Module name=\"kernelModule\"><Struct name=\"v_gid_s\"><Member name=\"systemId\"><ULong/></Member><Member name=\"localId\"><ULong/></Member><Member name=\"serial\"><ULong/></Member></Struct><TypeDef name=\"v_gid\"><Type name=\"v_gid_s\"/></TypeDef><Struct name=\"v_deliveryInfo\"><Member name=\"writerGID\"><Type name=\"v_gid\"/></Member><Member name=\"readerGID\"><Type name=\"v_gid\"/></Member><Member name=\"sequenceNumber\"><ULong/></Member></Struct></Module></MetaData>";
    DDS_ReturnCode_t result;
    DDS_TypeSupport ts;
    DDS_TopicQos *tqos;
    if ((ts = DDS_TypeSupport__alloc(tn, kl, md)) == NULL)
      error("DDS_TypeSupport__alloc(%s) failed\n", tn);
    if ((result = DDS_TypeSupport_register_type(ts, dp, tn)) != DDS_RETCODE_OK)
      error("DDS_TypeSupport_register_type(%s) failed: %d (%s)\n", tn, (int) result, dds_strerror(result));
    DDS_free(ts);
    if ((tqos = DDS_TopicQos__alloc()) == NULL)
      error("DDS_TopicQos failed: %d (%s)\n", (int) result, dds_strerror(result));
    if ((result = DDS_DomainParticipant_get_default_topic_qos(dp, tqos)) != DDS_RETCODE_OK)
      error("DDS_DomainParticipant_get_default_topic_qos failed: %d (%s)\n", (int) result, dds_strerror(result));
    tqos->reliability.max_blocking_time.sec = 0;
    tqos->reliability.max_blocking_time.nanosec = 0;
    if ((tp = DDS_DomainParticipant_create_topic(dp, name, tn, tqos, NULL, DDS_STATUS_MASK_NONE)) == NULL)
      error("DDS_DomainParticipant_create_topic(%s) failed\n", name);
    DDS_free(tqos);
  }
  else
  {
    char *tn, *kl, *md;
    DDS_ReturnCode_t result;
    DDS_TypeSupport ts;

    if ((tp = DDS_DomainParticipant_find_topic(dp, name, timeout)) == NULL)
      error("topic %s not found\n", name);
    tn = DDS_Topic_get_type_name(tp);
    kl = DDS_Topic_get_keylist(tp);
    md = DDS_Topic_get_metadescription(tp);
    if ((ts = DDS_TypeSupport__alloc(tn, kl ? kl : "", md)) == NULL)
      error("DDS_TypeSupport__alloc(%s) failed\n", tn);
    if ((result = DDS_TypeSupport_register_type(ts, dp, tn)) != DDS_RETCODE_OK)
      error("DDS_TypeSupport_register_type(%s) failed: %d (%s)\n", tn, (int) result, dds_strerror(result));
    DDS_free(md);
    DDS_free(kl);
    DDS_free(tn);
    DDS_free(ts);

    /* Work around a double-free-at-shutdown issue caused by a find_topic without a type support having been registered */
    if ((result = DDS_DomainParticipant_delete_topic(dp, tp)) != DDS_RETCODE_OK)
      error("DDS_DomainParticipant_find_topic failed: %d (%s)\n", (int) result, dds_strerror(result));
    if ((tp = DDS_DomainParticipant_find_topic(dp, name, timeout)) == NULL)
      error("DDS_DomainParticipant_find_topic(2) failed\n");
  }

  return tp;
}

static int patmatch(const char *pat, const char *str)
{
  switch(*pat)
  {
    case 0:   return *str == 0;
    case '?': return *str == 0 ? 0 : patmatch(pat+1, str+1);
    case '*': return patmatch(pat+1, str) || (*str != 0 && patmatch(pat, str+1));
    default:  return *str != *pat ? 0 : patmatch(pat+1, str+1);
  }
}

static DDS_StatusCondition attach_reader_status_data_available(DDS_WaitSet ws, DDS_DataReader rd)
{
  DDS_StatusCondition sc;
  DDS_ReturnCode_t rc;
  if ((sc = DDS_DataReader_get_statuscondition(rd)) == NULL)
    error("DDS_DataReader_get_statuscondition failed\n");
  if ((rc = DDS_StatusCondition_set_enabled_statuses(sc, DDS_DATA_AVAILABLE_STATUS)) != DDS_RETCODE_OK)
    error("DDS_StatusCondition_set_enabled_statuses failed (%s)\n", dds_strerror(rc));
  if ((rc = DDS_WaitSet_attach_condition(ws, sc)) != DDS_RETCODE_OK)
    error("DDS_WaitSet_attach_condition failed (%s)\n", dds_strerror(rc));
  return sc;
}

struct rdlist {
  DDS_DataReader rd;
  DDS_StatusCondition sc;
  struct tgtopic *tgtp;
  struct rdlist *next;
};

int main (int argc, char **argv)
{
  DDS_Duration_t timeout = { 10, 0 };
  DDS_Subscriber bsub, sub;
  DDS_DataReader trd;
  DDS_StatusCondition tsc;
  struct qos *qos;
  DDS_ReturnCode_t result;
  struct DDS_sequence_s mseq;
  DDS_sequence_DDS_TopicBuiltinTopicData tseq;
  DDS_SampleInfoSeq iseq;
  DDS_WaitSet ws;
  DDS_ConditionSeq *gl;
  unsigned i;
  struct rdlist *rds = NULL;
  const char *topicpat;

  if (argc < 2)
  {
    fprintf(stderr, "usage %s TOPIC_PATTERN [PARTITION...]\n", argv[0]);
    return 1;
  }
  common_init(argv[0]);
  topicpat = argv[1];

  qos = new_subqos();
  sub = new_subscriber(qos, (unsigned)argc - 2, (const char **) (argv + 2));
  free_qos(qos);

  if ((ws = DDS_WaitSet__alloc ()) == NULL)
    error ("DDS_WaitSet__alloc failed\n");
  if ((gl = DDS_ConditionSeq__alloc ()) == NULL)
    error ("DDS_ConditionSeq__alloc failed\n");

  if ((bsub = DDS_DomainParticipant_get_builtin_subscriber(dp)) == NULL)
    error ("DDS_DomainParticipant_get_builtin_subscriber failed\n");
  if ((trd = DDS_Subscriber_lookup_datareader(bsub, "DCPSTopic")) == NULL)
    error ("DDS_Subscriber_lookup_datareader(DCPSTopic) failed\n");
  tsc = attach_reader_status_data_available (ws, trd);

  memset(&tseq, 0, sizeof(tseq));
  memset(&mseq, 0, sizeof(mseq));
  memset(&iseq, 0, sizeof(iseq));
  while(1)
  {
    DDS_Duration_t infinity = DDS_DURATION_INFINITE;
    unsigned k;
    if ((result = DDS_WaitSet_wait (ws, gl, &infinity)) != DDS_RETCODE_OK)
      error ("DDS_WaitSet_wait failed: %s\n", dds_strerror (result));

    for (k = 0; k < gl->_length; k++)
    {
      if (gl->_buffer[k] == tsc)
      {
        result = DDS_TopicBuiltinTopicDataDataReader_take (trd, &tseq, &iseq, DDS_LENGTH_UNLIMITED, DDS_ANY_SAMPLE_STATE, DDS_ANY_VIEW_STATE, DDS_ANY_INSTANCE_STATE);
        if (result != DDS_RETCODE_OK)
          error("take failed, error %s\n", dds_strerror(result));

        for (i = 0; i < tseq._length; i++)
        {
          DDS_SampleInfo const * const si = &iseq._buffer[i];
          if (si->valid_data)
          {
            DDS_TopicBuiltinTopicData const * const d = &tseq._buffer[i];
            struct rdlist *rd;
            DDS_Topic tp;

            if (!patmatch (topicpat, d->name))
              continue; /* nonmatching topic */
            for (rd = rds; rd; rd = rd->next)
              if (strcmp(d->name, rd->tgtp->name) == 0)
                break;
            if (rd != NULL)
              continue; /* already have a reader */

            printf ("[new topic: %s]\n", d->name);

            rd = malloc (sizeof (*rd));
            if ((tp = find_topic(dp, d->name, &timeout)) == NULL)
              error("topic %s not found\n", d->name);
            rd->tgtp = tgnew(tp, 0);
            qos = new_rdqos(sub, tp); /* uses topic qos */
            rd->rd = new_datareader(qos);
            free_qos(qos);
            rd->sc = attach_reader_status_data_available (ws, rd->rd);

            rd->next = rds;
            rds = rd;
          }
        }

        DDS_TopicBuiltinTopicDataDataReader_return_loan(trd, &tseq, &iseq);
      }
      else
      {
        struct rdlist *rd;
        struct tgstring str;
        for (rd = rds; rd; rd = rd->next)
          if (gl->_buffer[k] == rd->sc)
            break;
        if (rd == NULL)
          error ("DDS_WaitSet_wait returned an unknown condition\n");

        result = DDS_DataReader_take (rd->rd, &mseq, &iseq, DDS_LENGTH_UNLIMITED, DDS_ANY_SAMPLE_STATE, DDS_ANY_VIEW_STATE, DDS_ANY_INSTANCE_STATE);
        if (result != DDS_RETCODE_OK)
          error("take failed, error %s\n", dds_strerror(result));

        tgstring_init (&str, 0xffffffff);
        for (i = 0; i < mseq._length; i++)
        {
          DDS_SampleInfo const * const si = &iseq._buffer[i];
          if (si->valid_data)
          {
            printf("%s: ", rd->tgtp->name);
            tgprint(&str, rd->tgtp, (char *)mseq._buffer + i * rd->tgtp->size, TGPM_FIELDS);
            printf("%s\n", str.buf);
          }
        }
        tgstring_fini (&str);

        DDS_DataReader_return_loan(rd->rd, &mseq, &iseq);
      }
    }
  }
}
