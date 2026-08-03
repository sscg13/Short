# gcc build of the engine (native 32/64-bit), for OpenBench and fast local testing.
# The 16-bit DOS target is chess.exe built by build.ps1; this builds the same
# source with gcc so it runs on normal Windows/Linux hosts.
#
# Usage:
#   make                -> chess_gcc   (Linux) / chess_gcc.exe   (Windows)
#   make EXE=foo        -> foo         (Linux) / foo.exe         (Windows)
#   make CFLAGS='...'   -> override compile flags
#   make clean          -> remove objects and the binary

# The gcc build is a fast SCALAR oracle for the 16-bit target: OpenBench measures
# its nps to scale time controls, and NOTES.md requires node-count fidelity. GCC
# auto-vectorizes at -O2 (GCC >= 14 enables tree-loop/slp-vectorize), which would
# inflate nps ~4x and break the speed-fidelity story, so keep it strictly scalar.
CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -Werror
SRCS    := chess.c search.c xboard.c nnue.c
HDRS    := engine.h
OBJS    := $(SRCS:.c=.o)

TARGET  := $(if $(EXE),$(EXE),chess_gcc)

# Windows hosts append .exe; Linux does not.
ifeq ($(OS),Windows_NT)
  ifeq (,$(findstring .exe,$(TARGET)))
    TARGET := $(TARGET).exe
  endif
endif

all: $(TARGET)

# Build a profiling binary (`chess profile [depth]`) with the call counters on.
# Uses its own *_prof.o objects so the normal build's objects stay flag-free.
PROF_OBJS := chess_prof.o search_prof.o xboard_prof.o nnue_prof.o
profile: chess_prof.exe

chess_prof.exe: $(PROF_OBJS)
	$(CC) $(CFLAGS) -DPROFILE -o $@ $(PROF_OBJS)

chess_prof.o: chess.c $(HDRS)
	$(CC) $(CFLAGS) -DPROFILE -c -o $@ $<
search_prof.o: search.c $(HDRS)
	$(CC) $(CFLAGS) -DPROFILE -c -o $@ $<
xboard_prof.o: xboard.c $(HDRS)
	$(CC) $(CFLAGS) -DPROFILE -c -o $@ $<
nnue_prof.o: nnue.c $(HDRS)
	$(CC) $(CFLAGS) -DPROFILE -c -o $@ $<

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(PROF_OBJS) $(TARGET) chess_gcc.exe chess_gcc chess_prof.exe

.PHONY: all clean profile
