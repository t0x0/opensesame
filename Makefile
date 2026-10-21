# shellcheck disable=SC1009,SC1072,SC1073,SC2034  # this is a Makefile, not a shell script
libs=display.rel rf.rel keys.rel fbuffer.rel zsprites.rel pm.rel
simlibs=display.c.sim.rel rf.c.sim.rel keys.c.sim.rel
CC=sdcc
CFLAGS=#--no-pack-iram #--stack-auto
LFLAGS=--xram-loc 0xF000 --model-large
#LFLAGS=--xram-loc 0xF000 --model-small
SIMFLAGS=-DSIMULATOR

all: opensesame.hex #simulator.hex

%.rel : %.c
	$(CC) $(CFLAGS) $(LFLAGS) -c $<

%.c.sim.rel : %.c
	$(CC) $(SIMFLAGS) $(CFLAGS) $(LFLAGS) -c $< -o $@

simulator.hex: opensesame.c.sim.rel $(libs)
	$(CC) $(SIMFLAGS) $(LFLAGS) opensesame.c.sim.rel $(simlibs)
	packihx <opensesame.c.ihx >simulator.hex

opensesame.hex: opensesame.rel $(libs)
	$(CC) $(LFLAGS) opensesame.rel $(libs)
	packihx <opensesame.ihx >opensesame.hex

install: opensesame.hex
	goodfet.cc erase
	goodfet.cc flash opensesame.hex
verify: opensesame.hex
	goodfet.cc verify opensesame.hex
clean:
	rm -f *.hex *.ihx *.rel *.map *.lnk *.lst *.mem *.sym *.rst *.asm *.lk rf sim.out
sim:
	echo > sim.out
	echo "Run:"
	echo "tail -f sim.out"
	echo
	s51 -s sim.out simulator.hex

# host side check of the de bruijn / bitstream generator (no sdcc needed)
test:
	gcc -I. -O1 -w -fsanitize=address -o rf-test test_debruijn.c -lm
	./rf-test
	@rm -f rf-test
.PHONY: all sim test clean
