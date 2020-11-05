OBJS = dro2midi.o midiio.o
PROG = dro2midi

-include config.mak

$(PROG): $(OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS)

all: $(PROG)

clean:
	rm -f $(PROG) $(OBJS)

.PHONY: all clean
