/*
 * Copyright (c) 2017 Michael Kuhn
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

#define _POSIX_C_SOURCE 200809L

#include <julea-config.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <gmodule.h>

#include <mongoc.h>

#include <jbackend.h>
#include <jtrace-internal.h>

mongoc_client_t* backend_connection;

static gchar* backend_host = NULL;
static gchar* backend_database = NULL;

static bson_t backend_value[1];

static
gboolean
backend_create (gchar const* namespace, gchar const* key, bson_t const* value)
{
	gboolean ret = FALSE;

	bson_t document[1];
	bson_t index[1];
	bson_t reply[1];
	mongoc_bulk_operation_t* bulk_op;
	mongoc_collection_t* m_collection;
	mongoc_index_opt_t m_index_opt[1];

	j_trace_enter(G_STRFUNC);

	/* FIXME */
	//write_concern = mongoc_write_concern_new();
	//j_helper_set_write_concern(write_concern, j_batch_get_semantics(batch));

	bson_init(document);
	bson_append_utf8(document, "key", -1, key, -1);
	bson_append_document(document, "value", -1, value);

	bson_init(index);
	bson_append_int32(index, "key", -1, 1);
	//bson_finish(index);

	mongoc_index_opt_init(m_index_opt);
	m_index_opt->unique = TRUE;

	/* FIXME cache */
	m_collection = mongoc_client_get_collection(backend_connection, backend_database, namespace);
	mongoc_collection_create_index(m_collection, index, m_index_opt, NULL);

	bulk_op = mongoc_collection_create_bulk_operation(m_collection, FALSE, NULL /*write_concern*/);

	mongoc_bulk_operation_insert(bulk_op, document);

	ret = mongoc_bulk_operation_execute(bulk_op, reply, NULL);

	/*
	if (!ret)
	{
		bson_t error[1];

		mongo_cmd_get_last_error(mongo_connection, store->name, error);
		bson_print(error);
		bson_destroy(error);
	}
	*/

	mongoc_bulk_operation_destroy(bulk_op);
	bson_destroy(reply);

	bson_destroy(index);
	bson_destroy(document);

	j_trace_leave(G_STRFUNC);

	return ret;
}

static
gboolean
backend_delete (gchar const* namespace, gchar const* key)
{
	gboolean ret = FALSE;

	bson_t document[1];
	bson_t reply[1];
	mongoc_bulk_operation_t* bulk_op;
	mongoc_collection_t* m_collection;

	j_trace_enter(G_STRFUNC);

	bson_init(document);
	bson_append_utf8(document, "key", -1, key, -1);

	/* FIXME */
	//write_concern = mongoc_write_concern_new();
	//j_helper_set_write_concern(write_concern, j_batch_get_semantics(batch));

	m_collection = mongoc_client_get_collection(backend_connection, backend_database, namespace);
	bulk_op = mongoc_collection_create_bulk_operation(m_collection, FALSE, NULL /*write_concern*/);

	mongoc_bulk_operation_remove(bulk_op, document);

	ret = mongoc_bulk_operation_execute(bulk_op, reply, NULL);

	mongoc_bulk_operation_destroy(bulk_op);
	bson_destroy(reply);

	bson_destroy(document);

	j_trace_leave(G_STRFUNC);

	return ret;
}

static
gboolean
backend_get (gchar const* namespace, gchar const* key, bson_t* value)
{
	gboolean ret = FALSE;

	bson_t document[1];
	bson_t opts[1];
	bson_t const* result;
	mongoc_collection_t* m_collection;
	mongoc_cursor_t* cursor;

	j_trace_enter(G_STRFUNC);

	bson_init(document);
	bson_append_utf8(document, "key", -1, key, -1);

	bson_init(opts);
	bson_append_int32(opts, "limit", -1, 1);

	/* FIXME */
	//write_concern = mongoc_write_concern_new();
	//j_helper_set_write_concern(write_concern, j_batch_get_semantics(batch));

	m_collection = mongoc_client_get_collection(backend_connection, backend_database, namespace);
	cursor = mongoc_collection_find_with_opts(m_collection, document, opts, NULL);

	while (mongoc_cursor_next(cursor, &result))
	{
		ret = TRUE;
		bson_copy_to(result, value);
		break;
	}

	bson_destroy(opts);
	bson_destroy(document);

	j_trace_leave(G_STRFUNC);

	return ret;
}

static
gboolean
backend_get_all (gchar const* namespace, gpointer* data)
{
	gboolean ret = FALSE;

	bson_t document[1];
	mongoc_collection_t* m_collection;
	mongoc_cursor_t* cursor;

	j_trace_enter(G_STRFUNC);

	bson_init(document);

	/* FIXME */
	//write_concern = mongoc_write_concern_new();
	//j_helper_set_write_concern(write_concern, j_batch_get_semantics(batch));

	m_collection = mongoc_client_get_collection(backend_connection, backend_database, namespace);
	cursor = mongoc_collection_find_with_opts(m_collection, document, NULL, NULL);

	bson_destroy(document);

	if (cursor != NULL)
	{
		ret = TRUE;
		*data = cursor;
	}

	j_trace_leave(G_STRFUNC);

	return ret;
}

static
gboolean
backend_iterate (gpointer data, bson_t const** result)
{
	bson_t const* tmp_result;
	bson_iter_t iter;
	mongoc_cursor_t* cursor = data;

	gboolean ret = FALSE;

	j_trace_enter(G_STRFUNC);

	ret = mongoc_cursor_next(cursor, &tmp_result);

	/* FIXME */
	if (ret && bson_iter_init_find(&iter, tmp_result, "value") && bson_iter_type(&iter) == BSON_TYPE_DOCUMENT)
	{
		bson_value_t const* value;

		value = bson_iter_value(&iter);
		bson_init_static(backend_value, value->value.v_doc.data, value->value.v_doc.data_len);

		*result = backend_value;
	}

	j_trace_leave(G_STRFUNC);

	return ret;
}

static
gboolean
backend_init (gchar const* path)
{
	mongoc_uri_t* uri;

	gboolean ret = FALSE;
	gchar** split;

	j_trace_enter(G_STRFUNC);

	mongoc_init();
	/* FIXME */
	//mongoc_log_set_handler(j_common_mongoc_log_handler, NULL);

	split = g_strsplit(path, ":", 0);

	/* FIXME error handling */
	backend_host = g_strdup(split[0]);
	backend_database = g_strdup(split[1]);

	g_strfreev(split);

	uri = mongoc_uri_new_for_host_port(backend_host, 27017);
	backend_connection = mongoc_client_new_from_uri(uri);

	if (backend_connection != NULL)
	{
		ret = mongoc_client_get_server_status(backend_connection, NULL, NULL, NULL);
	}

	if (!ret)
	{
		g_critical("Can not connect to MongoDB %s.", backend_host);
	}

	j_trace_leave(G_STRFUNC);

	return TRUE;
}

static
void
backend_fini (void)
{
	j_trace_enter(G_STRFUNC);

	mongoc_client_destroy(backend_connection);

	g_free(backend_database);
	g_free(backend_host);

	mongoc_cleanup();

	j_trace_leave(G_STRFUNC);
}

static
JBackend mongodb_backend = {
	.type = J_BACKEND_TYPE_META,
	.u.meta = {
		.init = backend_init,
		.fini = backend_fini,
		.thread_init = NULL,
		.thread_fini = NULL,
		.create = backend_create,
		.delete = backend_delete,
		.get = backend_get,
		.get_all = backend_get_all,
		.iterate = backend_iterate
	}
};

G_MODULE_EXPORT
JBackend*
backend_info (JBackendType type)
{
	JBackend* backend = NULL;

	j_trace_enter(G_STRFUNC);

	if (type == J_BACKEND_TYPE_META)
	{
		backend = &mongodb_backend;
	}

	j_trace_leave(G_STRFUNC);

	return backend;
}