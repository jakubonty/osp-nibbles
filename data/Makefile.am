SUBDIRS = icons levels pix

gsettings_in_file = org.gnome.nibbles.gschema.xml.in
gsettings_SCHEMAS = $(gsettings_in_file:.xml.in=.xml)
@INTLTOOL_XML_NOMERGE_RULE@
@GSETTINGS_RULES@

pixmapdir = $(datadir)/gnome-nibbles/pixmaps
pixmap_DATA = gnome-nibbles-logo.svg

soundsdir = $(datadir)/gnome-nibbles/sounds
sounds_DATA = \
	appear.ogg \
	bonus.ogg \
	crash.ogg \
	gameover.ogg \
	gobble.ogg \
	life.ogg \
	reverse.ogg \
	teleport.ogg

desktopdir=$(datadir)/applications
desktop_in_files = gnibbles.desktop.in.in
desktop_DATA = $(desktop_in_files:.desktop.in.in=.desktop)
@INTLTOOL_DESKTOP_RULE@

man_MANS = gnome-nibbles.6

EXTRA_DIST = \
	$(pixmap_DATA) \
	$(sounds_DATA) \
	$(gsettings_in_file) \
	$(man_MANS)

CLEANFILES = $(desktop_DATA) $(gsettings_SCHEMAS)
DISTCLEANFILES = $(desktop_DATA) $(gsettings_SCHEMAS)

install-exec-hook:
	-if test "$(setgid)" = "true"; then \
	  chgrp $(scores_group) $(DESTDIR)$(bindir)/gnome-nibbles && chmod 2555 $(DESTDIR)$(bindir)/gnome-nibbles ;\
	fi

-include $(top_srcdir)/git.mk
