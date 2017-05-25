PICO=picosat-960
QUANT=quantor-3.2

all: $(PICO)/libpicosat.a $(QUANT)/libquantor.a

# We use the flag -fPIC so that a shared stub can be created on linux.
# Notes:
# - on mingw, -fPIC is ignored because all objects are created with position
#   independent code. So this does not affect the windows build.
# - on macos, -fPIC is also ignored because in order to compile a position
#   independent code, the (clang) argument is -fno-common -DPIC. But
#   it seems to be working fine on macos without these flags (-fPIC is ignored).
$(QUANT)/config.h: $(PICO)/libpicosat.a
	cd $(dir $@); CFLAGS='-DNDEBUG -O3 -fPIC' ./configure --picosat=../$(PICO)
$(PICO)/config.h:
	cd $(dir $@); CFLAGS='-DNDEBUG -O3 -fPIC' ./configure

$(PICO)/libpicosat.a: $(PICO)/config.h
	cd $(dir $@); make; ranlib libpicosat.a
$(QUANT)/libquantor.a: $(QUANT)/config.h
	cd $(dir $@); make; ranlib libquantor.a

clean:
	make -C $(PICO) clean
	make -C $(QUANT) clean

.PHONY: all clean
