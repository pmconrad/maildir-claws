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
#include <db.h>
#include <stdlib.h>

#include "utils.h"
#include "uiddb.h"

struct _UIDDB
{
	DB	*db_uid;
	DB	*db_uniq;
	guint32	 lastuid;
};

static gboolean initialized = FALSE;
static DB_ENV *dbenv;

void uiddb_init()
{
	db_env_create(&dbenv, 0);
	dbenv->open(dbenv, get_tmp_dir(), DB_INIT_MPOOL | DB_INIT_CDB | DB_CREATE, 0600);

	initialized = TRUE;
}

void uiddb_done()
{
	dbenv->close(dbenv, 0);

	initialized = FALSE;
}

int get_secondary_key(DB *dbp, const DBT *pkey, const DBT *pdata, DBT *skey)
{
	gchar *uniq;

	memset(skey, 0, sizeof(DBT));

	uniq = pdata->data + sizeof(guint32);
	skey->data = uniq;
	skey->size = strlen(uniq);

	return 0;
}

UIDDB *uiddb_open(const gchar *dbfile)
{
	gint	 ret;
	DB	*db_uid, *db_uniq;
	UIDDB	*uiddb;

	g_return_val_if_fail(initialized, NULL);

	/* open uid key based database */
	if ((ret = db_create(&db_uid, dbenv, 0)) != 0) {
		debug_print("db_create: %s\n", db_strerror(ret));
		return NULL;
	}
	if ((ret = db_uid->open(db_uid, NULL, dbfile, "uidkey", DB_BTREE, DB_CREATE, 0600)) != 0) {
		debug_print("DB->open: %s\n", db_strerror(ret));
		db_uid->close(db_uid, 0);
		return NULL;
	}
	debug_print("UID based database opened\n");

	/* open uniq key based database */
	if ((ret = db_create(&db_uniq, dbenv, 0)) != 0) {
		debug_print("db_create: %s\n", db_strerror(ret));
		db_uid->close(db_uid, 0);
		return NULL;
	}
	if ((ret = db_uniq->open(db_uniq, NULL, dbfile, "uniqkey", DB_BTREE, DB_CREATE, 0600)) != 0) {
		debug_print("DB->open: %s\n", db_strerror(ret));
		db_uniq->close(db_uniq, 0);
		db_uid->close(db_uid, 0);
		return NULL;
	}
	debug_print("Uniq based database opened\n");

	if ((ret = db_uid->associate(db_uid, NULL, db_uniq, get_secondary_key, 0)) != 0) {
		debug_print("DB->associate: %s\n", db_strerror(ret));
		db_uid->close(db_uid, 0);
		db_uniq->close(db_uniq, 0);
		return NULL;
	}
	debug_print("Databases associated\n");

	uiddb = g_new0(UIDDB, 1);
	uiddb->db_uid = db_uid;
	uiddb->db_uniq = db_uniq;
	uiddb->lastuid = 0;

	return uiddb;
}

void uiddb_close(UIDDB *uiddb)
{
	g_return_if_fail(uiddb != NULL);

	if (uiddb->db_uid != NULL)
		uiddb->db_uid->close(uiddb->db_uid, 0);
	if (uiddb->db_uniq != NULL)
		uiddb->db_uniq->close(uiddb->db_uniq, 0);
}

void uiddb_free_msgdata(MessageData *msgdata)
{
	g_free(msgdata->uniq);
	g_free(msgdata->info);
	g_free(msgdata->dir);
	g_free(msgdata);
}

static DBT marshal(MessageData *msgdata)
{
	DBT dbt;
	gpointer ptr;

	memset(&dbt, 0, sizeof(dbt));
	dbt.size = sizeof(msgdata->uid) + \
		   strlen(msgdata->uniq) + 1 + \
		   strlen(msgdata->info) + 1 + \
		   strlen(msgdata->dir) + 1;
	dbt.data = g_malloc0(dbt.size);

	ptr = dbt.data;

#define ADD_DATA(dataptr, size) \
{ \
	memcpy(ptr, dataptr, size); \
	ptr += size; \
}

	ADD_DATA(&msgdata->uid, sizeof(msgdata->uid));
	ADD_DATA(msgdata->uniq, strlen(msgdata->uniq) + 1);
	ADD_DATA(msgdata->info, strlen(msgdata->info) + 1);
	ADD_DATA(msgdata->dir, strlen(msgdata->dir) + 1);

#undef ADD_DATA	

	return dbt;
}

static MessageData *unmarshal(DBT dbt)
{
	gpointer ptr;
	MessageData *msgdata;

	ptr = dbt.data;
	msgdata = g_new0(MessageData, 1);

	memcpy(&msgdata->uid, ptr, sizeof(msgdata->uid));
	ptr += sizeof(msgdata->uid);
	msgdata->uniq = g_strdup(ptr);
	ptr += strlen(ptr) + 1;
	msgdata->info = g_strdup(ptr);
	ptr += strlen(ptr) + 1;
	msgdata->dir = g_strdup(ptr);

	return msgdata;
}

guint32 uiddb_get_new_uid(UIDDB *uiddb)
{
	DBC *cursor;
	DBT key, data;
	gint ret;
	guint32 uid, lastuid = -1;

	g_return_val_if_fail(uiddb != NULL, 0);

	lastuid = uiddb->lastuid;

	if (uiddb->lastuid > 0)
		return ++uiddb->lastuid;

	ret = uiddb->db_uid->cursor(uiddb->db_uid, NULL, &cursor, 0);
	if (ret != 0) {
		debug_print("DB->cursor: %s\n", db_strerror(ret));
		return -1;
	}

	memset(&key, 0, sizeof(key));
	memset(&data, 0, sizeof(data));
	while ((ret = cursor->c_get(cursor, &key, &data, DB_NEXT)) == 0) {
		uid = *((guint32 *) key.data);

		if (uid > lastuid)
			lastuid = uid;		

		memset(&key, 0, sizeof(key));
		memset(&data, 0, sizeof(data));
	}

	cursor->c_close(cursor);

	uiddb->lastuid = ++lastuid;
	return lastuid;
}

MessageData *uiddb_get_entry_for_uid(UIDDB *uiddb, guint32 uid)
{
	DBT key, data;

	g_return_val_if_fail(uiddb, NULL);

	memset(&key, 0, sizeof(key));
	memset(&data, 0, sizeof(data));

	key.size = sizeof(guint32);
	key.data = &uid;

	if (uiddb->db_uid->get(uiddb->db_uid, NULL, &key, &data, 0) != 0)
		return NULL;

	return unmarshal(data);
}

MessageData *uiddb_get_entry_for_uniq(UIDDB *uiddb, gchar *uniq)
{
	DBT key, pkey, data;

	g_return_val_if_fail(uiddb, NULL);

	memset(&key, 0, sizeof(key));
	memset(&pkey, 0, sizeof(pkey));
	memset(&data, 0, sizeof(data));

	key.size = strlen(uniq);
	key.data = uniq;

	if (uiddb->db_uniq->pget(uiddb->db_uniq, NULL, &key, &pkey, &data, 0) != 0)
		return NULL;

	return unmarshal(data);
}

void uiddb_delete_entry(UIDDB *uiddb, guint32 uid)
{
	DBT key;

	g_return_if_fail(uiddb);

	memset(&key, 0, sizeof(key));

	key.size = sizeof(guint32);
	key.data = &uid;

	uiddb->db_uid->del(uiddb->db_uid, NULL, &key, 0);
}

void uiddb_insert_entry(UIDDB *uiddb, MessageData *msgdata)
{
	DBT key, data;
	gint ret;

	g_return_if_fail(uiddb);

	memset(&key, 0, sizeof(key));
	memset(&data, 0, sizeof(data));

	key.size = sizeof(guint32);
	key.data = &msgdata->uid;

	data = marshal(msgdata);

	ret = uiddb->db_uid->put(uiddb->db_uid, NULL, &key, &data, 0);
	if (ret != 0)
		debug_print("DB->put: %s\n", db_strerror(ret));

	g_free(data.data);
}

static int uiddb_uid_compare(const void *a, const void *b)
{
    return *(guint32*)a - *(guint32*)b;
}

void uiddb_delete_entries_not_in_list(UIDDB *uiddb, MsgNumberList *list)
{
	DBC *cursor;
	DBT key, data;
	gint i, uidcnt, ret;
	guint32 *uid_sorted;

	g_return_if_fail(uiddb);
	if (list == NULL)
		return;

	ret = uiddb->db_uid->cursor(uiddb->db_uid, NULL, &cursor, DB_WRITECURSOR);
	if (ret != 0) {
		debug_print("DB->cursor: %s\n", db_strerror(ret));
		return;
	}

	uidcnt = g_slist_length(list);
	uid_sorted = g_new(guint32, uidcnt);
	for (i = 0; i < uidcnt; i++) {
	    uid_sorted[i] = GPOINTER_TO_INT(list->data);
	    list = g_slist_next(list);
	}
	
	memset(&key, 0, sizeof(key));
	memset(&data, 0, sizeof(data));
	while ((ret = cursor->c_get(cursor, &key, &data, DB_NEXT)) == 0) {
		guint32 uid = *((guint32 *) key.data);

		if (bsearch(&uid, uid_sorted, uidcnt, sizeof(guint32), &uiddb_uid_compare) == NULL)
			cursor->c_del(cursor, 0);

		memset(&key, 0, sizeof(key));
		memset(&data, 0, sizeof(data));
	}

	g_free(uid_sorted);

	cursor->c_close(cursor);
}
