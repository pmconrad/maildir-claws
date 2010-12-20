/*
 * Maildir Plugin -- Maildir++ support for Sylpheed
 * Copyright (C) 2003-2004 Christoph Hohmann
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <glib.h>
#include <glib/gi18n.h>

#include "common/version.h"
#include "common/plugin.h"
#include "folder.h"
#include "main.h"

#include "pluginconfig.h"
#include "maildir.h"
#include "maildir_gtk.h"
#include "uiddb.h"
#include "plugin.h"

gint plugin_init(gchar **error)
{
	if (!check_plugin_version(MAKE_NUMERIC_VERSION(3,7,6,53),
				VERSION_NUMERIC, "Maildir++", error))
		return -1;

	uiddb_init();
	folder_register_class(maildir_get_class());

	maildir_gtk_init();

	return 0;
}

gboolean plugin_done(void)
{
	maildir_gtk_done();
	if (!claws_is_exiting())
		folder_unregister_class(maildir_get_class());
	uiddb_done();
	return TRUE;
}

const gchar *plugin_name(void)
{
	return "Maildir++";
}

const gchar *plugin_desc(void)
{
	return _("This plugin provides direct support for Maildir++ mailboxes.\n"
	       "\n"
	       "With this plugin you can share your Maildir++ mailbox with "
	       "other mailers or IMAP servers.");
}

const gchar *plugin_type(void)
{
	return "GTK2";
}

const gchar *plugin_licence(void)
{
		return "GPL2+";
}

const gchar *plugin_version(void)
{
	return PLUGINVERSION;
}

struct PluginFeature *plugin_provides(void)
{
	static struct PluginFeature features[] = 
		{ {PLUGIN_FOLDERCLASS, N_("Maildir")},
		  {PLUGIN_NOTHING, NULL}};
	return features;
}
