#Thanks to https://stackoverflow.com/questions/52034997/how-to-make-makefile-recompile-when-a-header-file-is-changed for the -MMD & -MP flags
#Without them headers wouldn't trigger recompilation

libs := $(wildcard lib/*.a)
srcs := $(shell find src/ -type f -iname "*.cpp")
objs := $(srcs:src/%.cpp=obj/%.o)
deps := $(objs:%.o=%.d)

#Disabled until I manage to fork a frickin MessageBox. Can't be that hard
GTKFLAGS := $(shell pkg-config --libs --cflags gtk+-3.0)

CXXFLAGS := -O3 -flto=auto -fPIC -m32 -std=c++17 -Wall -Wextra -Wpedantic
LDFLAGS := -shared

ifeq ($(shell echo $$NATIVE),1)
	CXXFLAGS += -march=native
endif

#Speed up compilation if additional dependencies are found
ifeq ($(shell type ccache &> /dev/null && echo "found"),found)
	export PATH := /usr/lib/ccache/bin:$(PATH)
endif
ifeq ($(shell type mold &> /dev/null && echo "found"),found)
	LDFLAGS += -fuse-ld=mold
endif

bin/SLSsteam.so: $(objs) $(libs)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $^ -o bin/SLSsteam.so
	strip -s bin/SLSsteam.so

-include $(deps)
obj/%.o : src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -Iinclude -MMD -MP -c $< -o $@

clean:
	rm -rvf "obj/" "bin/" "zips/"

install:
	sh setup.sh

zips: bin/SLSsteam.so
	@mkdir -p zips
	7z a -mx9 -m9=lzma2 "zips/SLSsteam.7z" "bin/SLSsteam.so" "setup.sh"
	7z a -mx9 -m9=lzma2 "zips/SLSsteam - Source.7z" "setup.sh" "include" "lib" "src" "Makefile"
	7z a -mx9 -m9=lzma2 "zips/SLSsteam - Full.7z" "bin/SLSsteam.so" "setup.sh" "include" "lib" "src" "Makefile"
	#Maybe should be somewhere else, but who cares. Does anyone even use this besides me?
	7z a -mx9 -m9=lzma2 "zips/SLSsteam - SLSConfig.7z" "$(HOME)/.config/SLSsteam/config.yaml"

all: clean build zips
rebuild: clean bin/SLSsteam.so

.PHONY: all build clean rebuild zips
