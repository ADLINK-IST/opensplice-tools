#ifndef COMMON_H
#define COMMON_H

#include "dds_dcps.h"

extern DDS_DomainParticipantFactory dpf;
extern DDS_DomainParticipant dp;
extern DDS_TypeSupport ts_KeyedSeq;
extern DDS_TypeSupport ts_Keyed32;
extern DDS_TypeSupport ts_Keyed64;
extern DDS_TypeSupport ts_Keyed128;
extern DDS_TypeSupport ts_Keyed256;
extern DDS_TypeSupport ts_OneULong;
extern const char *saved_argv0;
extern const char *qos_arg_usagestr;

struct qos;

#define BINS_LENGTH (8 * sizeof (unsigned long long) + 1)

unsigned long long nowll (void);
void nowll_as_ddstime (DDS_Time_t *t);

void bindelta (unsigned long long *bins, unsigned long long d, unsigned repeat);
void binprint (unsigned long long *bins, unsigned long long telapsed);

struct hist;
struct hist *hist_new (unsigned nbins, uint64_t binwidth, uint64_t bin0);
void hist_free (struct hist *h);
void hist_reset_minmax (struct hist *h);
void hist_reset (struct hist *h);
void hist_record (struct hist *h, uint64_t x, unsigned weight);
void hist_print (struct hist *h, uint64_t dt, int reset);

void save_argv0 (const char *argv0);
const char *dds_strerror (DDS_ReturnCode_t code);
void error (const char *fmt, ...);
int common_init (const char *argv0);
void common_fini (void);
DDS_ReturnCode_t set_PartitionQosPolicy (DDS_PartitionQosPolicy *qp, unsigned npartitions, const char *partitions[]);
DDS_ReturnCode_t change_publisher_partitions (DDS_Publisher pub, unsigned npartitions, const char *partitions[]);
DDS_ReturnCode_t change_subscriber_partitions (DDS_Subscriber pub, unsigned npartitions, const char *partitions[]);
DDS_Publisher new_publisher (const struct qos *a, unsigned npartitions, const char **partitions);
DDS_Subscriber new_subscriber (const struct qos *a, unsigned npartitions, const char **partitions);
DDS_Publisher new_publisher1 (const struct qos *a, const char *partition);
DDS_Subscriber new_subscriber1 (const struct qos *a, const char *partition);
struct qos *new_tqos (void);
struct qos *new_pubqos (void);
struct qos *new_subqos (void);
struct qos *new_rdqos (DDS_Subscriber s, DDS_Topic t);
struct qos *new_wrqos (DDS_Publisher p, DDS_Topic t);
void free_qos (struct qos *a);
void set_infinite_dds_duration (DDS_Duration_t *dd);
int double_to_dds_duration (DDS_Duration_t *dd, double d);
DDS_TypeSupport register_type (const char *name);
DDS_Topic new_topic (const char *name, DDS_TypeSupport typesup, const struct qos *a);
DDS_Topic new_topic_KeyedSeq (const char *name, const struct qos *a);
DDS_Topic new_topic_Keyed32 (const char *name, const struct qos *a);
DDS_Topic new_topic_Keyed64 (const char *name, const struct qos *a);
DDS_Topic new_topic_Keyed128 (const char *name, const struct qos *a);
DDS_Topic new_topic_Keyed256 (const char *name, const struct qos *a);
DDS_Topic new_topic_OneULong (const char *name, const struct qos *a);
DDS_DataWriter new_datawriter (const struct qos *a);
DDS_DataReader new_datareader (const struct qos *a);
DDS_DataWriter new_datawriter_listener (const struct qos *a, const struct DDS_DataWriterListener *l, DDS_StatusMask mask);
DDS_DataReader new_datareader_listener (const struct qos *a, const struct DDS_DataReaderListener *l, DDS_StatusMask mask);
const DDS_DataWriterQos *qos_datawriter(const struct qos *a);
void qos_liveliness (struct qos *a, const char *arg);
void qos_deadline (struct qos *a, const char *arg);
void qos_durability (struct qos *a, const char *arg);
void qos_history (struct qos *a, const char *arg);
void qos_destination_order (struct qos *a, const char *arg);
void qos_ownership (struct qos *a, const char *arg);
void qos_transport_priority (struct qos *a, const char *arg);
void qos_reliability (struct qos *a, const char *arg);
void qos_resource_limits (struct qos *a, const char *arg);
void qos_user_data (struct qos *a, const char *arg);
void qos_latency_budget (struct qos *a, const char *arg);
void qos_lifespan (struct qos *a, const char *arg);
void qos_autodispose_unregistered_instances (struct qos *a, const char *arg);
void qos_subscription_keys (struct qos *a, const char *arg);
void qos_presentation (struct qos *a, const char *arg);
void qos_durability_service (struct qos *a, const char *arg);
void qos_autopurge_disposed_samples_delay (struct qos *a, const char *arg);
void qos_autoenable (struct qos *a, const char *arg);
void set_qosprovider (const char *arg);
void setqos_from_args (struct qos *q, int n, const char *args[]);

#endif
