OUTPUT_DIR = Debug
DEBUG_FLAGS = -g -D_DEBUG

CXXFLAGS    = -I$../../include -fPIC $(DEBUG_FLAGS) -Wall -fshort-wchar
LDFLAGS     = -shared -dynamiclib -lgcc

OBJS = $(addprefix $(OUTPUT_DIR)/, $(addsuffix .o, $(basename $(SRCS))))

all:    $(OUTPUT_DIR)/$(TARGET).so

$(OUTPUT_DIR)/$(TARGET).so: $(OUTPUT_DIR) $(OBJS)
	$(CXX)  $(LDFLAGS) -o $@ $(OBJS)

$(OUTPUT_DIR):
	mkdir $@

$(OUTPUT_DIR)/%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	$(RM) $(OBJS) $(OUTPUT_DIR)/$(TARGET)

