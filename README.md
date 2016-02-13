# Command-line tools for OpenSplice

This repository contains a few command-line utilities for OpenSplice. In principle, they are source compatible with OpenSplice V6 (and should be easily ported to V5 in a pinch) but the compatibility with old versions is not regularly checked and some minor issue may be encountered.

The primary tool is "pubsub", a generic reader-writer program with far too many command-line options. The "lsbuiltin" tool offers a simple way to get the contents of the built-in topics containing discovery information in a human-readable form (but you can get the same data using "pubsub", even if the formatting is a little bit harder on the eyes).

Then there is "idl2md" (and "simpleidlpp"). These scripts work together with the OpenSplice IDL preprocessor "idlpp" to easily construct input files for defining topics/types in "pubsub" that are not yet present in the system.

The "lsbuiltin" tool and "idl2md" script are simple enough to not require additional documentation, but "pubsub" is another story altogether.

## pubsub

The model of "pubsub" is to have one participant, with one publisher and one subscriber, and N readers and writers (though typically only 1 reader/writer). One can suppress readers and/or writers, and a publisher/subscriber is only created when needed.

A number of topic types are embedded in "pubsub", and for these topics it can automatically write data, check sequence numbers, measure throughput. For all topics, it offers the ability to write/dispose arbitrary data and the ability to print the received data with a configurable amount of meta-data.

### Synopsis

`pubsub` [_OPTIONS_] _PARTITION_...

The options control topics, QoS, input selection, printing modes, &c. The remaining arguments are interpreted as partition names to be used when creating the publisher and subscriber entities. Without options, it will use a standard topic, embedded type, standard QoS (though different from the DCPS default QoS), &c., to provide the simplest possible way to quickly read and write some data and verify basic system operation.

The partition names are subjected to environment variable expansion, with two environment variables automatically defined by `pubsub`:
* SYSTEMID is set to the OpenSplice system id (in decimal) for the federation that `pubsub` is in;
* NODE\_BUILTIN\_PARTITION is set to the node-specific built-in partition (\_\_NODE\_x\_BUILT-IN PARTITION\_\_) for the federation.

When invoked as `pub` (e.g., via a soft-link), the default reader mode specification is changed to `-m0` (see below), disabling the default creation of readers. Similarly, when invoked as `sub` the default writer mode is changed to `-w0` (again, see below), disabling the default creation of writers.

There are so many options that without doubt some interact in unfortunate ways, but a surprising number of them combine nicely.

### Options controlling topics/types

Topic and type to read/write are controlled by two options, `-T` and `-K`. Specifying a topic with `-T` when one has already been specified for the current reader/writer pair introduces a new reader/writer pair; similarly, specifying a type with `-K` when one has already been specified also introduces a new pair.

option       |  effect
-------------|------------
`-K` _TYPE_  |  selects a type to use, either one of the predefined ones, an arbitrary one from a lookup of the topic, or a file providing metadata.
`-T` _TOPIC_ | selects a topic name (and possibly sets a timeout for the find_topic operation and a content-filtered topic expression)

The _TYPE_ argument of `-K` is interpreted as follows:

_TYPE_  |  type name    | default topic name  | resulting type
--------|---------------|---------------------|---------------------
`KS`    |  `KeyedSeq`   |  `PubSub`           | key (32-bit int), sequence number (unsigned 32-bit int), octet sequence
`K32`   |  `Keyed32`    |  `PubSub32`         | key (32-bit int), sequence number (unsigned 32-bit int), octet array of length 32
`K64`   |  `Keyed64`    | `PubSub64`          | as K32, length 64
`K128`  |  `Keyed128`   | `PubSub128`         | as K32, length 128
`K256`  |  `Keyed256`   | `PubSub256`         | as K32, length 256
`OU`    |  `OneULong`   | `PubSubOU`          | sequence number only
`ARB`   |  n/a          | no default          | uses find_topic to look up type and topic QoS, no topic QoS override possible
_FILE_  |  n/a          | no default          | reads typename, keylist and type metadata from _FILE_ and defines a topic with the specified name

The default used depends on what has and hasn't been specified:

specified             |  default
----------------------|--------------
neither `-K` nor `-T` | type `KS` with topic name from above table
only `-K`             | typic name from above table (or an error for the `ARB` and _FILE_ variants)
only `-T`             | type `ARB`

The _TOPIC_ argument of the `-T` option has 3 components, of which the first is required and the others are optional, and which are separated by a colon: _TOPIC_[:_TO_][:_EXPR_]. Here _TOPIC_ is the DDS topic name, _TO_ is a time-out for find_topic (i.e., when using `-K ARB`), and _EXPR_ is an optional content filter. The timeout is specified in seconds (or the word "inf" for an infinite timeout) and has a default of 10s. Specifying a content filter causes a content-filtered topic with a name "cft_N_" (with _N_ an integer) to be defined.

The content-filtered topic expression is subjected to environment-variable expansion with the variables SYSTEMID and NODE\_BUILTIN\_PARTITION set automatically, just like the partition names.

### Options controlling QoS

QoS can be configured for the topics (other than those looked up via find_topic), subscriber, publisher, readers and writers, but not (yet) independently for the individual entities. QoS defaults are slightly different from those of DCPS, and the readers and writers inherit automatically from the topic QoS.

In ARB mode, the topic QoS is set by the existing topic's definition. In KS, K32, ..., K256, OU and _FILE_ modes (of the `-K` option), default topic QoS is the DCPS default except:

QoS setting       | value
------------------|-------------
reliability kind  | reliable
max blocking time | 1s
destination order | by source timestamp

Default subscriber and publisher QoS are the DCPS default , except that the partition is always explicitly set from the command-line arguments (and at least one must be given -- the default partition is specified by giving an empty string as partition name).

Default reader and writer QoS are determined by applying (in this order) the DCPS default, the topic QoS and the built-in default overrides of:

QoS setting                         | value
------------------------------------|-------------
history kind                        | keep all
resource limits                     | max_samples = 10000 (reader)
                                    | max_samples = 100 (writer)
auto-dispose unregistered instances | false

The `-q` option allows the specification of overrides applied after setting the defaults. It takes an argument in either of three forms: _FS_:_QOS_ to set QoS directly from the command-line, `provider=`[_PROFILE_,]_URI_ to select a QoS provider, or anything accepted by the selected QoS provider, if any.

Here, _FS_ is a set of one or more flags (list them without separators) indicating to which types of entities this QoS setting is to be applied:

flag | entity type
-----|---------------
`t`  | topic
`p`  | publisher
`s`  | subscriber
`w`  | writer
`r`  | reader
`a`  | all of the above

Specifying a QoS that is inapplicable to the entity type causes a warning, but is otherwise ignored. The _QOS_ field is then interpreted as a comma-separated list of QoS specifications, each of the form _Q_=_S_. In the below table, "{a,b}" means either of a and b, and "[a]" means a is optional:

_Q_ | _S_          | applicability | meaning
----|--------------|---------------|---------------
`A` | {a,p:_S_,w:_S_}  | t, r, w | set liveliness to: automatic, manual-per-participant, or manual-per-writer; _S_ is the lease duration
`d` | {v,tl,t,p} | t, r, w | set the durability kind: volatile, transient-local, transient, or persistent
`D` | _P_ | t, r, w | set deadline to _P_
`k` | {all,_N_} | t, r, w | set history kind to keep all, or to keep-last _N_
`l` | _D_ | r, w | set latency budget to _D_
`L` | _D_ | w | set lifespan to _D_
`o` | {r,s} | t, r, w | set destination order, by-reception or by-source.
`O` | {s,x[:_S_]} | t, r, w | set ownership to shared, or to exclusive with ownership strength _S_ (defaults to 0)
`p` | _P_ | w | set transport priority to _P_
`P` | {i,t,g} | p, s | set presentation QoS: by-instance, by-topic, or by-group; for by-instance, coherent_access is set to false, for by-topic and by-group, coherent_access is set to true; ordered access is always false
`r` | {y[:_T_],s[:_T_],n} | t, r, w | set reliability: reliable, reliable + synchronous, best-effort; _T_ is the max blocking time
`R` | _S_/_I_/_SpI_ | r, w | set resource limits: max samples, max instances, and max samples per instance
`S` | _C_[/_H_[/_S_/_I_/_SpI_]] | t | set durability service: cleanup delay _C_, history setting _H_ (same as `k` setting), resource limits (same as `R`)
`u` | {y,n} | w | set autodispose unregistered instances to true/false
`U` | _T_ | r, w | set user data to _T_, C-style escape-sequence processing is applied
`V` | _K0_[:_K1_[:...]] | r | set subscriber-defined keys to fields _K0_, _K1_, &c.

All durations are in seconds and may be floating-point numbers. Durations and limits also accept "inf" for infinite/unlimited.

If a QoS provider has been configured, then the third form is passed through to the QoS provider unchanged, except that an empty string is interpreted to mean the default value. Note that _FS_:_QOS_ form allows updating individual QoS settings, but that the QoS provider always sets all QoS settings for the entity. Hence, the QoS provider is really only useful for overriding the default QoS used by `pubsub`.

### Options controlling reader modes

Whether or not a reader (for a topic) is created, and in what mode it operates is primarily controlled by the `-m` option, which has various possible argument forms:

argument   | meaning
-----------|------------------
`0`        | no reader for the current topic specification
`p`        | print received data, reading/taking alive and not-alive-no-writers data and taking disposed data -- whether it reads or takes is controlled by the `-R` option
`pp`       | as `p`, except that it polls rather than waits for data to arrive and always takes data
`c`[:_N_]  | "check" mode, applicable only to KS, K32, ..., K256 modes. Takes all incoming samples on a data_available trigger, and prints the number of received samples, out-of-order samples and throughput, when triggered and when the previous line was output >= 1s ago. The out-of-order checking is based on expecting the topic's sequence number field to increment for each sample and the keys to cycle from 0 to _N_-1 (exactly corresponding to the -wN writer mode). _N_ is a global setting, the last one set wins, the default is 1.
`cp`[:_N_] | polling variant of the above
`d`        | uses simpler triggering than `p`, reads/takes everything and prints it
`dp`       | polling variant of the above, always takes data
`z`        | zero-load mode; creates a reader but does nothing with it; changes the (`pubsub`) default history setting to keep-last 1, but still allows the `-q` option to override it

In addition, there are the following options affecting behaviour:

option | argument | per-reader | meaning
-------|----------|------------|---------------
`-n`   | _N_      | yes        | limit read/take to _N_ samples
`-s`   | _T_      | yes        | sleep _T_ ms after each read/take (default: 0)
`-$`   |          | yes        | perform one final, unlimited take-all just before stopping in `p` and `d` modes
`-F`   |          | no         | explicitly set line-buffering mode, useful when piping the output into a script that filters and outputs the results
`-W`   | _T_      | no         | call wait_for_historical_data with timeout _T_
`-O`   |          | no         | "once" mode, triggers termination after reading data once immediately (though after having called wait_for_historical_data if requested) -- this is the same take that otherwise is enabled with `-$` as the "final take"; exist status is 0 if data was read, 1 if not

Then, there is a global `-P` option controlling what gets printed. The `-P` option takes a comma-separated list of keywords turning on/off various outputs. In the three tables below, those marked with "yes" in the second column can be inverted by prefixing them with "no":

keyword | "no" prefix? | meaning
--------|--------------|--------------
meta    | yes          | enable printing of all sample metadata
trad    | yes          | "traditional" (i.e., older `pubsub` versions) flags: pid, time, phandle, stime, state
pid     | yes          | process id of pubsub
topic   | yes          | which topic
time    | yes          | read time relative to program start
phandle | yes          | publication handle
ihandle | yes          | instance handle
stime   | yes          | source timestamp
rtime   | yes          | reception timestamp
dgen    | yes          | disposed generation count
nwgen   | yes          | no-writers generation count
ranks   | yes          | sample, generation, absolute generation ranks
state   | yes          | instance/sample/view states

The sample meta data is always printed in the above order, with spaces (and in some cases, space-colon-space) separating the fields. For `ARB` mode and _FILE_ modes of the `-K` option, it is possible to control whether or not the topic type is printed and as well as the formatting of the sample contents:

keyword   | "no" prefix? | meaning
----------|--------------|--------------
type      | no           | print type definition at start up
dense     | no           | no additional white space, no field names
fields    | no           | field names (C99-style designated initializers), some white space
multiline | no           | field names (C99-style designated initializers), one field per line

For the KS, K32, ... K256 modes, a sample is always printed as the sequence number followed by the key value (separated by a space); in the case of invalid samples (those that do not have the "valid_data" flag in the sample info set), it prints "NA" for the sequence number. For the OU mode, it is just the sequence number or "NA".

Finally, there are the ones that do not fit anywhere else:

keyword   | "no" prefix? | meaning
----------|--------------|--------------
finaltake | yes          | print a "final take" notice before the results of the optional final take (see `-$` option above) just before stopping.

The default is "nometa,state,fields,finaltake".

Each reader runs in a separate thread with its own waitsets and mode settings (except for some settings, such as the number of key values in checking mode). Output always goes to stdout and uses file locking to keep the lines separate.

### Options controlling writer modes

Analogous to the reader mode selection option, there is the `-w` writer mode selection option (`-w` is probably more mnemonic of "writer" than `-m` is of "reader", but hey, it grew over time). The `-w` option argument is interpreted as follows:

argument     | auto | meaning
-------------|------|----------
`0`          | n/a  | no writer for the current topic specification
_N_          | yes  | cycle through _N_ key values as fast as possible, incrementing the sequence number for each sample (see also `-m` option)
_N_:_R_      | yes  | as _N_ but at a rate of _R_ samples/second (rate in floating-point)
_N_:_R_\*_B_ | yes  | as above, but writing bursts of _B_ samples at a rate of _R_ bursts/second (rate in floating-point)
`-`          | no   | (a hyphen) read from stdin, the default
:_P_         | no   | listen on port _P_ for a TCP connection, then read from that connection until closed and repeat
_H_:_P_      | no   | establish a TCP connection to host _H_, port _P_ and read from it

Each writer in "automatic" mode (the "auto" column above) runs in a separate thread until `pubsub` termination is triggered, either by end-of-input on a non-automatic writer or by reaching the time limit set by `-D`. Note that the TCP-server mode is an odd one as it automatically repeats when the client drops the connection, rather than triggering termination.

The non-automatic modes all read from the same input and in a single thread. Only the last input specification given is actually used. If invoked as `pubsub` or `pub` the default writer mode is `-` (read from stdin), hence specifying a different input source once changes all (non-auto) writers to use the alternative one. However, if invoked as `sub` writers are only created if explicitly requested, and in that case it can be convenient to specify mode `-` for the first writers that you do want created, and the actual input source for the final writer that you do want created.

The automatic modes are available only for the KS, K32, ..., K256 and OU types, and for the OU type, the actual value of the _N_ argument is irrelevant as long as it is positive (as it doesn't have a key). The non-auto ones are available for all types. The automatic modes report once per 4s how many samples they have written, plus an ASCII-art single-line histogram showing the distribution of the time it takes to perform the write operation.

Behaviour can be further configured with the following options:

option | argument | applicability | meaning
-------|----------|---------------|---------------
`-D`   | _D_      | auto          | run for _D_ seconds (_D_ may be floating-point); this option affects all automatic writers
`-r`   |          | auto          | pre-register instances, than write using the instance handles
`-z`   | _N_      | auto KS       | set the size of the octet sequence to _N_-12 bytes in the KS mode (12 bytes is occupied by key, sequence number and sequence length, so this gives an _N_-byte sample)
`-@`   |          | non-auto      | write an exact copy of everything on a duplicate writer

### Writer input format

In principle, the input is interpreted as a white-space separated sequence of commands. This is strictly the case for the KS, K32, ..., K256 and OU modes, and mostly the case for arbitrary-type mode. In the latter mode, command-processing is line-based and some commands take the remainder of the line.

The commands and their arguments are:

command | argument  | meaning
--------|-----------|--------------
_V_     |           | synonym for the `w` command
`w`     | _V_       | write sample with value _V_
`d`     | _V_       | dispose _V_
`D`     | _V_       | write-dispose _V_
`u`     | _V_       | unregister _V_
`r`     | _V_       | register _V_
`s`     | _N_       | sleep for _N_ seconds (_N_ an integer)
`z`     | _N_       | set sequence size to _N_-12 (KS only, see also `-z` option)
`p`     | _PS_      | set publisher partitions to the comma-separated list _PS_ (note that QoS changes are not supported by the DDSI2 service at the time of writing)
`Y`     |           | invoke the "dispose_all" operation on the topic
`B`     |           | begin coherent changes
`E`     |           | end coherent changes
`W`     |           | wait_for_acknowledgements with timeout set to infinite
`S`     | _P_;_T_;_U_ | make a persistent snapshot for partition expression _P_, topic expression _T_, and destination URI _U_
`:`     | _N_       | select the non-auto writer number _N_ (0-based), if _N_ has a sign, move towards the right (+) or the left (-) _N_-th non-auto writer
`:`     | _NAME_    | select the unique writer of which the topic name starts with _NAME_

The value _V_ is a single integer in non-arbitrary-type modes, a C99-style (designated) initializer in arbitrary-type modes. Typically that means the value starts with a '{' in arbitrary-type mode, as typically the topic type is a struct.

The initializers in arbitrary-type mode are mostly, but not exactly C99 (designated) initializers. The most important difference is that IDL unions are discriminated, and in `pubsub` the base format for union initialization is the discriminant value followed by a colon, followed by the value. However, if the value is specified with a member name, the discriminant value will be set automatically.

The write/dispose/&c. commands can optionally specify the seconds component of the source timestamp by appending an `@` to the command letter, e.g., `w@`_T_. If the timestamp starts with a `=`, it is an absolute timestamp, otherwise relative to the current time. So `w@-3 24` in non-arbitrary-type mode writes key value 24 with a timestamp 3s in the past, and `w@=10 314159` would write with a timestamp of 10.0s since the epoch of 1 Jan 1970.

In arbitrary-type mode, the `p`, `S` and `:` commands interpret the remainder of the line, for all other commands, interpretation continues on the same line. In non-arbitrary type modes, the argument always ends at the first white-space character, and hence in non-arbitrary type modes, the possible partition names, URIs, &c. are limited.

### Miscellaneous options:

option | argument | meaning
-------|----------|-----------------
`-M`   | _T_:_U_  | wait at most _T_ seconds for a each writer to detect a matching reader with user data _U_ and not owned by this instance of pubsub; terminates on time-out
`-S`   | _ES_     | set listeners for the events in _ES_ (see below)
`-*`   | _N_      | sleep for _N_ seconds just before returning from main(), after deleting all entities
`-!`   |          | disable built-in signal handlers for the INT and TERM signals that trigger graceful termination of pubsub, just like end-of-file does for non-automatic writers

The events for which listeners can be set with the `-S` option are specified as a comma-separated list of keywords, either the abbreviated form or the full form:

abbreviated form | full form
-----------------|-------------------
`pr`             | `pre-read`
`sl`             | `sample-lost`
`sr`             | `sample-rejected`
`lc`             | `liveliness-changed`
`sm`             | `subscription-matched`
`riq`            | `requested-incompatible-qos`
`rdm`            | `requested-deadline-missed`
`ll`             | `liveliness-lost`
`pm`             | `publication-matched`
`oiq`            | `offered-incompatible-qos`
`odm`            | `offered-deadline-missed`

The `pre-read` event is not a listener, just a line printed when a pubsub waitset has been triggered and data is about to be read. The listeners all print decoded information, which for the "incompatible QoS" listeners includes the names of the incompatible QoS.

### Examples

* `pubsub test` creates a reader and a writer in partition "test" with standard QoS (see below for the actual QoS used), for type `KS` and with a topic name of `PubSub`.
* `pubsub -KOU -Tfoo bar baz` creates a topic "foo" for the OU type in the partitions bar and baz
* `pubsub -TDCPSHeartbeat:'id.systemId<>$SYSTEMID' '__BUILT-IN PARTITION__'` looks up the "DCPSHeartbeat" topic (which is an OpenSplice built-in topic and hence always present), derives a content-filtered topic that selects only those for which the systemId differs from that of PubSub itself and creates a reader and a writer with the topic QoS in the "\_\_BUILT-IN PARTITION\_\_" partition. Each node joining/leaving the network will be printed.
* `pubsub -KKS -TA -KKS -TA ""` will create two `KS` readers/writers both for topic A and A in the default partition, and consequently any data written will be received (and printed) by both readers.
* `pubsub -qrw:k=1 test` uses topic "PubSub" of typed "KeyedSeq" (KS), by-source ordering, reliability kind reliable, &c., but with a keep-last 1 history rather than the (`pubsub`) default of keep-all.
* `echo '{.deaf_mute=true, .duration=30}' | pubsub -m0 -qps:P=t -Tq_ddsiControl '$NODE_BUILTIN_PARTITION'` creates a by-topic coherent publisher with a writer for the q_ddsiControl topic (pre-defined by DDSI2 if the control interface is enabled) in the node-local built-in partition, and publishes a command to make DDSI2 deaf-mute for the next 30s.
* `pubsub -w0 -mcp:1 -z1024 test` and `pubsub -w1 -r -m0 test` give a simple throughput test with 1kB-byte large samples of the KeyedSeq type (with reliable delivery and by-source timestamp ordering)
* `pubsub -w0 -K<(./idl2md ShapeType.idl ShapeType) -TCircle -Ptype,multiline -Ssm,riq -qrw:o=r,r=n ''` defines the Circle topic using the type and key definition from the output of the "idl2md" script and creates a best-effort, order-by-reception-timestamp reader in the default partition. It prints the type definition, it prints the samples one field per line, and it has listeners set that show when matching writers come and go, and when incompatible writers are detected.
* `pubsub -qps:P=g -qt:d=p -w:12345 -KKS -TA -KKS -TB -Pmeta barf` and `echo B 1 2 :B 3 4 E | nc localhost 12345` uses "netcat" to pipe a group transaction containing two samples of topic A and two samples of topic B into pubsub for publication; and has pubsub printing these with full sample meta-data. (The readers are independent in pubsub, so the output is not necessarily a coherent set.)
* A simple persistently-written RnR scenario:

````
echo '{.scenarioName="xyzzy", .rnrId="rnr", .kind=ADD_RECORD_COMMAND:{ .storage="st", .interestExpr
={"*.PubSub"} }}
      {.scenarioName="BuiltinScenario", .rnrId="rnr", .kind=START_SCENARIO_COMMAND:"xyzzy"}' | \
    ./pub -qw:d=p -Trr_scenario RecordAndReplay

(sleep 1 ; for x in 1 2 3 4 5 6 ; do echo $x ; sleep 1 ; done ; sleep 1) | ./pubsub test

echo '{.scenarioName="BuiltinScenario", .rnrId="rnr", .kind=STOP_SCENARIO_COMMAND:"xyzzy"}
      d{.scenarioName="xyzzy", .rnrId="rnr"}' | \
    ./pub -qw:d=p -Trr_scenario RecordAndReplay
````
