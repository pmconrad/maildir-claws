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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "defs.h"

#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <glob.h>
#include <unistd.h>
#include <glib.h>

#include "utils.h"
#include "procmsg.h"
#include "procheader.h"
#include "folder.h"
#include "maildir.h"
#include "localfolder.h"
#include "uiddb.h"
#include "mainwindow.h"
#include "summaryview.h"
#include "messageview.h"

#define MAILDIR_FOLDERITEM(item) ((MaildirFolderItem *) item)

typedef struct _MaildirFolder MaildirFolder;
typedef struct _MaildirFolderItem MaildirFolderItem;

static Folder *maildir_folder_new(const gchar * name,
				  const gchar * folder);
static void maildir_folder_destroy(Folder * folder);
static gint maildir_scan_tree(Folder * folder);
static FolderItem *maildir_item_new(Folder * folder);
static void maildir_item_destroy(Folder * folder, FolderItem * item);
static gchar *maildir_item_get_path(Folder * folder, FolderItem * item);
static gint maildir_get_num_list(Folder * folder, FolderItem * item,
				 MsgNumberList ** list,
				 gboolean * old_uids_valid);
static gboolean maildir_scan_required(Folder * folder, FolderItem * item);
static MsgInfo *maildir_get_msginfo(Folder * folder, FolderItem * item,
				    gint num);
static gchar *maildir_fetch_msg(Folder * folder, FolderItem * item,
				gint num);
static gint maildir_add_msg(Folder * folder, FolderItem * _dest,
			    const gchar * file, MsgFlags * flags);
static gint maildir_copy_msg(Folder * folder, FolderItem * dest,
			     MsgInfo * msginfo);
static gint maildir_remove_msg(Folder * folder, FolderItem * _item,
			       gint num);
static void maildir_change_flags(Folder * folder, FolderItem * item,
				 MsgInfo * msginfo, MsgPermFlags newflags);
static FolderItem *maildir_create_folder(Folder * folder,
					 FolderItem * parent,
					 const gchar * name);
static gint maildir_create_tree(Folder *folder);
static void remove_missing_folder_items(Folder *folder);
static gint maildir_remove_folder(Folder *folder, FolderItem *item);
static gint maildir_rename_folder(Folder *folder, FolderItem *item,
			     const gchar *name);
static gint maildir_get_flags (Folder *folder,  FolderItem *item,
			       MsgInfoList *msglist, GHashTable *msgflags);

static gchar *filename_from_utf8(const gchar *path);
static gchar *filename_to_utf8(const gchar *path);

FolderClass maildir_class;

struct _MaildirFolder
{
	LocalFolder folder;
};

struct _MaildirFolderItem
{
	FolderItem item;

	guint lastuid;
	UIDDB *db;
};

FolderClass *maildir_get_class()
{
	if (maildir_class.idstr == NULL) {
		maildir_class.type = F_MAILDIR;
		maildir_class.idstr = "maildir";
		maildir_class.uistr = "Maildir++";

		/* Folder functions */
		maildir_class.new_folder = maildir_folder_new;
		maildir_class.destroy_folder = maildir_folder_destroy;
		maildir_class.set_xml = folder_local_set_xml;
		maildir_class.get_xml = folder_local_get_xml;
		maildir_class.scan_tree = maildir_scan_tree;
		maildir_class.create_tree = maildir_create_tree;

		/* FolderItem functions */
		maildir_class.item_new = maildir_item_new;
		maildir_class.item_destroy = maildir_item_destroy;
		maildir_class.item_get_path = maildir_item_get_path;
		maildir_class.create_folder = maildir_create_folder;
		maildir_class.remove_folder = maildir_remove_folder;
		maildir_class.rename_folder = maildir_rename_folder;
		maildir_class.get_num_list = maildir_get_num_list;
		maildir_class.scan_required = maildir_scan_required;

		/* Message functions */
		maildir_class.get_msginfo = maildir_get_msginfo;
		maildir_class.fetch_msg = maildir_fetch_msg;
		maildir_class.add_msg = maildir_add_msg;
		maildir_class.copy_msg = maildir_copy_msg;
		maildir_class.remove_msg = maildir_remove_msg;
		maildir_class.change_flags = maildir_change_flags;
		maildir_class.get_flags = maildir_get_flags;
	}

	return &maildir_class;
}

static Folder *maildir_folder_new(const gchar * name,
				  const gchar * path)
{
        MaildirFolder *folder;                   
        
        folder = g_new0(MaildirFolder, 1);
        FOLDER(folder)->klass = &maildir_class;
        folder_local_folder_init(FOLDER(folder), name, path);

        return FOLDER(folder);
}

static void maildir_folder_destroy(Folder *_folder)
{
	MaildirFolder *folder = (MaildirFolder *) _folder;

	folder_local_folder_destroy(LOCAL_FOLDER(folder));
}

static gint open_database(MaildirFolderItem *item)
{
	gchar *path, *database;

	g_return_val_if_fail(item->db == NULL, -1);
	
	path = maildir_item_get_path(FOLDER_ITEM(item)->folder, FOLDER_ITEM(item));
	Xstrcat_a(database, path, G_DIR_SEPARATOR_S "sylpheed_uid.db", return -1);
	g_free(path);

	item->db = uiddb_open(database);
	g_return_val_if_fail(item->db != NULL, -1);
	
	return 0;
}

static void close_database(MaildirFolderItem *item)
{
        g_return_if_fail(item->db != NULL);
	
	uiddb_close(item->db);
	item->db = NULL;
}

static FolderItem *maildir_item_new(Folder *folder)
{
        MaildirFolderItem *item;

        item = g_new0(MaildirFolderItem, 1);
        item->lastuid = 0;
	item->db = NULL;
        
        return (FolderItem *) item;

}

static void maildir_item_destroy(Folder *folder, FolderItem *_item)
{
        MaildirFolderItem *item = (MaildirFolderItem *)_item;

        g_return_if_fail(item != NULL);
	
        g_free(item);
}

static gchar *maildir_item_get_path(Folder *folder, FolderItem *item)
{
	gchar *folder_path, *path, *real_path;

	g_return_val_if_fail(folder != NULL, NULL);
	g_return_val_if_fail(item != NULL, NULL);

	folder_path = g_strdup(LOCAL_FOLDER(folder)->rootpath);
	g_return_val_if_fail(folder_path != NULL, NULL);

	if (g_path_is_absolute(folder_path)) {
                if (item->path && strcmp(item->path, ".inbox"))
                        path = g_strconcat(folder_path, G_DIR_SEPARATOR_S,
                                           item->path, NULL);
                else
                        path = g_strdup(folder_path);
        } else {
                if (item->path && strcmp(item->path, ".inbox"))
                        path = g_strconcat(get_home_dir(), G_DIR_SEPARATOR_S,
                                           folder_path, G_DIR_SEPARATOR_S,
                                           item->path, NULL);
                else
                        path = g_strconcat(get_home_dir(), G_DIR_SEPARATOR_S,
                                           folder_path, NULL);
        }
	g_free(folder_path);

	real_path = filename_from_utf8(path);

	g_free(path);
	return real_path;
}

static void build_tree(GNode *node, glob_t *globbuf)
{
        int i;
	FolderItem *parent = FOLDER_ITEM(node->data);
	gchar *prefix = parent->path ?  filename_from_utf8(parent->path) : g_strdup("");
	Folder *folder = parent->folder;

        for (i = 0; i < globbuf->gl_pathc; i++) {
		FolderItem *newitem;
		GNode *newnode;
		gchar *dirname;
		gchar *foldername;
		gchar *tmpstr, *dirname_utf8, *foldername_utf8;
		gboolean res;

		dirname = g_path_get_basename(globbuf->gl_pathv[i]);
		foldername = &(dirname[strlen(prefix) + 1]);

                if (dirname[0] == '.' && dirname[1] == '\0') {
			g_free(dirname);
                        continue;
		}
                if (strncmp(dirname, prefix, strlen(prefix))) {
			g_free(dirname);
                        continue;
		}
                if (dirname[strlen(prefix)] != '.') {
			g_free(dirname);
                        continue;
		}
                if (strchr(foldername, '.') != NULL) {
			g_free(dirname);
                        continue;
		}

                if (!is_dir_exist(globbuf->gl_pathv[i])) {
			g_free(dirname);
                        continue;
		}
		tmpstr = g_strconcat(globbuf->gl_pathv[i], "/cur", NULL);
                res = is_dir_exist(tmpstr);
		g_free(tmpstr);
		if (!res) {
			g_free(dirname);
                        continue;
		}
		dirname_utf8 = filename_to_utf8(dirname);
		foldername_utf8 = filename_to_utf8(foldername);

		/* don't add items that already exist in the tree */
		newitem = folder_find_child_item_by_name(parent, foldername_utf8);
		if (newitem == NULL) {
			newitem = folder_item_new(parent->folder, foldername_utf8, dirname_utf8);
			newitem->folder = folder;

			newnode = g_node_new(newitem);
			newitem->node = newnode;
			g_node_append(node, newnode);

            		debug_print("added item %s\n", newitem->path);
		}

		g_free(dirname_utf8);
		g_free(foldername_utf8);

		if (!parent->path) {
			if (!folder->outbox && !strcmp(dirname, "." OUTBOX_DIR)) {
				newitem->stype = F_OUTBOX;
				folder->outbox = newitem;
			} else if (!folder->draft && !strcmp(dirname, "." DRAFT_DIR)) {
				newitem->stype = F_DRAFT;
				folder->draft = newitem;
			} else if (!folder->queue && !strcmp(dirname, "." QUEUE_DIR)) {
				newitem->stype = F_QUEUE;
				folder->queue = newitem;
			} else if (!folder->trash && !strcmp(dirname, "." TRASH_DIR)) {
				newitem->stype = F_TRASH;
				folder->trash = newitem;
			}
		}
		g_free(dirname);
                build_tree(newitem->node, globbuf);
        }

	g_free(prefix);
}

static gint maildir_scan_tree(Folder *folder)
{
        FolderItem *rootitem, *inboxitem;
	GNode *rootnode, *inboxnode;
        glob_t globbuf;
	gchar *rootpath, *globpat;
        
        g_return_val_if_fail(folder != NULL, -1);

	if (!folder->node) {
		rootitem = folder_item_new(folder, folder->name, NULL);
		rootitem->folder = folder;
		rootnode = g_node_new(rootitem);
		folder->node = rootnode;
		rootitem->node = rootnode;
	} else {
		rootitem = FOLDER_ITEM(folder->node->data);
		rootnode = folder->node;
	}

	/* Add inbox folder */
	if (!folder->inbox) {
		inboxitem = folder_item_new(folder, "inbox", ".inbox");
		inboxitem->folder = folder;
		inboxitem->stype = F_INBOX;
		inboxnode = g_node_new(inboxitem);
		inboxitem->node = inboxnode;
		folder->inbox = inboxitem;
		g_node_append(rootnode, inboxnode);
	}

	rootpath = folder_item_get_path(rootitem);

	/* clear special folders to make sure we don't have invalid references
	   after remove_missing_folder_items */
	folder->outbox = NULL;
	folder->draft = NULL;
	folder->queue = NULL;
	folder->trash = NULL;

	debug_print("scanning tree %s\n", rootpath);
	maildir_create_tree(folder);
	remove_missing_folder_items(folder);

	globpat = g_strconcat(rootpath, G_DIR_SEPARATOR_S ".*", NULL);
	globbuf.gl_offs = 0;
	glob(globpat, 0, NULL, &globbuf);
	g_free(globpat);
	build_tree(rootnode, &globbuf);
	globfree(&globbuf);

	return 0;
}

static gchar *get_filename_for_msgdata(MessageData *msgdata)
{
	gchar *filename;

	if (msgdata->info[0])
		filename = g_strconcat(msgdata->dir, G_DIR_SEPARATOR_S, 
				       msgdata->uniq, ":", msgdata->info, NULL);
	else
		filename = g_strconcat(msgdata->dir, G_DIR_SEPARATOR_S, 
				       msgdata->uniq, NULL);

	return filename;
}

static MessageData *get_msgdata_for_filename(const gchar *filename)
{
	MessageData *msgdata;
	const gchar *tmpfilename;
	gchar **pathsplit, **namesplit;

	tmpfilename = strrchr(filename, G_DIR_SEPARATOR);
	if (tmpfilename == NULL || tmpfilename == filename)
		return NULL;

	tmpfilename--;
	while (tmpfilename > filename && tmpfilename[0] != G_DIR_SEPARATOR)
		tmpfilename--;
	if (tmpfilename[0] == G_DIR_SEPARATOR)
		tmpfilename++;

    	pathsplit = g_strsplit(tmpfilename, G_DIR_SEPARATOR_S, 2);
	if (pathsplit[1] == NULL) {
	        g_strfreev(pathsplit);
		return NULL;
	}

	namesplit = g_strsplit(pathsplit[1], ":", 2);

	msgdata = g_new0(MessageData, 1);

	msgdata->dir = g_strdup(pathsplit[0]);
	msgdata->uniq = g_strdup(namesplit[0]);
	if (namesplit[1] != NULL)
		msgdata->info = g_strdup(namesplit[1]);
	else
		msgdata->info = g_strdup("");

	g_strfreev(namesplit);
	g_strfreev(pathsplit);

	return msgdata;
}

static guint32 get_uid_for_filename(MaildirFolderItem *item, const gchar *filename)
{
	gchar *uniq, *info;
	MessageData *msgdata;
	guint32 uid;

	g_return_val_if_fail(item->db != NULL, 0);

	uniq = strrchr(filename, G_DIR_SEPARATOR);
	if (uniq == NULL) {
		return 0;
	}
	uniq++;

	Xstrdup_a(uniq, uniq, return 0);
	info = strchr(uniq, ':');
	if (info != NULL)
		*info++ = '\0';
	else
		info = "";

	msgdata = uiddb_get_entry_for_uniq(item->db, uniq);
	if (msgdata == NULL) {
		msgdata = get_msgdata_for_filename(filename);
		if (msgdata == NULL) {
			return 0;
		}
		msgdata->uid = uiddb_get_new_uid(item->db);

		uiddb_insert_entry(item->db, msgdata);
	} else if (strcmp(msgdata->info, info)) {
		uiddb_delete_entry(item->db, msgdata->uid);

		g_free(msgdata->info);
		msgdata->info = g_strdup(info);

		uiddb_insert_entry(item->db, msgdata);
	}

	uid = msgdata->uid;
	uiddb_free_msgdata(msgdata);

	return uid;
}

static MessageData *get_msgdata_for_uid(MaildirFolderItem *item, guint32 uid)
{
	MessageData *msgdata;
	gchar *path, *msgname, *filename;
	glob_t globbuf;

	g_return_val_if_fail(item->db != NULL, NULL);

	msgdata = uiddb_get_entry_for_uid(item->db, uid);
	if (msgdata == NULL) {
		return NULL;
	}
	path = maildir_item_get_path(FOLDER_ITEM(item)->folder, FOLDER_ITEM(item));

	msgname = get_filename_for_msgdata(msgdata);
	filename = g_strconcat(path, G_DIR_SEPARATOR_S, msgname, NULL);
	g_free(msgname);
	
	if (is_file_exist(filename)) {
		g_free(path);
		return msgdata;
	}

	debug_print("researching for %s\n", msgdata->uniq);
	/* delete old entry */
	g_free(filename);
	uiddb_delete_entry(item->db, uid);

	/* try to find file with same uniq and different info */
	filename = g_strconcat(path, G_DIR_SEPARATOR_S, DIR_NEW, G_DIR_SEPARATOR_S, msgdata->uniq, NULL);
	if (!is_file_exist(filename)) {
		g_free(filename);

		filename = g_strconcat(path, G_DIR_SEPARATOR_S, DIR_CUR, G_DIR_SEPARATOR_S, msgdata->uniq, ":*", NULL);
		glob(filename, 0, NULL, &globbuf);
		g_free(filename);

		g_free(path);
	
		filename = NULL;
		if (globbuf.gl_pathc > 0)
			filename = g_strdup(globbuf.gl_pathv[0]);
		globfree(&globbuf);
	}
	uiddb_free_msgdata(msgdata);
	msgdata = NULL;

	/* if found: update database and return new entry */
	if (filename != NULL) {
		debug_print("found %s\n", filename);
		
		msgdata = get_msgdata_for_filename(filename);
		msgdata->uid = uid;

		uiddb_insert_entry(item->db, msgdata);
	}

	return msgdata;
}

static gchar *get_filepath_for_msgdata(MaildirFolderItem *item, MessageData *msgdata)
{
	gchar *path, *msgname, *filename;

	path = maildir_item_get_path(FOLDER_ITEM(item)->folder, FOLDER_ITEM(item));
	msgname = get_filename_for_msgdata(msgdata);
	filename = g_strconcat(path, G_DIR_SEPARATOR_S, msgname, NULL);
	g_free(msgname);
	g_free(path);
	
	return filename;
}

static gchar *get_filepath_for_uid(MaildirFolderItem *item, guint32 uid)
{
	MessageData *msgdata;
	gchar *filename;

	g_return_val_if_fail(item->db != NULL, NULL);

	msgdata = get_msgdata_for_uid(item, uid);
	if (msgdata == NULL) {
		return NULL;
	}
	filename = get_filepath_for_msgdata(item, msgdata);
	uiddb_free_msgdata(msgdata);

	return filename;
}

static gint maildir_uid_compare(gconstpointer a, gconstpointer b)
{
	guint gint_a = GPOINTER_TO_INT(a);
	guint gint_b = GPOINTER_TO_INT(b);
	
	return (gint_a - gint_b);
}

static gint maildir_get_num_list(Folder *folder, FolderItem *item,
				 MsgNumberList ** list, gboolean *old_uids_valid)
{
	gchar *path, *globpattern;
	glob_t globbuf;
	int i;
	MsgNumberList * tail;

        g_return_val_if_fail(open_database(MAILDIR_FOLDERITEM(item)) == 0, -1);

	*old_uids_valid = TRUE;

	globbuf.gl_offs = 0;
	path = maildir_item_get_path(folder, item);

	globpattern = g_strconcat(path, G_DIR_SEPARATOR_S, DIR_CUR, G_DIR_SEPARATOR_S, "*", NULL);
	glob(globpattern, GLOB_NOSORT, NULL, &globbuf);
	g_free(globpattern);

	globpattern = g_strconcat(path, G_DIR_SEPARATOR_S, DIR_NEW, G_DIR_SEPARATOR_S, "*", NULL);
	glob(globpattern, GLOB_NOSORT | GLOB_APPEND, NULL, &globbuf);
	g_free(globpattern);

	g_free(path);

	tail = g_slist_last(*list);
	
	for (i = 0; i < globbuf.gl_pathc; i++) {
		guint32 uid;

		uid = get_uid_for_filename((MaildirFolderItem *) item, globbuf.gl_pathv[i]);
		if (uid != 0) {
			tail = g_slist_append(tail, GINT_TO_POINTER(uid));
			tail = g_slist_last(tail);
			if (!*list) *list = tail;
		}
	}
	globfree(&globbuf);

	*list = g_slist_sort(*list, maildir_uid_compare);

	uiddb_delete_entries_not_in_list(((MaildirFolderItem *) item)->db, *list);

	close_database(MAILDIR_FOLDERITEM(item));
	return g_slist_length(*list);
}

static MsgInfo *maildir_parse_msg(const gchar *file, FolderItem *item)
{
	MsgInfo *msginfo;
	MsgFlags flags;

	flags.perm_flags = MSG_NEW|MSG_UNREAD;
	flags.tmp_flags = 0;

	g_return_val_if_fail(item != NULL, NULL);
	g_return_val_if_fail(file != NULL, NULL);

	if (item->stype == F_QUEUE) {
		MSG_SET_TMP_FLAGS(flags, MSG_QUEUED);
	} else if (item->stype == F_DRAFT) {
		MSG_SET_TMP_FLAGS(flags, MSG_DRAFT);
	}

	msginfo = procheader_parse_file(file, flags, FALSE, FALSE);
	if (!msginfo) return NULL;

	msginfo->msgnum = atoi(file);
	msginfo->folder = item;

#if 0
	/* this is already done by procheader_parse_file */
	if (stat(file, &s) < 0) {
		FILE_OP_ERROR(file, "stat");
		msginfo->size = 0;
		msginfo->mtime = 0;
	} else {
		msginfo->size = (goffset)s.st_size;
		msginfo->mtime = s.st_mtime;
	}
#endif

	return msginfo;
}

static gboolean maildir_scan_required(Folder * folder, FolderItem * item) {
	gchar *path, *database;
	struct stat my_stat;
	time_t db_time;
	gboolean result = FALSE;

	path = maildir_item_get_path(FOLDER_ITEM(item)->folder, FOLDER_ITEM(item));
	Xstrcat_a(database, path, G_DIR_SEPARATOR_S "sylpheed_uid.db", return -1);
	if (lstat(database, &my_stat)) { goto OUTTAHERE; }
	db_time = my_stat.st_mtime;

	Xstrcat_a(database, path, G_DIR_SEPARATOR_S "new", return -1);
	if (lstat(database, &my_stat)) { goto OUTTAHERE; }
	result = my_stat.st_mtime > db_time;
	if (!result) {
		Xstrcat_a(database, path, G_DIR_SEPARATOR_S "cur", return -1);
		if (lstat(database, &my_stat)) { goto OUTTAHERE; }
		result = my_stat.st_mtime > db_time;
	}
OUTTAHERE:
	g_free(path);

	return result;
}

static MsgInfo *maildir_get_msginfo(Folder * folder,
				    FolderItem * item, gint num)
{
	MsgInfo *msginfo;
	gchar *file;

	g_return_val_if_fail(item != NULL, NULL);
	g_return_val_if_fail(num > 0, NULL);

	file = maildir_fetch_msg(folder, item, num);
	if (!file) return NULL;

	msginfo = maildir_parse_msg(file, item);
	if (msginfo)
		msginfo->msgnum = num;

	g_free(file);

	return msginfo;
}

static gchar *maildir_fetch_msg(Folder * folder, FolderItem * item,
				gint num)
{
	gchar *filename;
	
        g_return_val_if_fail(open_database(MAILDIR_FOLDERITEM(item)) == 0, NULL);
	filename = get_filepath_for_uid((MaildirFolderItem *) item, num);
	close_database(MAILDIR_FOLDERITEM(item));
	
	return filename;
}

static gchar *generate_uniq()
{
	gchar hostname[32], *strptr;
	static gint q = 1;
	struct timeval tv;

	gethostname(hostname, 32);
	hostname[31] = '\0';

	strptr = &hostname[0];
	while (*strptr != '\0') {
		if (*strptr == '/')
			*strptr = '\057';
		if (*strptr == ':')
			*strptr = '\072';
		strptr++;
	}

	gettimeofday(&tv, NULL);

	return g_strdup_printf("%d.P%dQ%dM%d.%s", (int) tv.tv_sec, getpid(), q++, (int) tv.tv_usec, hostname);
}

static gchar *get_infostr(MsgPermFlags permflags)
{
	if (permflags & MSG_NEW)
		return g_strdup("");

	return g_strconcat("2,",
		  permflags & MSG_MARKED    ? "F" : "",
		  permflags & MSG_FORWARDED ? "P" : "",
		  permflags & MSG_REPLIED   ? "R" : "",
		!(permflags & MSG_UNREAD)   ? "S" : "",
		NULL);
}

static gint add_file_to_maildir(MaildirFolderItem *item, const gchar *file, MsgFlags *flags)
{
	MessageData *msgdata;
	gchar *tmpname, *destname;
	gint uid = -1;

	g_return_val_if_fail(item != NULL, -1);
        g_return_val_if_fail(open_database(MAILDIR_FOLDERITEM(item)) == 0, -1);

	msgdata = g_new0(MessageData, 1);
	msgdata->uniq = generate_uniq();
	if (flags != NULL)
		msgdata->info = get_infostr(flags->perm_flags);
	else
		msgdata->info = g_strdup("");
	msgdata->uid = uiddb_get_new_uid(item->db);

	msgdata->dir = DIR_TMP;
	tmpname = get_filepath_for_msgdata(item, msgdata);

	if (flags != NULL)
		msgdata->dir = g_strdup(flags->perm_flags & MSG_NEW ? DIR_NEW : DIR_CUR);
	else
		msgdata->dir = g_strdup(DIR_NEW);

	if (copy_file(file, tmpname, FALSE) < 0) {
		goto exit;
	}

	destname = get_filepath_for_msgdata(item, msgdata);
	if (rename(tmpname, destname) < 0) {
		g_free(destname);
		goto exit;
	}

	uiddb_insert_entry(item->db, msgdata);

	uid = msgdata->uid;
	
 exit:
	uiddb_free_msgdata(msgdata);
	g_free(tmpname);
	close_database(MAILDIR_FOLDERITEM(item));
	return uid;
}

static gint maildir_add_msg(Folder *folder, FolderItem *_dest, const gchar *file, MsgFlags *flags)
{
	MaildirFolderItem *dest = MAILDIR_FOLDERITEM(_dest);

	g_return_val_if_fail(folder != NULL, -1);
	g_return_val_if_fail(dest != NULL, -1);
	g_return_val_if_fail(file != NULL, -1);

	return add_file_to_maildir(dest, file, flags);
}

static gint maildir_copy_msg(Folder *folder, FolderItem *dest, MsgInfo *msginfo)
{
	gchar *srcfile;
	gint ret = -1;
	gboolean delsrc = FALSE;

	g_return_val_if_fail(folder != NULL, -1);
	g_return_val_if_fail(dest != NULL, -1);
	g_return_val_if_fail(msginfo != NULL, -1);

	srcfile = procmsg_get_message_file(msginfo);
	if (srcfile == NULL)
		return -1;

	if ((MSG_IS_QUEUED(msginfo->flags) || MSG_IS_DRAFT(msginfo->flags))
	    && dest->stype != F_QUEUE && dest->stype != F_DRAFT) {
		gchar *tmpfile;

		tmpfile = get_tmp_file();
		if (procmsg_remove_special_headers(srcfile, tmpfile) != 0) {
			g_free(srcfile);
			g_free(tmpfile);
			return -1;
		}		
		g_free(srcfile);
		srcfile = tmpfile;
		delsrc = TRUE;
	}

	ret = add_file_to_maildir(MAILDIR_FOLDERITEM(dest), srcfile, &msginfo->flags);

	if (delsrc)
		unlink(srcfile);
	g_free(srcfile);

	return ret;
}

static gint maildir_remove_msg(Folder *folder, FolderItem *_item, gint num)
{
	MaildirFolderItem *item = MAILDIR_FOLDERITEM(_item);
	gchar *filename;
	gint ret;

	g_return_val_if_fail(folder != NULL, -1);
	g_return_val_if_fail(item != NULL, -1);
	g_return_val_if_fail(num > 0, -1);

        g_return_val_if_fail(open_database(MAILDIR_FOLDERITEM(item)) == 0, -1);
	
	filename = get_filepath_for_uid(item, num);
	if (filename == NULL) {
		ret = -1;
		goto close;
	}

	ret = unlink(filename);	
	if (ret == 0)
		uiddb_delete_entry(item->db, num);

	g_free(filename);
	
 close:
	close_database(MAILDIR_FOLDERITEM(item));
	return ret;
}

static void maildir_change_flags(Folder *folder, FolderItem *_item, MsgInfo *msginfo, MsgPermFlags newflags)
{
	MaildirFolderItem *item = MAILDIR_FOLDERITEM(_item);
	MessageData *msgdata;
	gchar *oldname, *newinfo, *newdir;
	gboolean renamefile = FALSE;

	g_return_if_fail(open_database(MAILDIR_FOLDERITEM(item)) == 0);

	msgdata = get_msgdata_for_uid(item, msginfo->msgnum);
	if (msgdata == NULL)
		goto fail;
	
	oldname = get_filepath_for_msgdata(item, msgdata);

	newinfo = get_infostr(newflags);
	if (strcmp(msgdata->info, newinfo)) {
		g_free(msgdata->info);
		msgdata->info = newinfo;
		renamefile = TRUE;
	} else
		g_free(newinfo);

	newdir = g_strdup(newflags & MSG_NEW ? DIR_NEW : DIR_CUR);
	if (strcmp(msgdata->dir, newdir)) {
		g_free(msgdata->dir);
		msgdata->dir = newdir;
		renamefile = TRUE;
	} else
		g_free(newdir);

	if (renamefile) {
		gchar *newname;

		newname = get_filepath_for_msgdata(item, msgdata);
		if (rename(oldname, newname) == 0) {
			uiddb_delete_entry(item->db, msgdata->uid);
			uiddb_insert_entry(item->db, msgdata);
			msginfo->flags.perm_flags = newflags;
		}
		g_free(newname);
	} else {
		msginfo->flags.perm_flags = newflags;
	}

	g_free(oldname);
	uiddb_free_msgdata(msgdata);
	
	close_database(MAILDIR_FOLDERITEM(item));
	
	if (renamefile) {
		MainWindow *mainwin = mainwindow_get_mainwindow();
		SummaryView *summaryview = mainwin->summaryview;
		gint displayed_msgnum = -1;
		if (summaryview->displayed)
			displayed_msgnum = summary_get_msgnum(summaryview,
						      summaryview->displayed);
		if (displayed_msgnum == msginfo->msgnum
		&& summaryview->folder_item == msginfo->folder)
			messageview_show(
				summaryview->messageview, 
				msginfo,
				summaryview->messageview->all_headers);
	}
	
	return;
	
 fail:
	close_database(MAILDIR_FOLDERITEM(item));
}

static gboolean setup_new_folder(const gchar * path, gboolean subfolder)
{
	gchar *curpath, *newpath, *tmppath, *maildirfolder;
	gboolean failed = FALSE;

	g_return_val_if_fail(path != NULL, TRUE);

	curpath = g_strconcat(path, G_DIR_SEPARATOR_S, DIR_CUR, NULL);
	newpath = g_strconcat(path, G_DIR_SEPARATOR_S, DIR_NEW, NULL);
	tmppath = g_strconcat(path, G_DIR_SEPARATOR_S, DIR_TMP, NULL);

	if (!is_dir_exist(path))
		if (mkdir(path, DIR_PERMISSION) != 0)
			failed = TRUE;
	if (!is_dir_exist(curpath))
		if (mkdir(curpath, DIR_PERMISSION) != 0)
			failed = TRUE;
	if (!is_dir_exist(newpath))
		if (mkdir(newpath, DIR_PERMISSION) != 0)
			failed = TRUE;
	if (!is_dir_exist(tmppath))
		if (mkdir(tmppath, DIR_PERMISSION) != 0)
			failed = TRUE;

	if (subfolder) {
		int res;
		maildirfolder = g_strconcat(path, G_DIR_SEPARATOR_S, "maildirfolder", NULL);
		res = open(maildirfolder, O_WRONLY | O_CREAT | O_NONBLOCK | O_NOCTTY,
			   S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
		if (res != -1)
			close(res);
		else
			failed = TRUE;
	}

	if (failed) {
		rmdir(tmppath);
		rmdir(newpath);
		rmdir(curpath);
		rmdir(path);
	}
	    
	g_free(tmppath);
	g_free(newpath);
	g_free(curpath);

	return failed;
}

static FolderItem *maildir_create_folder(Folder * folder,
					 FolderItem * parent,
					 const gchar * name)
{
	gchar *folder_path, *path, *real_path;
	FolderItem *newitem = NULL;
	gboolean failed = FALSE;

	g_return_val_if_fail(folder != NULL, NULL);
	g_return_val_if_fail(parent != NULL, NULL);
	g_return_val_if_fail(name != NULL, NULL);

	folder_path = g_strdup(LOCAL_FOLDER(folder)->rootpath);
	g_return_val_if_fail(folder_path != NULL, NULL);

	if (g_path_is_absolute(folder_path)) {
		path = g_strconcat(folder_path, G_DIR_SEPARATOR_S,
                                   parent->path != NULL ? parent->path : "",
				   ".", name, NULL);
        } else {
                path = g_strconcat(get_home_dir(), G_DIR_SEPARATOR_S,
                                   folder_path, G_DIR_SEPARATOR_S,
                                   parent->path != NULL ? parent->path : "",
				   ".", name, NULL);
        }
	g_free(folder_path);

	debug_print("creating new maildir folder: %s\n", path);

	real_path = filename_from_utf8(path);
	g_free(path);

	failed = setup_new_folder(real_path, TRUE);
	g_free(real_path);

	if (failed)
		return NULL;

	path = g_strconcat((parent->path != NULL) ? parent->path : "", ".", name, NULL);
	newitem = folder_item_new(folder, name, path);
	folder_item_append(parent, newitem);
	g_free(path);

	return newitem;
}

static gint maildir_create_tree(Folder *folder)
{
	gchar *rootpath, *real_rootpath, *folder_path, *path;

	g_return_val_if_fail(folder != NULL, -1);

	folder_path = g_strdup(LOCAL_FOLDER(folder)->rootpath);
	g_return_val_if_fail(folder_path != NULL, -1);

	if (g_path_is_absolute(folder_path)) {
		rootpath = g_strdup(folder_path);
        } else {
                rootpath = g_strconcat(get_home_dir(), G_DIR_SEPARATOR_S,
                                   folder_path, NULL);
        }
	g_free(folder_path);

	real_rootpath = filename_from_utf8(rootpath);
	g_free(rootpath);

	debug_print("creating new maildir tree: %s\n", real_rootpath);
	if (!is_dir_exist(real_rootpath)) {
		if (is_file_exist(real_rootpath)) {
			g_warning("File `%s' already exists.\n"
				    "Can't create folder.", real_rootpath);
			return -1;
		}
		if (make_dir_hier(real_rootpath) < 0)
			return -1;
	}

	if (setup_new_folder(real_rootpath, FALSE)) { /* create INBOX */
		g_free(real_rootpath);
		return -1;
	}
	path = g_strconcat(real_rootpath, G_DIR_SEPARATOR_S, "." OUTBOX_DIR, NULL);
	if (setup_new_folder(path, TRUE)) {
		g_free(path);
	g_free(real_rootpath);
			return -1;
	}
	g_free(path);
	path = g_strconcat(real_rootpath, G_DIR_SEPARATOR_S, "." QUEUE_DIR, NULL);
	if (setup_new_folder(path, TRUE)) {
		g_free(path);
		g_free(real_rootpath);
			return -1;
	}
	g_free(path);
	path = g_strconcat(real_rootpath, G_DIR_SEPARATOR_S, "." DRAFT_DIR, NULL);
	if (setup_new_folder(path, TRUE)) {
		g_free(path);
		g_free(real_rootpath);
			return -1;
	}
	g_free(path);
	path = g_strconcat(real_rootpath, G_DIR_SEPARATOR_S, "." TRASH_DIR, NULL);
	if (setup_new_folder(path, TRUE)) {
		g_free(path);
		g_free(real_rootpath);
			return -1;
	}
	g_free(path);
	g_free(real_rootpath);

	return 0;
}

static gboolean remove_missing_folder_items_func(GNode *node, gpointer data)
{
	FolderItem *item;
	gchar *path;

	g_return_val_if_fail(node->data != NULL, FALSE);

	if (G_NODE_IS_ROOT(node))
		return FALSE;

	item = FOLDER_ITEM(node->data);

	if (item->stype == F_INBOX)
		return FALSE;

	path = folder_item_get_path(item);
	if (!is_dir_exist(path)) {
		debug_print("folder '%s' not found. removing...\n", path);
		folder_item_remove(item);
	}
	g_free(path);

	return FALSE;
}

static void remove_missing_folder_items(Folder *folder)
{
	g_return_if_fail(folder != NULL);

	debug_print("searching missing folders...\n");

	g_node_traverse(folder->node, G_POST_ORDER, G_TRAVERSE_ALL, -1,
			remove_missing_folder_items_func, folder);
}

static gboolean remove_folder_func(GNode *node, gpointer data)
{
	FolderItem *item;
	gchar *path;

	g_return_val_if_fail(node->data != NULL, FALSE);

	if (G_NODE_IS_ROOT(node))
		return FALSE;

	item = FOLDER_ITEM(node->data);

	if (item->stype != F_NORMAL)
		return FALSE;

	path = folder_item_get_path(item);
	debug_print("removing directory %s\n", path);
	if (remove_dir_recursive(path) < 0) {
		g_warning("can't remove directory `%s'\n", path);
		g_free(path);
		*(gint*)data = -1;
		return TRUE;
	}
	g_free(path);

	folder_item_remove(item);

	return FALSE;
}

static gint maildir_remove_folder(Folder *folder, FolderItem *item)
{
	gint res=0;

	g_return_val_if_fail(folder != NULL, -1);
	g_return_val_if_fail(item != NULL, -1);
	g_return_val_if_fail(item->path != NULL, -1);
	g_return_val_if_fail(item->stype == F_NORMAL, -1);

	debug_print("removing folder %s\n", item->path);

	g_node_traverse(item->node, G_POST_ORDER, G_TRAVERSE_ALL, -1,
			remove_folder_func, &res);

	return res;
}

struct RenameData
{
	gint	 oldprefixlen;
	gchar	*newprefix;
};

static gboolean rename_folder_func(GNode *node, gpointer data)
{
	FolderItem *item;
	gchar *oldpath, *newpath, *newitempath;
	gchar *suffix, *real_path, *real_rootpath;
	struct RenameData *renamedata = data;

	g_return_val_if_fail(node->data != NULL, FALSE);

	if (G_NODE_IS_ROOT(node))
		return FALSE;

	item = FOLDER_ITEM(node->data);

	if (item->stype != F_NORMAL)
		return FALSE;

	real_rootpath = filename_from_utf8(LOCAL_FOLDER(item->folder)->rootpath);
	real_path = filename_from_utf8(item->path);
	suffix = real_path + renamedata->oldprefixlen;

	oldpath = folder_item_get_path(item);
	newitempath = g_strconcat(renamedata->newprefix, suffix, NULL);
	newpath = g_strconcat(real_rootpath, G_DIR_SEPARATOR_S, newitempath, NULL);
	g_free(real_path);
	g_free(real_rootpath);

	debug_print("renaming directory %s to %s\n", oldpath, newpath);

	if (rename(oldpath, newpath) < 0) {
		FILE_OP_ERROR(oldpath, "rename");
	} else {
		g_free(item->path);
		item->path = filename_to_utf8(newitempath);
	}

	g_free(newitempath);
	g_free(oldpath);
	g_free(newpath);

	return FALSE;
}

static gint maildir_rename_folder(Folder *folder, FolderItem *item,
			     const gchar *name)
{
	struct RenameData renamedata;
	gchar *p, *real_path, *real_name;

	g_return_val_if_fail(folder != NULL, -1);
	g_return_val_if_fail(item != NULL, -1);
	g_return_val_if_fail(item->path != NULL, -1);
	g_return_val_if_fail(name != NULL, -1);

	debug_print("renaming folder %s to %s\n", item->path, name);

	g_free(item->name);
	item->name = g_strdup(name);

	real_path = filename_from_utf8(item->path);
	real_name = filename_from_utf8(name);

	renamedata.oldprefixlen = strlen(real_path);
	p = strrchr(real_path, '.');
	if (p)
		p = g_strndup(real_path, p - real_path + 1);
	else
		p = g_strdup(".");
	renamedata.newprefix = g_strconcat(p, real_name, NULL);
	g_free(p);
	g_free(real_name);
	g_free(real_path);

	g_node_traverse(item->node, G_PRE_ORDER, G_TRAVERSE_ALL, -1,
			rename_folder_func, &renamedata);

	g_free(renamedata.newprefix);

	return 0;
}

static gint get_flags_for_msgdata(MessageData *msgdata, MsgPermFlags *flags)
{
	gint	i;

	g_return_val_if_fail(msgdata != NULL, -1);
	g_return_val_if_fail(msgdata->info != NULL, -1);

	if ((msgdata->info[0] != '2') && (msgdata->info[1] != ','))
		return -1;

	*flags = MSG_UNREAD;
	for (i = 2; i < strlen(msgdata->info); i++) {
		switch (msgdata->info[i]) {
			case 'F':
				  *flags |= MSG_MARKED;
				  break;
			case 'P':
				  *flags |= MSG_FORWARDED;
				  break;
			case 'R':
				  *flags |= MSG_REPLIED;
				  break;
			case 'S':
				  *flags &= ~MSG_UNREAD;
				  break;
		}
	}

	return 0;
}

static gint maildir_get_flags (Folder *folder,  FolderItem *item,
			       MsgInfoList *msglist, GHashTable *msgflags)
{
	MsgInfoList	*elem;
	MsgInfo		*msginfo;
	MessageData	*msgdata;
	MsgPermFlags	flags;

	g_return_val_if_fail(folder != NULL, -1);
	g_return_val_if_fail(item != NULL, -1);
	g_return_val_if_fail(msglist != NULL, -1);
	g_return_val_if_fail(msgflags != NULL, -1);
	g_return_val_if_fail(open_database(MAILDIR_FOLDERITEM(item)) == 0, -1);

	for (elem = msglist; elem != NULL; elem = g_slist_next(elem)) {
		msginfo = (MsgInfo*) elem->data;
		msgdata = uiddb_get_entry_for_uid(MAILDIR_FOLDERITEM(item)->db, msginfo->msgnum);
		if (msgdata == NULL)
			break;

		if (get_flags_for_msgdata(msgdata, &flags) < 0)
			break;

		flags = flags | (msginfo->flags.perm_flags & 
			~(MSG_MARKED | MSG_FORWARDED | MSG_REPLIED | MSG_UNREAD | ((flags & MSG_UNREAD) == 0 ? MSG_NEW : 0)));
		g_hash_table_insert(msgflags, msginfo, GINT_TO_POINTER(flags));

		uiddb_free_msgdata(msgdata);
	}

	close_database(MAILDIR_FOLDERITEM(item));
	return 0;
}

static gchar *filename_from_utf8(const gchar *path)
{
	gchar *real_path = g_filename_from_utf8(path, -1, NULL, NULL, NULL);

	if (!real_path) {
		g_warning("filename_from_utf8: failed to convert character set\n");
		real_path = g_strdup(path);
	}

	return real_path;
}

static gchar *filename_to_utf8(const gchar *path)
{
	gchar *utf8path = g_filename_to_utf8(path, -1, NULL, NULL, NULL);
	if (!utf8path) {
		g_warning("filename_to_utf8: failed to convert character set\n");
		utf8path = g_strdup(path);
	}

	return utf8path;
}
