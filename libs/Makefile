

all:
	cd picosat-960/ ; ./configure ; make
	cd quantor-3.2/ ; ./configure --picosat=../picosat-960 ; make

clean:
	make -C picosat-960/ clean
	make -C quantor-3.2/ clean

.PHONY: all clean
