SRC := ../../source
CORE := $(wildcard $(SRC)/core/*.cpp)
FAAD := $(wildcard $(SRC)/third_party/faad2/*.c)
WV := $(wildcard $(SRC)/third_party/wavpack/*.c)
DEFS := -DHAVE_INTTYPES_H=1 -DHAVE_MEMCPY=1 -DHAVE_STRING_H=1 -DHAVE_STRINGS_H=1 -DHAVE_SYS_STAT_H=1 \
        -DHAVE_SYS_TYPES_H=1 -DHAVE_LRINTF=1 -DPACKAGE_VERSION=\"2.11.1\" -DAPPLY_DRC -DENABLE_LEGACY -DENABLE_DSD
OBJ := $(patsubst $(SRC)/%,obj/%.o,$(CORE) $(FAAD) $(WV)) obj/main.o
CXXFLAGS := -O2 -g -std=gnu++17 -Wall -Wno-unused-function $(DEFS)
CFLAGS := -O2 -g -w $(DEFS) -I$(SRC)/third_party/faad2 -I$(SRC)/third_party/wavpack
hosttest: $(OBJ)
	g++ -o $@ $^ -lvorbisidec -logg -lz -llua5.1 -lm
obj/%.cpp.o: $(SRC)/%.cpp
	@mkdir -p $(dir $@); g++ $(CXXFLAGS) -c $< -o $@
obj/%.c.o: $(SRC)/%.c
	@mkdir -p $(dir $@); gcc $(CFLAGS) -c $< -o $@
obj/main.o: main.cpp
	@mkdir -p obj; g++ $(CXXFLAGS) -c $< -o $@
clean:
	rm -rf obj hosttest
