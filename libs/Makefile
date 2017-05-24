

all: picosat-960/config.h quantor-3.2/config.h
	cd picosat-960/ ; make
	cd quantor-3.2/ ; make

# We use the flag -fPIC so that a shared stub can be created on linux.
# Notes:
# - on mingw, -fPIC is ignored because all objects are created with position
#   independent code. So this does not affect the windows build.
# - on macos, -fPIC is also ignored because in order to compile a position
#   independent code, the (clang) argument is -fno-common -DPIC. But
#   it seems to be working fine on macos without these flags (-fPIC is ignored).
quantor-3.2/config.h: picosat-960/config.h
	cd $(dir $@); CFLAGS='-DNDEBUG -O3 -fPIC' ./configure --picosat=../picosat-960
picosat-960/config.h:
	cd $(dir $@); CFLAGS='-DNDEBUG -O3 -fPIC' ./configure

clean:
	make -C picosat-960/ clean
	make -C quantor-3.2/ clean

.PHONY: all clean
