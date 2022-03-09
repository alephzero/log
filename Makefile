BIN_DIR = bin

CXXFLAGS += -std=c++17
CXXFLAGS += -Iinclude
CXXFLAGS += -Ithird_party/alephzero/alephzero/include
CXXFLAGS += -Ithird_party/alephzero/alephzero/third_party/yyjson/src
CXXFLAGS += -Ithird_party/alephzero/alephzero/third_party/json/single_include
CXXFLAGS += -Ithird_party/mariusbancila/croncpp/include
CXXFLAGS += -DA0_EXT_NLOHMANN=1

LDFLAGS += -Lthird_party/alephzero/alephzero/lib
LDFLAGS += -static-libstdc++ -static-libgcc
LDFLAGS += -Wl,-Bstatic -lalephzero -Wl,-Bdynamic
LDFLAGS += -lpthread

DEBUG ?= 0
ifeq ($(DEBUG), 1)
	CXXFLAGS += -O0 -g3 -ggdb3 -DDEBUG
else
	CXXFLAGS += -O2 -flto -DNDEBUG
endif

$(BIN_DIR)/log: logger.cpp
	@mkdir -p $(@D)
	$(MAKE) -C third_party/alephzero/alephzero lib/libalephzero.a A0_EXT_NLOHMANN=1
	$(CXX) -o $@ $(CXXFLAGS) $< $(LDFLAGS)

.PHONY: run
run: $(BIN_DIR)/log
	$(BIN_DIR)/log

.PHONY: clean
clean:
	$(MAKE) -C third_party/alephzero/alephzero clean
	rm -rf $(BIN_DIR)
