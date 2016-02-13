#define _ISOC99_SOURCE
#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "tglib.h"

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
    if ((result = DDS_DomainParticipant_delete_subscriber(dp, sub)) != DDS_RETCODE_OK)
      error("DDS_DomainParticipant_delete_subscriber failed: error %d (%s)\n", (int) result, dds_strerror(result));
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

    /* Work around a double-free-at-shutdown issue caused by a find_topic without a type support having been registered */
    if ((result = DDS_DomainParticipant_delete_topic(dp, tp)) != DDS_RETCODE_OK) {
      error("DDS_DomainParticipant_find_topic failed: %d (%s)\n", (int) result, dds_strerror(result));
    }
    if ((tp = DDS_DomainParticipant_find_topic(dp, name, timeout)) == NULL) {
      error("DDS_DomainParticipant_find_topic(2) failed\n");
    }
  }

  return tp;
}

int main (int argc, char **argv)
{
  DDS_Topic tp;
  DDS_Duration_t timeout = { 10, 0 };
  DDS_Subscriber sub;
  DDS_DataReader rd;
  struct qos *qos;
  struct tgtopic *tgtp;
  const char *partitions[] = { "", "__BUILT-IN PARTITION__" };
  DDS_ReturnCode_t result;
  struct DDS_sequence_s mseq;
  DDS_SampleInfoSeq iseq;
  unsigned i;

  if (argc != 2)
  {
    fprintf(stderr, "usage %s TOPIC\n", argv[0]);
    return 1;
  }
  common_init(argv[0]);

  if ((tp = find_topic(dp, argv[1], &timeout)) == NULL)
    error("topic %s not found\n", argv[1]);
  tgtp = tgnew(tp, 0);

  qos = new_subqos();
  sub = new_subscriber(qos, 2, partitions);
  free_qos(qos);

  qos = new_rdqos(sub, tp); /* uses topic qos */
  rd = new_datareader(qos);
  free_qos(qos);

  memset(&mseq, 0, sizeof(mseq));
  memset(&iseq, 0, sizeof(iseq));
  while(1)
  {
    result = DDS_DataReader_take (rd, &mseq, &iseq, DDS_LENGTH_UNLIMITED, DDS_ANY_SAMPLE_STATE, DDS_ANY_VIEW_STATE, DDS_ANY_INSTANCE_STATE);
    if (result != DDS_RETCODE_OK && result != DDS_RETCODE_NO_DATA)
      error("take failed, error %s\n", dds_strerror(result));

    for (i = 0; i < mseq._length; i++)
    {
      DDS_SampleInfo const * const si = &iseq._buffer[i];
      if (si->valid_data)
      {
        tgprint(stdout, tgtp, (char *)mseq._buffer + i * tgtp->size, TGPM_FIELDS);
        printf("\n");
      }
    }

    DDS_DataReader_return_loan(rd, &mseq, &iseq);
    usleep(10000);
  }

  tgfree(tgtp);
  common_fini();
  return 0;
}
