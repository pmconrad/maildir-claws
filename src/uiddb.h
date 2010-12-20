/*
 * Maildir Plugin -- Maildir++ support for Sylpheed
 * Copyright (C) 2003 Christoph Hohmann
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

#ifndef UIDDB_H
#define UIDDB_H 1

#include <glib.h>

typedef struct _UIDDB UIDDB;
typedef struct _MessageData MessageData;

#include "procmsg.h"

struct _MessageData
{
	guint32	 uid;
	gchar   *uniq;
	gchar	*info;
	gchar	*dir;
};

void uiddb_init();
void uiddb_done();

void uiddb_free_msgdata(MessageData *);

UIDDB *uiddb_open(const gchar *);
void uiddb_close(UIDDB *);
guint32 uiddb_get_new_uid(UIDDB *);

MessageData *uiddb_get_entry_for_uid(UIDDB *, guint32);
MessageData *uiddb_get_entry_for_uniq(UIDDB *uiddb, gchar *);
void uiddb_delete_entry(UIDDB *, guint32);
void uiddb_insert_entry(UIDDB *, MessageData *);
void uiddb_delete_entries_not_in_list(UIDDB *uiddb, MsgNumberList *list);

#endif /* UIDDB_H */
