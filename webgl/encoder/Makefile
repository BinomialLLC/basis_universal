
SOURCES := basisu_backend.cpp \
    basisu_basis_file.cpp \
    basisu_comp.cpp \
    basisu_enc.cpp \
    basisu_etc.cpp \
    basisu_frontend.cpp \
    basisu_global_selector_palette_helpers.cpp \
    basisu_gpu_texture.cpp \
    basisu_pvrtc1_4.cpp \
    basisu_resampler.cpp \
    basisu_resample_filters.cpp \
    basisu_ssim.cpp \
    basisu_tool.cpp \
    basisu_astc_decomp.cpp \
    lodepng.cpp \
    transcoder/basisu_transcoder.cpp

INTERMEDIATES_PATH=intermediates/
TARGET=basisu.js

OBJECTS=$(addprefix $(INTERMEDIATES_PATH), $(SOURCES:%.cpp=%.o))
OBJECTS+=$(INTERMEDIATES_PATH)basisu_wrappers.o

# compiler setup
#CC=clang++
CC=em++
HEADER_INCLUDES=-I../.. -I../../transcoder
RELEASE_FLAGS=-O3 -flto
DEBUG_FLAGS=-DDEBUG -DBASISU_DEVEL_MESSAGES=1 -s ASSERTIONS=1
COMMON_FLAGS=$(RELEASE_FLAGS) -s EXPORT_NAME=basisu -s TOTAL_MEMORY=67108864 --bind -s INVOKE_RUN=0 -s BINARYEN_TRAP_MODE='clamp' -s WASM=1 -s MODULARIZE=1
CFLAGS=$(COMMON_FLAGS) $(HEADER_INCLUDES) -std=c++11 -fvisibility=hidden -fvisibility-inlines-hidden -fPIC -fno-strict-aliasing -D_LARGEFILE64_SOURCE=1 -D_FILE_OFFSET_BITS=64 -Wall -Wextra -Wno-unused-local-typedefs -Wno-unused-value -Wno-unused-parameter -Wno-unused-variable -Wno-reorder
LINK_FLAGS=$(COMMON_FLAGS) -lm -lpthread

build: $(TARGET)

clean:
	rm -f $(OBJECTS)
	@echo clean done

$(TARGET): $(OBJECTS)
	$(CC) $(LINK_FLAGS) -o $@ $^

$(INTERMEDIATES_PATH)%.o: ../../%.cpp
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(INTERMEDIATES_PATH)%.o: %.cpp
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

# helper for printing out a variable value
print-%  : ; @echo $* = $($*)
