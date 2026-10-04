# libhelixaac
HELIXAACLIB := $(CODECDIR)/libhelixaac.a
HELIXAACLIB_SRC := $(call preprocess, $(RBCODECLIB_DIR)/codecs/libhelixaac/SOURCES)
HELIXAACLIB_OBJ := $(call c2obj, $(HELIXAACLIB_SRC))
OTHER_SRC += $(HELIXAACLIB_SRC)

$(HELIXAACLIB): $(HELIXAACLIB_OBJ)
	$(SILENT)$(shell rm -f $@)
	$(call PRINTS,AR $(@F))$(AR) rcs $@ $^ >/dev/null
