# gcc build of the engine (native 32/64-bit), for OpenBench and fast local testing.
# The 16-bit DOS target is chess.exe built by build.ps1; this builds the same
# source with gcc so it runs on normal Windows/Linux hosts.
#
# Usage:
#   make                -> chess_gcc   (Linux) / chess_gcc.exe   (Windows)
#   make EXE=foo        -> foo         (Linux) / foo.exe         (Windows)
#   make CFLAGS='...'   -> override compile flags
#   make clean          -> remove objects and the binary

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

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET) chess_gcc.exe chess_gcc

.PHONY: all clean
