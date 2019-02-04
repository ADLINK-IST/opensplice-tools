#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>

#include "common.h"
#include "testtype.h"

static int cmp_double (const void *va, const void *vb)
{
  const double *a = va;
  const double *b = vb;
  return (*a == *b) ? 0 : (*a < *b) ? -1 : 1;
}

int main (int argc, char **argv)
{
  DDS_Topic topic;
  JustSeqDataWriter writer;
  JustSeqDataReader reader;
  DDS_Publisher pub;
  DDS_Subscriber sub;
  JustSeq d;
  DDS_sequence_JustSeq *mseq = DDS_sequence_JustSeq__alloc ();
  DDS_SampleInfoSeq *iseq = DDS_SampleInfoSeq__alloc ();

  (void) argc;
  common_init (argv[0]);

  /* leaking QoS objects for simplicity */
  topic = new_topic_JustSeq ("Overhead", new_tqos ());
  pub = new_publisher1 (new_pubqos (), "");
  sub = new_subscriber1 (new_subqos (), "");
  writer = new_datawriter (new_wrqos (pub, topic));
  reader = new_datareader (new_rdqos (sub, topic));

  printf ("%7s WRITE %6s %6s %6s %6s | READ %6s %6s %6s %6s\n", "size", "min", "median", "99%", "max", "min", "median", "99%", "max");
  for (size_t size = 0; size <= 1048576; size = (size == 0) ? 1 : 2 * size)
  {
    memset (&d, 0, sizeof (d));
    d.baggage._buffer = size ? DDS_sequence_octet_allocbuf (size) : NULL;
    d.baggage._length = d.baggage._maximum = (uint32_t) size;
    d.baggage._release = 1;
    if (size)
      memset (d.baggage._buffer, 0, size);

    double twrite[1001]; /* in us */
    double tread[sizeof (twrite) / sizeof (twrite[0])];  /* in us */
    const int rounds = (int) (sizeof (twrite) / sizeof (*twrite));
    for (int k = 0; k < rounds; k++)
    {
      DDS_Time_t twrite1;
      DDS_DomainParticipant_get_current_time (dp, &twrite1);
      if (JustSeqDataWriter_write (writer, &d, DDS_HANDLE_NIL) != DDS_RETCODE_OK)
        abort ();
      DDS_Time_t tread1;
      DDS_DomainParticipant_get_current_time (dp, &tread1);
      if (JustSeqDataReader_read (reader, mseq, iseq, 1, DDS_ANY_SAMPLE_STATE, DDS_ANY_VIEW_STATE, DDS_ANY_INSTANCE_STATE) != DDS_RETCODE_OK)
        abort ();
      DDS_Time_t tdone;
      DDS_DomainParticipant_get_current_time (dp, &tdone);
      long long x = 1000000000;
      tread[k] = ((x * tdone.sec + tdone.nanosec) - (x * tread1.sec + tread1.nanosec)) / 1e3;
      twrite[k] = ((x * tread1.sec + tread1.nanosec) - (x * twrite1.sec + twrite1.nanosec)) / 1e3;
      JustSeqDataReader_return_loan (reader, mseq, iseq);
    }

    DDS_free (d.baggage._buffer);

    qsort (twrite, rounds, sizeof (*twrite), cmp_double);
    qsort (tread, rounds, sizeof (*tread), cmp_double);
    assert (rounds >= 100); /* for 99% case */
    printf ("%7zu %5s %6.0f %6.0f %6.0f %6.0f | %4s %6.0f %6.0f %6.0f %6.0f\n",
            size,
            "", twrite[0], twrite[rounds / 2], twrite[rounds - rounds / 100], twrite[rounds - 1],
            "", tread[0], tread[rounds / 2], tread[rounds - rounds / 100], tread[rounds - 1]);
  }

  common_fini ();
  return 0;
}
