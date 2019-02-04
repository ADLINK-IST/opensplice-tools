.NOTPARALLEL:

ifeq "$(filter second-expansion, $(.FEATURES))" ""
  $(error Secondary expansion required, which is available in gnu make 3.81 and later)
endif

# Target operating system and processor matter: we pick the compiler
# based on the operating system and we set the 32/64-bit flags for
# x86/x86_64.
ifneq "$(SPLICE_TARGET)" ""
  OS_PROC := $(SPLICE_TARGET)
else
  OS_PROC := $(notdir $(OSPL_HOME))
endif
OS := $(shell echo $(OS_PROC) | sed -e 's/^[^.]*\.//' -e 's/^\([^-_]*\)[-_].*/\1/')
PROC := $(shell echo $(OS_PROC) | sed -e 's/^\([^.]*\)\..*/\1/')

# Special-case: solaris11 => solaris10 -- should check OSREV
ifeq "$(OS)" "solaris11"
  OS = solaris10
endif

# On solaris, use Sun's C compiler; on Linux and Mac just use gcc. It
# might be worthwhile to consider adding a file with the compiler
# target settings used to the OpenSplice distribution ...
ifneq "$(filter solaris%, $(OS))" ""
  CC = cc
  OPT = -fast
  CFLAGS = -g -v $(OPT)
  DEPFLAG = -xM1
else
  ifneq "$(filter darwin%, $(OS))" ""
    CC = clang
    OPT = #-O
    CFLAGS = -g -Wall $(OPT)
    LDFLAGS += -g
    DEPFLAG = -M
    ifneq "$(USE_SANITIZER)" ""
      CFLAGS  += -fsanitize=$(USE_SANITIZER)
      LDFLAGS += -fsanitize=$(USE_SANITIZER)
    endif
  else
    ifeq "$(PROC).$(OS)" "E500mc.linux"
      CC = powerpc-fsl-linux-gcc
      OPT = -O
      SYSROOT = /opt/fsl-networking/QorIQ-SDK-V1.6/sysroots/ppce500mc-fsl-linux
      CPPFLAGS += --sysroot=$(SYSROOT)
      CFLAGS = -std=c99 -mcpu=e500mc -mtune=e500mc --sysroot=$(SYSROOT) -g -Wall $(OPT)
      DEPFLAG = -M
      LDFLAGS += -g --sysroot=$(SYSROOT)
    else
      ifneq "$(filter CYGWIN%, $(shell uname -s))" ""
        OS = linux
        X = .exe
      endif
      CC = gcc -std=gnu99
      OPT = -O
      CFLAGS = -g -Wall $(OPT)
      DEPFLAG = -M
    endif
  endif
endif
# Solaris and Linux seem to default to 32-bit code, Mac defaults to
# 64-bit code. So just set it to whatever the OpenSplice was built
# with regardless of the current host's default.
ifeq "$(PROC)" "x86"
  CFLAGS += -m32
  LDFLAGS += -m32
  ifneq "$(filter linux, $(OS))" ""
    CPPFLAGS += -march=i686
    LDFLAGS += -rdynamic
  endif
endif
ifeq "$(PROC)" "x86_64"
  CFLAGS += -m64
  LDFLAGS += -m64
endif
ifneq "$(filter linux%, $(OS))" ""
  CPPFLAGS += -D_GNU_SOURCE
  LDFLAGS += -rdynamic
endif

# If $(SPLICE_TARGET) is set, assume we're building against a source tree
ifneq "$(SPLICE_TARGET)" ""
  OSPL_INTERNAL_INC = api/dcps/gapi/include api/dcps/gapi/code user/include user/code abstraction/os-net/include abstraction/os-net/$(OS) abstraction/pa/include
  OSPLINC = $(addprefix src/, api/dcps/sac/bld/$(SPLICE_TARGET) api/dcps/sac/include database/database/include database/serialization/include kernel/include osplcore/bld/$(SPLICE_TARGET) kernel/bld/$(SPLICE_TARGET) abstraction/os/include abstraction/os/$(OS) $(OSPL_INTERNAL_INC))
else
  OSPLINC = include/dcps/C/SAC include/sys
endif

# Expect to be invoked either as:
#   "make <targets>"
# or as
#   "make -f .../makefile <targets>"
# so: the first word in $(MAKEFILE_LIST) will be the top-level
# makefile and therefore the $(dir) function will give us the relative
# directory. If it is "./" assume we're building inside the source dir
# and don't need to fiddle with the vpaths, otherwise ./ assume we're
# outside the source dir and set the vpath to point to the sources
SRCDIR := $(dir $(firstword $(MAKEFILE_LIST)))
ifneq "$(SRCDIR)" "./"
  vpath %.c $(SRCDIR)
  vpath %.h $(SRCDIR)
endif

ifneq "$(OSPL_OUTER_HOME)" ""
  CPPFLAGS += $(OSPLINC:%=-I$(OSPL_OUTER_HOME)/%)
endif
CPPFLAGS += $(OSPLINC:%=-I$(OSPL_HOME)/%)
LDFLAGS += -L$(OSPL_HOME)/lib/$(SPLICE_TARGET)
LDLIBS = -ldcpssac -lddskernel

# Solaris needs a few more libraries
ifneq "$(filter solaris%, $(OS))" ""
  LDLIBS += -lsocket -lnsl -lm -lrt
endif
ifneq "$(filter linux%, $(OS))" ""
  LDLIBS += -lm -lrt -ldl -lpthread
endif
ifneq "$(filter darwin%, $(OS))" ""
  LDLIBS += -ledit -ltermcap
  comma=,
  LDFLAGS := $(LDFLAGS) $(patsubst -L%, -Wl$(comma)-rpath %, $(filter -L%, $(LDFLAGS)))
endif

# Target executables, each may have a bunch of IDL files ...
TARGETS = pubsub$X lsbuiltin$X pingpong$X
TARGETS += overheadtest$X
IDL_common := testtype
# ... and those really required per target ...
IDL_pubsub := testtype ddsicontrol
IDL_pingpong := testtype
IDL_fanout := testtype
IDL_manysamples := testtype
IDL_manyendpoints := testtype
IDL_txnid_test := testtype
IDL_overheadtest := testtype
# ... and the set of all IDL files ($(sort) removes duplicates)
IDLMODS := $(sort $(foreach x, common $(TARGETS), $(IDL_$(subst -,_,$x))))
ifneq "$(OSPL_MAJOR)" "" # assume source tree in which case $OSPL_HOME/etc/idl may not have the req files 
  PRE_V6_5 := $(shell [ `expr 100 \* $(OSPL_MAJOR) + $(OSPL_MINOR)` -lt 605 ] && echo yes)
else
  ifneq "$(wildcard $(OSPL_HOME)/release.com)" ""
    MAJOR_MINOR := $(shell sed -n -e '/OpenSplice HDE Release/s/.*Release \(V\|\)\([0-9][0-9]*\)\.\([0-9][0-9]*\).*/\2 \3/p' $(OSPL_HOME)/release.com)
    PRE_V6_5 := $(shell [ `expr 100 \* $(word 1, $(MAJOR_MINOR)) + $(word 2, $(MAJOR_MINOR))` -lt 605 ] && echo yes)
  endif
endif
ifeq "$(PRE_V6_5)" "yes"
  IDLPPFLAGS += -odds-types
  CPPFLAGS += -DPRE_V6_5
else
  IDLPPFLAGS += -I$(OSPL_HOME)/etc/idl -DINCLUDE_DDS_DCPS
endif

IDLPP := $(shell which idlpp$X)

ECHO_PREFIX=@

.PHONY: all clean zz dd
.SECONDARY:

.SECONDEXPANSION:
%: %.o
%: %.c

%.h %Dcps.h %SacDcps.c %SacDcps.h %SplDcps.c %SplDcps.h: %.idl $(IDLPP)
	$(ECHO_PREFIX)$(IDLPP) -S -lc $(IDLPPFLAGS) $<

%.o: %.c
	$(ECHO_PREFIX)$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ -c $<


%$X: %.o $$(foreach x, SacDcps.o SplDcps.o, \
		$$(sort $$(addsuffix $$x, $$(IDL_$$@) $$(if $$(filter common.o, $$^), $(IDL_common)))))
	$(ECHO_PREFIX)$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

all: $(TARGETS)

pubsub$X: tglib.o common.o porting.o
fanout$X: common.o porting.o
manysamples$X: common.o porting.o
manyendpoints$X: common.o porting.o
txnid-test$X: common.o porting.o
genreader$X: tglib.o common.o
pingpong$X: common.o porting.o
overheadtest$X: common.o

# A little bit of a simplification: assume preprocessing foo.c needs
# the output of the IDL preprocessor files of the IDL files listed in
# IDL_foo. This works only for the sources of which the
# name-sans-sufffix is one of the targets, but for such simple
# programs as these, that works just fine.
%.d: %.c $(addsuffix .h, $(IDLMODS)) $(addsuffix SacDcps.h, $(IDLMODS)) $(addsuffix SplDcps.h, $(IDLMODS))
	$(ECHO_PREFIX)$(CC) $(DEPFLAG) $(CPPFLAGS) $< | sed 's/^ *\($*\.o\) *:/\1 $@:/' > $@ || rm -f $@ ; exit 1

clean:
	rm -f *.[od] $(TARGETS) $(foreach x, $(IDLMODS), $(x).h $(x)Dcps.h $(x)SacDcps.[ch] $(x)SplDcps.[ch])

cleanexe:
	rm -f $(TARGETS)

zz:
	@echo MAKEFILE_LIST = $(MAKEFILE_LIST)
	@echo SRCDIR = $(SRCDIR)
	@echo IDLMODS = $(IDLMODS)
	@echo OS = $(OS)
	@echo PRE_V6_5 = $(PRE_V6_5)
	@echo IDLPPFLAGS = $(IDLPPFLAGS)

ifneq ($(MAKECMDGOALS),zz)
  ifneq ($(MAKECMDGOALS),clean)
    -include $(TARGETS:%=%.d) $(foreach x, $(IDLMODS), $(x).d $(x)SacDcps.d $(x)SplDcps.d) common.d
  endif
endif
