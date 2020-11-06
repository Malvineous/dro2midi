OBJS = dro2midi.o midiio.o
PROGS = dro2midi droshrink

-include config.mak

all: $(PROGS)

dro2midi: $(OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS)

droshrink: droshrink.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(PROGS) $(OBJS)

.PHONY: all clean
