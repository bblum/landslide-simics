###################### FULL BUILD TARGETS ############################

REFK ?= pathos

FINALTARGETS=kernel
FINALVERYCLEANS=$(FINALTARGETS)
FINALCLEANS=$(FINALTARGETS:%=%.gz) $(FINALTARGETS:%=%.strip)

# Inspired by Elly:
# "For each FINALTARGETS" (in theory, but in practice for "kernel"):
# 1. nuke temp/kernel__*.o (double-underscore is for a less-risky pattern)
# 2. copy 410kern/kernel_$(REFK).o to temp/kernel__$(REFK).o
# 3. link the latter
# This way, every time REFK changes we need to "build" the object in temp;
# "building" it has the side effect of deleting whichever one we yanked in
# when REFK had a diferent value.  So we do one copy per change of REFK.
# This kind of gamesmanship is NOT necessary with cons and derivatives.
#
$(FINALTARGETS) : % : $(BUILDDIR)/user_apps.o $(BUILDDIR)/%__$(REFK).o
	$(LD) $(KLDFLAGS) -o $@ $^
#	$(LD) -T $(410KDIR)/kernel.lds $(KLDFLAGS) -o $@ $^
$(BUILDDIR)/%__$(REFK).o : $(410KDIR)/%_$(REFK).o
	-rm -f $(wildcard $(BUILDDIR)/$*__*.o)
	mkdir -p $(BUILDDIR)
	cp $< $@

$(patsubst %,%.strip,$(FINALTARGETS)) : %.strip : %
	$(OBJCOPY) -g $< $@

$(patsubst %,%.gz,$(FINALTARGETS)) : %.gz : %.strip
	gzip -c $< > $@

### Boy was that a bad idea, especially for this semester.
### Too clever by half.  Sorry.
### .INTERMEDIATE: $(FINALTARGETS:%=%.strip)
.INTERMEDIATE: $(FINALTARGETS:%=%.gz)

$(410KDIR)/bootfd.img.gz:
	@echo file $(410KDIR)/bootfd.img.gz missing
	@false

$(410KDIR)/menu.lst:
	@echo file $(410KDIR)/menu.lst missing
	@false

ifeq (,$(INFRASTRUCTURE_OVERRIDE_BOOTFDIMG))
bootfd.img: $(FINALTARGETS:%=%.gz) $(410KDIR)/bootfd.img.gz $(410KDIR)/menu.lst
	gzip -cd $(410KDIR)/bootfd.img.gz > $(PROJROOT)/bootfd.img
	mcopy -o -i "$(PROJROOT)/bootfd.img" $(FINALTARGETS:%=%.gz) $(410KDIR)/menu.lst ::/boot/
endif
