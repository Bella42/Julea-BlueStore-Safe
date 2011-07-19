/*
 * Copyright (c) 2010-2011 Michael Kuhn
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/**
 * \file
 **/

#ifndef H_ITEM
#define H_ITEM

struct JItem;

typedef struct JItem JItem;

#include <glib.h>

#include <jcollection.h>
#include <jitem-status.h>
#include <joperation.h>
#include <jsemantics.h>

JItem* j_item_new (JCollection*, const gchar*);
JItem* j_item_ref (JItem*);
void j_item_unref (JItem*);

const gchar* j_item_get_name (JItem*);

JItemStatus* j_item_get_status (JItem*);
void j_item_set_status (JItem*, JItemStatus*);

JSemantics* j_item_get_semantics (JItem*);
void j_item_set_semantics (JItem*, JSemantics*);

void j_item_create (JItem*, JOperation*);
void j_item_get (JItem*, JOperation*);

void j_item_read (JItem*, gpointer, guint64, guint64, guint64*, JOperation*);
void j_item_write (JItem*, gconstpointer, guint64, guint64, guint64*, JOperation*);

/*
		private:
			void IsInitialized (bool);

			bool m_initialized;
*/

#endif