

all: picosat-960/config.h quantor-3.2/config.h
	cd picosat-960/ ; make
	cd quantor-3.2/ ; make

quantor-3.2/config.h:
	cd $(dir $@); ./configure --picosat=../picosat-960
picosat-960/config.h:
	cd $(dir $@); ./configure

clean:
	make -C picosat-960/ clean
	make -C quantor-3.2/ clean

.PHONY: all clean
