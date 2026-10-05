# Desktop simulator for the Gluco-Monitor firmware.
#   make          build ./build/gluco-sim
#   make run      build and open the simulator window
#   make reset    forget saved settings (language, WiFi, account, ...)
#
# Needs SDL2 (brew install sdl2) and a prior `pio run` so PlatformIO has fetched the libraries.

ROOT    := $(abspath ..)
SRC     := $(ROOT)/src
LIBDEPS := $(ROOT)/.pio/libdeps/esp32-s3-devkitc-1
CORE    := $(HOME)/.platformio/packages/framework-arduinoespressif32/cores/esp32
BUILD   := build

# The GFX library lives in a directory with spaces in its name; link it to keep make happy.
LIB := $(BUILD)/lib
GFX := $(LIB)/gfx
U8G := $(LIB)/u8g2
# Core sources are linked individually so their quoted includes don't find the real Arduino.h.
$(shell test -d "$(LIBDEPS)/GFX Library for Arduino/src" && mkdir -p $(LIB)/core && \
  ln -sfn "$(LIBDEPS)/GFX Library for Arduino/src" $(GFX) && ln -sfn "$(LIBDEPS)/U8g2/src" $(U8G) && \
  ln -sf $(CORE)/WString.cpp $(CORE)/Print.cpp $(LIB)/core/)

FW_SRCS   := $(SRC)/main.cpp $(SRC)/Config.cpp $(SRC)/Heure.cpp $(SRC)/Serie.cpp $(SRC)/Stock.cpp \
             $(SRC)/Internet.cpp $(SRC)/Langues/Langue.cpp $(wildcard $(SRC)/Ecran/*.cpp)
GFX_SRCS  := $(GFX)/Arduino_G.cpp $(GFX)/Arduino_GFX.cpp $(GFX)/Arduino_DataBus.cpp $(GFX)/canvas/Arduino_Canvas.cpp
CORE_SRCS := $(LIB)/core/WString.cpp $(LIB)/core/Print.cpp
SIM_SRCS  := sim_runtime.cpp sim_data.cpp
CXX_SRCS  := $(FW_SRCS) $(GFX_SRCS) $(CORE_SRCS) $(SIM_SRCS)
C_SRCS    := $(U8G)/clib/u8g2_fonts.c

objname = $(BUILD)/obj/$(subst /,_,$(patsubst /%,%,$(1))).o
OBJS := $(foreach f,$(CXX_SRCS) $(C_SRCS),$(call objname,$(f)))

SDL_CFLAGS := $(shell sdl2-config --cflags)
SDL_LIBS   := $(shell sdl2-config --libs)

INCS := -Ishim -I$(SRC) -I$(GFX) -I$(U8G) -I$(LIBDEPS)/ArduinoJson/src -I$(CORE)
DEFS := -DARDUINOJSON_ENABLE_ARDUINO_STRING=1 -DPROG_VERSION=\"sim\" -DBUILD_DATE=\"sim\" -DVERSION=sim
CXXFLAGS := -MMD -MP -std=gnu++17 -O1 -g -w $(DEFS) $(INCS) $(SDL_CFLAGS)
CFLAGS   := -MMD -MP -O1 -g -w $(INCS)

all: $(BUILD)/gluco-sim

-include $(OBJS:.o=.d)   # rebuild when a header (e.g. a language file) changes

$(BUILD)/gluco-sim: $(OBJS)
	$(CXX) -o $@ $^ $(SDL_LIBS)

define cxx_rule
$(call objname,$(1)): $(1)
	@mkdir -p $$(dir $$@)
	$$(CXX) $$(CXXFLAGS) -c $$< -o $$@
endef
define c_rule
$(call objname,$(1)): $(1)
	@mkdir -p $$(dir $$@)
	$$(CC) $$(CFLAGS) -c $$< -o $$@
endef
$(foreach f,$(CXX_SRCS),$(eval $(call cxx_rule,$(f))))
$(foreach f,$(C_SRCS),$(eval $(call c_rule,$(f))))

run: all
	./$(BUILD)/gluco-sim

reset:
	rm -rf data

clean:
	rm -rf $(BUILD)

.PHONY: all run reset clean
