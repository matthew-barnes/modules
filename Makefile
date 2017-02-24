export MODULESHOME = $(shell pwd)
.PHONY: doc www initdir install uninstall dist clean .makeinstallpath

# load previously saved install paths if any
-include .makeinstallpath

CURRENT_VERSION := $(shell grep '^set MODULES_CURRENT_VERSION' \
	modulecmd.tcl | cut -d ' ' -f 3)
DIST_PREFIX := modules-tcl-$(CURRENT_VERSION)

# set default installation paths if not yet defined
prefix ?= /usr/local/modules-tcl
bindir ?= $(prefix)/bin
libexecdir ?= $(prefix)/libexec
initdir ?= $(prefix)/init
modulefilesdir ?=$(prefix)/modulefiles
datarootdir ?= $(prefix)/share
mandir ?= $(datarootdir)/man
docdir ?= $(datarootdir)/doc
# modulepaths and modulefiles to enable in default config
modulepath ?= $(modulefilesdir)
loadedmodules ?=

# enable or not some specific definition
setmanpath ?= y
setbinpath ?= y
setdotmodulespath ?= n

all: initdir doc ChangeLog .makeinstallpath

# save defined install paths
.makeinstallpath:
	@echo "prefix := $(prefix)" >$@
	@echo "bindir := $(bindir)" >>$@
	@echo "libexecdir := $(libexecdir)" >>$@
	@echo "initdir := $(initdir)" >>$@
	@echo "modulefilesdir := $(modulefilesdir)" >>$@
	@echo "datarootdir := $(datarootdir)" >>$@
	@echo "mandir := $(mandir)" >>$@
	@echo "docdir := $(docdir)" >>$@
	@echo "setdotmodulespath := $(setdotmodulespath)" >>$@

initdir:
	make -C init all prefix=$(prefix) libexecdir=$(libexecdir) \
		initdir=$(initdir) modulefilesdir=$(modulefilesdir) \
		bindir=$(bindir) mandir=$(mandir) datarootdir=$(datarootdir) \
		setmanpath=$(setmanpath) setbinpath=$(setbinpath) \
		modulepath=$(modulepath) loadedmodules=$(loadedmodules) \
		setdotmodulespath=$(setdotmodulespath)

doc:
	make -C doc all prefix=$(prefix) libexecdir=$(libexecdir) \
		initdir=$(initdir) modulefilesdir=$(modulefilesdir) \
		bindir=$(bindir) mandir=$(mandir) datarootdir=$(datarootdir)

www:
	make -C www all

ChangeLog:
	contrib/gitlog2changelog.py

install: ChangeLog .makeinstallpath
	mkdir -p $(libexecdir)
	mkdir -p $(bindir)
	mkdir -p $(docdir)
	cp modulecmd.tcl $(libexecdir)/
	chmod +x $(libexecdir)/modulecmd.tcl
	cp contrib/envml $(bindir)/
	chmod +x $(bindir)/envml
	cp ChangeLog $(docdir)/
	cp NEWS $(docdir)/
	cp readme.txt $(docdir)/README
	make -C init install
	make -C doc install

uninstall:
	rm -f $(libexecdir)/modulecmd.tcl
	rm -f $(bindir)/envml
	rm -f $(addprefix $(docdir)/,ChangeLog NEWS README)
	make -C init uninstall
	make -C doc uninstall
	rmdir $(libexecdir)
	rmdir $(bindir)
	rmdir $(datarootdir)
	rmdir $(prefix)

dist: ChangeLog
	git archive --prefix=$(DIST_PREFIX)/ --worktree-attributes \
		-o $(DIST_PREFIX).tar HEAD
	tar -rf $(DIST_PREFIX).tar --transform 's,^,$(DIST_PREFIX)/,' ChangeLog
	gzip -f -9 $(DIST_PREFIX).tar

distclean: clean

clean: 
	rm -f *.log *.sum .makeinstallpath
ifeq ($(wildcard .git) $(wildcard contrib/gitlog2changelog.py),.git contrib/gitlog2changelog.py)
	rm -f ChangeLog
endif
	rm -f modules-tcl-*.tar.gz
	make -C init clean
	make -C doc clean
ifneq ($(wildcard www),)
	make -C www clean
endif

test:
	MODULEVERSION=Tcl; export MODULEVERSION; \
	OBJDIR=`pwd -P`; export OBJDIR; \
	TESTSUITEDIR=`cd testsuite;pwd -P`; export TESTSUITEDIR; \
	runtest --srcdir $$TESTSUITEDIR --objdir $$OBJDIR --tool modules -v
