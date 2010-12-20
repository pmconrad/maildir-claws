plugindir = $(CLAWS_MAIL_PLUGINDIR)
plugin_LTLIBRARIES = maildir.la

## Maildir folder plugin

maildir_la_SOURCES = \
	plugin.c \
	maildir.c maildir.h \
	maildir_gtk.c maildir_gtk.h \
	uiddb.c uiddb.h

maildir_la_LDFLAGS = \
	-avoid-version -module

maildir_la_LIBADD = \
	$(DBLIB) 

maildir_la_CPPFLAGS = \
	$(CLAWS_MAIL_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(GTK_CFLAGS)

maildir_gtk_la_CPPFLAGS = \
	$(CLAWS_MAIL_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(GTK_CFLAGS)

AM_CPPFLAGS = \
	$(CLAWS_MAIL_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(GTK_CFLAGS)
