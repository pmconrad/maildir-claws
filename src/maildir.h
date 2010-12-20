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

#ifndef MAILDIR_H
#define MAILDIR_H 1

#include "folder.h"

#define DIR_CUR         "cur" /* Sub directory cur */
#define DIR_NEW         "new" /* Sub directory new */
#define DIR_TMP         "tmp" /* Sub directory tmp */

#define DIR_PERMISSION  0700 /* Permission of maildir root directory */

FolderClass *maildir_get_class();

#endif /* MAILDIR_H */
