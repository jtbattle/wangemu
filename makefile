# This is a makefile for building the emulator on OSX.
#
# make targets:
#
# make         -- shorthand for "make debug"
# make debug   -- non-optimized wangemu build, and generate tags
# make opt     --     optimized wangemu build, and generate tags
# make tags    -- make ctags index from files in src/
# make clean   -- remove all build products
# make release -- create a "release" directory containing the app bundle and support files
#                 ideally, do "make clean opt release"

.PHONY: debug opt tags clean release

# Add .d to Make's recognized suffixes.
SUFFIXES += .d

# don't create dependency files for these targets
NODEPS := clean tags

# Find all the source files in the src/ directory
CPP_SOURCES := $(shell find src -name "*.cpp")
C_SOURCES   := $(shell find src -name "*.c")
H_SOURCES   := $(shell find src -name "*.h")
SOURCES     := $(CPP_SOURCES) $(C_SOURCES)

# These are the dependency files, which make will clean up after it creates them
DEPFILES := $(patsubst %.cpp,%.d,$(CPP_SOURCES)) \
	    $(patsubst %.c,%.d,$(C_SOURCES))

OBJFILES := $(patsubst src/%.cpp,obj/%.o,$(CPP_SOURCES)) \
	    $(patsubst src/%.c,obj/%.o,$(C_SOURCES))

# debug build
debug: OPTFLAGS := -g -O0 -fno-common 
debug: wangemu tags

# optimized build
opt: OPTFLAGS := -O2 -fno-common 
opt: wangemu tags

CXX         := `wx-config --cxx`
CXXFLAGS    := `wx-config --cxxflags`
CXXWARNINGS := -Wall -Wextra -Wshadow -Wformat -Wundef -Wstrict-aliasing=1 \
               -Wno-deprecated-declarations \
               -Wno-ctor-dtor-privacy -Woverloaded-virtual 
LDFLAGS     := `wx-config --libs`

# don't create dependencies when we're cleaning, for instance
ifeq (0, $(words $(findstring $(MAKECMDGOALS), $(NODEPS))))
    -include $(DEPFILES)
endif

# ===== create the dependency files =====

src/%.d: src/%.cpp
	$(CXX) $(CXXFLAGS) -MM -MT '$(patsubst src/%.cpp,obj/%.o,$<)' $< -MF $@

src/%.d: src/%.c
	$(CXX) $(CXXFLAGS) -MM -MT '$(patsubst src/%.c,obj/%.o,$<)' $< -MF $@

# ===== build the .o files =====

obj/%.o: src/%.cpp src/%.d
	@mkdir -p $(dir $@)
	$(CXX) $(OPTFLAGS) $(CXXFLAGS) $(CXXWARNINGS) -o $@ -c $<

obj/%.o: src/%.c src/%.d
	@mkdir -p $(dir $@)
	$(CXX) $(OPTFLAGS) $(CXXFLAGS) -o $@ -c $<

# ==== link step ====

wangemu: $(OBJFILES)
	$(CXX) $(LDFLAGS) $(OBJFILES) -o wangemu

# ==== build ctags index file ====

tags: src/tags
src/tags: $(CPP_SOURCES) $(C_SOURCES) $(H_SOURCES)
	(cd src; ctags *.h *.cpp *.c >& /dev/null)

# ===== build a release directory =====
RELEASE_DIR  := release
APP_NAME     := wangemu.app
CONTENTS_DIR := $(RELEASE_DIR)/$(APP_NAME)/Contents
MACOS_DIR    := $(CONTENTS_DIR)/MacOS
RESOURCE_DIR := $(CONTENTS_DIR)/Resources

define plist_var
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple Computer//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleDevelopmentRegion</key>
	<string>English</string>
	<key>CFBundleExecutable</key>
	<string>wangemu</string>
	<key>CFBundleIconFile</key>
	<string>wang</string>
	<key>CFBundleIdentifier</key>
	<string>www.wang2200.org</string>
	<key>CFBundleInfoDictionaryVersion</key>
	<string>6.0</string>
	<key>CFBundlePackageType</key>
	<string>APPL</string>
	<key>CFBundleSignature</key>
	<string>????</string>
	<key>CFBundleVersion</key>
	<string>3.0</string>
</dict>
</plist>
endef

release:
	@./make_release

# ===== clean build products =====

clean:
	rm -f src/*.d src/tags obj/*.o 
	rm -rf release
