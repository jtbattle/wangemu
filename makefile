# Add .d to Make's recognized suffixes.
SUFFIXES += .d

# We don't need to clean up when we're making these targets
NODEPS := clean tags info

# Find all the source files in the src/ directory
CPP_SOURCES := $(shell find src -name "*.cpp")
C_SOURCES   := $(shell find src -name "*.c")
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

CXX         := `wx-config --cxx`
CXXFLAGS    := -g -O0 -fno-common `wx-config --cxxflags`
CXXWARNINGS := -Wall -Wundef -Wunused-parameter -Wno-ctor-dtor-privacy -Woverloaded-virtual -Wno-deprecated-declarations
LDFLAGS     := `wx-config --libs`

# This is the rule for creating the dependency files
src/%.d: src/%.cpp
	$(CXX) $(CXXFLAGS) -MM -MT '$(patsubst src/%.cpp,obj/%.o,$<)' $< -MF $@

src/%.d: src/%.c
	$(CXX) $(CXXFLAGS) -MM -MT '$(patsubst src/%.c,obj/%.o,$<)' $< -MF $@

# This rule does the compilation
obj/%.o: src/%.cpp src/%.d
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ -c $<

obj/%.o: src/%.c src/%.d
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ -c $<

# link step
wangemu: $(OBJFILES)
	$(CXX) $(LDFLAGS) $(OBJFILES) -o wangemu

src/tags:
	(cd src; ctags *.h *.cpp *.c)

clean:
	rm -f src/*.d src/tags obj/*.o
