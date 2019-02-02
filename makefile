# Add .d to Make's recognized suffixes.
SUFFIXES += .d

# We don't need to clean up when we're making these targets
NODEPS := clean tags info

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

.PHONY: all clean info

# default target
all: wangemu src/tags

info:
	@echo $(SOURCES)

# Don't create dependencies when we're cleaning, for instance
ifeq (0, $(words $(findstring $(MAKECMDGOALS), $(NODEPS))))
    # Chances are, these files don't exist.  Make will create them and
    # clean up automatically afterwards
    -include $(DEPFILES)
endif

# -g -O0: vp is 2.5x, 15MB
# -g -O1: vp is 13x, 14.5MB
# -g -O2: vp is 16x, 14.4MB
#    -O2: vp is 16x, 14.1MB
#    -O3: vp is 16x, 14.2MB
CXX         := `wx-config --cxx`
CXXFLAGS    := -g -O0 -fno-common `wx-config --cxxflags`
CXXWARNINGS := -Wall -Wextra -Wshadow -Wformat -Wundef -Wstrict-aliasing=1 \
               -Wno-deprecated-declarations \
               -Wno-ctor-dtor-privacy -Woverloaded-virtual 
LDFLAGS     := `wx-config --libs`

# This is the rule for creating the dependency files
src/%.d: src/%.cpp
	$(CXX) $(CXXFLAGS) -MM -MT '$(patsubst src/%.cpp,obj/%.o,$<)' $< -MF $@

src/%.d: src/%.c
	$(CXX) $(CXXFLAGS) -MM -MT '$(patsubst src/%.c,obj/%.o,$<)' $< -MF $@

# This rule does the compilation
obj/%.o: src/%.cpp src/%.d
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CXXWARNINGS) -o $@ -c $<

obj/%.o: src/%.c src/%.d
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ -c $<

# link step
wangemu: $(OBJFILES)
	$(CXX) $(LDFLAGS) $(OBJFILES) -o wangemu

src/tags: $(CPP_SOURCES) $(C_SOURCES) $(H_SOURCES)
	(cd src; ctags *.h *.cpp *.c >& /dev/null)

clean:
	rm -f src/*.d src/tags obj/*.o
