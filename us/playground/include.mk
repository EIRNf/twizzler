PLAYGROUND_PROGS=example queue net logtest ut netapi queue2

PLAYGROUND_LIBS=-Wl,--whole-archive -lbacktrace -Wl,--no-whole-archive

PLAYGROUND_CFLAGS=-g -fno-omit-frame-pointer -O3 -Ius/include

LIBS_logtest=-ltwzsec -ltomcrypt -ltommath

LIBS_netapi=-ltwznet

$(BUILDDIR)/us/sysroot/usr/bin/%: $(BUILDDIR)/us/playground/%.o $(SYSROOT_READY) $(SYSLIBS) $(UTILS) $(ALL_EXTRAS)
	@echo "[LD]      $@"
	@$(TWZCC) $(TWZLDFLAGS) -g -o $@ -MD $< $(EXTRAS_$(notdir $@)) $(LIBS_$(notdir $@)) $(PLAYGROUND_LIBS)

$(BUILDDIR)/us/sysroot/usr/bin/%: $(BUILDDIR)/us/playground/%.opp $(SYSROOT_READY) $(SYSLIBS) $(UTILS) $(ALL_EXTRAS)
	@echo "[LD]      $@"
	@$(TWZCXX) $(TWZLDFLAGS) -g -o $@ -MD $< $(EXTRAS_$(notdir $@)) $(LIBS_$(notdir $@)) $(PLAYGROUND_LIBS)

$(BUILDDIR)/us/playground/%.o: us/playground/%.c $(MUSL_HDRS)
	@mkdir -p $(dir $@)
	@echo "[CC]      $@"
	$(TWZCC) $(TWZCFLAGS) $(PLAYGROUND_CFLAGS) -o $@ $(CFLAGS_$(basename $(notdir $@))) -c -MD $<

$(BUILDDIR)/us/playground/%.opp: us/playground/%.cpp $(MUSL_HDRS)
	@mkdir -p $(dir $@)
	@echo "[CC]      $@"
	@$(TWZCXX) $(TWZCFLAGS) $(PLAYGROUND_CFLAGS) -o $@ $(CFLAGS_$(basename $(notdir $@))) -c -MD $<

-include $($(BUILDDIR)/us/playground/*.d)

SYSROOT_FILES+=$(addprefix $(BUILDDIR)/us/sysroot/usr/bin/,$(PLAYGROUND_PROGS))

