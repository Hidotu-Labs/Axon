CC ?= gcc
CFLAGS ?= -Wall -Wextra -pedantic -std=c11 -O2 -D_GNU_SOURCE
CFLAGS += -MMD -MP -Iinclude -Isrc
LDFLAGS += $(shell pkg-config --libs libarchive libcurl)
PKG_CFLAGS = $(shell pkg-config --cflags libarchive libcurl)
CFLAGS += $(PKG_CFLAGS)

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
LIBEXECDIR ?= $(PREFIX)/libexec/axon
CFLAGS += -DAXON_LIBEXECDIR=\"$(LIBEXECDIR)\"

SRCS = \
	src/main.c \
	src/config.c \
	src/cli/get.c \
	src/cli/remove.c \
	src/cli/search.c \
	src/cli/info.c \
	src/cli/sync.c \
	src/cli/upgrade.c \
	src/package/pkginfo.c \
	src/package/archive.c \
	src/package/version.c \
	src/repository/repo.c \
	src/repository/arch.c \
	src/repository/downloader.c \
	src/database/local.c \
	src/database/ownership.c \
	src/resolver/dependency.c \
	src/resolver/conflict.c \
	src/transaction/prepare.c \
	src/transaction/commit.c \
	src/transaction/lifecycle.c \
	src/transaction/rollback.c \
	src/security/signature.c \
	src/security/verify.c

OBJS = $(SRCS:.c=.o)
-include $(OBJS:.o=.d)

LIB_OBJS = $(filter-out src/main.o, $(OBJS))

TEST_SRCS = \
	tests/test_version.c \
	tests/test_dependency.c \
	tests/test_pkginfo.c \
	tests/test_search.c \
	tests/test_sync.c \
	tests/test_archive.c

TEST_BINS = $(TEST_SRCS:.c=)

TARGET = axon

.DEFAULT_GOAL := all

.PHONY: all clean test test-bootstrap test-system install uninstall

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

tests/test_%: tests/test_%.c $(LIB_OBJS)
	$(CC) $(CFLAGS) $< $(LIB_OBJS) $(LDFLAGS) -o $@

test-system: $(TARGET)
	python3 tests/test_system.py

test-bootstrap: $(TARGET)
	python3 tests/test_bootstrap.py

test: $(TEST_BINS) test-bootstrap test-system
	@echo "================ Running Axon Test Suite ================"
	@for t in $(TEST_BINS); do \
		echo "Running $$t..."; \
		$$t || exit 1; \
	done
	@echo "================ All tests passed! ======================"

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)
	install -d $(DESTDIR)$(LIBEXECDIR)/profiles/avory
	install -m 644 libexec/axon-runtime.py $(DESTDIR)$(LIBEXECDIR)/axon-runtime.py
	install -m 644 libexec/profiles/avory/* $(DESTDIR)$(LIBEXECDIR)/profiles/avory/

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
	rm -rf $(DESTDIR)$(LIBEXECDIR)

clean:
	rm -f $(OBJS) $(TARGET) $(TEST_BINS) tests/*.o $(OBJS:.o=.d) tests/*.d
