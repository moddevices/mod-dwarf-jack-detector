ifeq ($(MAKECMDGOALS),)
MAKECMDGOALS = all
endif

$(MAKECMDGOALS):
	make $(MAKECMDGOALS) -C mod-auto-input-selector
	make $(MAKECMDGOALS) -C mod-auto-output-selector
	make $(MAKECMDGOALS) -C mod-cv-jack-detector
