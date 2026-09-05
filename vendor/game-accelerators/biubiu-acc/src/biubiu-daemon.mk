QUICKNET_C_NAMES := \
	fec imembase imemdata inetbase inetcode inetkcp inetnot inettcp \
	ineturl iposix isecure itimer itoolbox

QUICKNET_CXX_NAMES := \
	Combinator FecCodec FecCodecBuf FecPacket FecTransmission NePingRouter \
	NetFecCodec ProtocolBasic ProtocolImp RequestRepeat SessionDesc \
	SessionManager TransportUdp

QUICKNET_C_OBJECTS := \
	$(addprefix quicknet/system/,$(addsuffix .o,$(QUICKNET_C_NAMES)))
QUICKNET_CXX_OBJECTS := \
	$(addprefix quicknet/network/,$(addsuffix .o,$(QUICKNET_CXX_NAMES)))
DAEMON_OBJECTS := \
	biubiu-accd.o bbnet_bridge.o bbnet_transport.o confluence_codec.o \
	$(QUICKNET_C_OBJECTS) \
	$(QUICKNET_CXX_OBJECTS)

QUICKNET_INCLUDES := \
	-Iquicknet/common -Iquicknet/network -Iquicknet/system
COMMON_WARNINGS := -Wall -Wextra -Wformat=2

.PHONY: all clean

all: biubiu-accd

biubiu-accd: $(DAEMON_OBJECTS)
	$(CXX) -o $@ $(DAEMON_OBJECTS) $(LDFLAGS) $(LDLIBS)

biubiu-accd.o: biubiu-accd.c bbnet_transport.h confluence_codec.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(COMMON_WARNINGS) -std=gnu11 -pthread \
		-c $< -o $@

bbnet_transport.o: bbnet_transport.c bbnet_transport.h bbnet_bridge.h \
		confluence_codec.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(COMMON_WARNINGS) -std=gnu11 -pthread \
		-c $< -o $@

confluence_codec.o: confluence_codec.c confluence_codec.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(COMMON_WARNINGS) -std=gnu11 \
		-c $< -o $@

bbnet_bridge.o: bbnet_bridge.cpp bbnet_bridge.h
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(COMMON_WARNINGS) \
		-Wno-unused-parameter -std=gnu++11 -pthread \
		$(QUICKNET_INCLUDES) -c $< -o $@

quicknet/system/%.o: quicknet/system/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=gnu11 \
		$(QUICKNET_INCLUDES) -c $< -o $@

quicknet/network/%.o: quicknet/network/%.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -Wno-unused-parameter -std=gnu++11 \
		$(QUICKNET_INCLUDES) -c $< -o $@

clean:
	$(RM) biubiu-accd $(DAEMON_OBJECTS)
