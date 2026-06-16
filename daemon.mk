PREFIX ?= /usr
BINDIR ?= $(PREFIX)/bin

CC ?= gcc
CXX ?= g++

CPPFLAGS ?=
CFLAGS ?= -Wall -Wextra -O2
CXXFLAGS ?= -std=c++11 -Wall -Wextra -O2
LDFLAGS ?=

DAEMON := meshcore-he4025
PROBE := sx1276-regversion

DAEMON_SRCS := \
	src/config.cpp \
	src/linux_hal.cpp \
	src/main.cpp \
	src/sx1276.cpp \
	src/web_server.cpp

DAEMON_OBJS := $(DAEMON_SRCS:.cpp=.o)

.PHONY: all clean install

all: $(DAEMON) $(PROBE)

$(DAEMON): $(DAEMON_OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(DAEMON_OBJS)

$(PROBE): tools/sx1276_regversion.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $<

%.o: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

install: all
	install -d $(DESTDIR)$(BINDIR)
	install -m 0755 $(DAEMON) $(DESTDIR)$(BINDIR)/$(DAEMON)
	install -m 0755 $(PROBE) $(DESTDIR)$(BINDIR)/$(PROBE)
	install -d $(DESTDIR)$(PREFIX)/share/meshcore-he4025/boards
	install -m 0644 boards/dragino-ibb-v1.0.conf $(DESTDIR)$(PREFIX)/share/meshcore-he4025/boards/dragino-ibb-v1.0.conf
	install -d $(DESTDIR)/etc/config
	install -m 0644 openwrt/files/etc/config/meshcore $(DESTDIR)/etc/config/meshcore
	install -d $(DESTDIR)/etc/init.d
	install -m 0755 openwrt/files/etc/init.d/meshcore $(DESTDIR)/etc/init.d/meshcore

clean:
	rm -f $(DAEMON_OBJS) $(DAEMON) $(PROBE)

