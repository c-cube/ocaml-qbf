

all:
	cd picosat-535/ ; ./configure ; make
	cd quantor-3.0/ ; ./configure --picosat=../picosat-535 ; make

clean:
	make -C picosat-535/ clean
	make -C quantor-3.0/ clean

.PHONY: all clean
