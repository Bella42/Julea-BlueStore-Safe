/*
 * Copyright (c) 2010-2013 Michael Kuhn
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

#include <julea-config.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <glib-unix.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <gmodule.h>

#include <string.h>

#include <julea-internal.h>
#include <jconfiguration-internal.h>
#include <jconnection-internal.h>
#include <jhelper-internal.h>
#include <jmemory-chunk-internal.h>
#include <jmessage-internal.h>
#include <jstatistics-internal.h>
#include <jtrace-internal.h>

#include "backend/backend.h"

static JStatistics* jd_statistics;

G_LOCK_DEFINE_STATIC(jd_statistics);

static
gboolean
jd_signal (gpointer data)
{
	GMainLoop* main_loop = data;

	if (g_main_loop_is_running(main_loop))
	{
		g_main_loop_quit(main_loop);
	}

	return FALSE;
}

static
JBackendItem*
jd_create_file (GHashTable* hash_table, gchar const* store, gchar const* collection, gchar const* item)
{
	JBackendItem* file;
	gchar* key;

	key = g_strdup_printf("%s.%s.%s", store, collection, item);
	file = g_slice_new(JBackendItem);

	if (!jd_backend_create(file, store, collection, item))
	{
		goto error;
	}

	g_hash_table_insert(hash_table, key, file);

	return file;

error:
	g_free(key);
	g_slice_free(JBackendItem, file);

	return NULL;
}

static
JBackendItem*
jd_open_file (GHashTable* hash_table, gchar const* store, gchar const* collection, gchar const* item)
{
	JBackendItem* file;
	gchar* key;

	key = g_strdup_printf("%s.%s.%s", store, collection, item);

	if ((file = g_hash_table_lookup(hash_table, key)) == NULL)
	{
		file = g_slice_new(JBackendItem);

		if (!jd_backend_open(file, store, collection, item))
		{
			goto error;
		}

		g_hash_table_insert(hash_table, key, file);
	}
	else
	{
		g_free(key);
	}

	return file;

error:
	g_free(key);
	g_slice_free(JBackendItem, file);

	return NULL;
}

static
void
jd_close_file (GHashTable* hash_table, gchar const* store, gchar const* collection, gchar const* item)
{
	gchar* key;

	key = g_strdup_printf("%s.%s.%s", store, collection, item);
	g_hash_table_remove(hash_table, key);
	g_free(key);
}

static
void
jd_file_hash_free (gpointer data)
{
	JBackendItem* file = data;

	jd_backend_close(file);

	g_slice_free(JBackendItem, file);
}

#if 0
static guint jd_thread_num = 0;
#endif

static
gboolean
jd_on_run (GThreadedSocketService* service, GSocketConnection* connection, GObject* source_object, gpointer user_data)
{
	JMemoryChunk* memory_chunk;
	JMessage* message;
	JStatistics* statistics;
	GHashTable* files;
	GInputStream* input;
	GOutputStream* output;

	(void)service;
	(void)source_object;
	(void)user_data;

	j_trace_enter(G_STRFUNC);

	j_helper_set_nodelay(connection, TRUE);

	statistics = j_statistics_new(TRUE);
	memory_chunk = j_memory_chunk_new(J_STRIPE_SIZE);

	if (jd_backend_thread_init != NULL)
	{
		/* FIXME return value */
		jd_backend_thread_init();
	}

	message = j_message_new(J_MESSAGE_NONE, 0);
	input = g_io_stream_get_input_stream(G_IO_STREAM(connection));
	output = g_io_stream_get_output_stream(G_IO_STREAM(connection));
	files = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, jd_file_hash_free);

	while (j_message_read(message, input))
	{
		JBackendItem* bf;
		gchar const* store;
		gchar const* collection;
		gchar const* item;
		guint32 operation_count;
		JMessageType type_modifier;
		guint i;

		operation_count = j_message_get_count(message);
		type_modifier = j_message_get_type_modifier(message);

		switch (j_message_get_type(message))
		{
			case J_MESSAGE_NONE:
				break;
			case J_MESSAGE_CREATE:
				{
					store = j_message_get_string(message);
					collection = j_message_get_string(message);

					for (i = 0; i < operation_count; i++)
					{
						item = j_message_get_string(message);

						if (jd_create_file(files, store, collection, item) != NULL)
						{
							j_statistics_add(statistics, J_STATISTICS_FILES_CREATED, 1);
						}
					}
				}
				break;
			case J_MESSAGE_DELETE:
				{
					JMessage* reply = NULL;

					store = j_message_get_string(message);
					collection = j_message_get_string(message);

					if (type_modifier & J_MESSAGE_SAFETY_NETWORK)
					{
						reply = j_message_new_reply(message);
					}

					for (i = 0; i < operation_count; i++)
					{
						item = j_message_get_string(message);

						bf = jd_open_file(files, store, collection, item);

						if (bf != NULL)
						{
							jd_backend_delete(bf);
							jd_close_file(files, store, collection, item);
							j_statistics_add(statistics, J_STATISTICS_FILES_DELETED, 1);
						}

						if (reply != NULL)
						{
							j_message_add_operation(reply, 0);
						}
					}

					if (reply != NULL)
					{
						j_message_write(reply, output);
						j_message_unref(reply);
					}
				}
				break;
			case J_MESSAGE_READ:
				{
					JMessage* reply;

					store = j_message_get_string(message);
					collection = j_message_get_string(message);
					item = j_message_get_string(message);

					reply = j_message_new_reply(message);

					bf = jd_open_file(files, store, collection, item);

					for (i = 0; i < operation_count; i++)
					{
						gchar* buf;
						guint64 length;
						guint64 offset;
						guint64 bytes_read = 0;

						length = j_message_get_8(message);
						offset = j_message_get_8(message);

						buf = j_memory_chunk_get(memory_chunk, length);

						if (buf == NULL)
						{
							// FIXME ugly
							j_message_write(reply, output);
							j_message_unref(reply);

							reply = j_message_new_reply(message);

							j_memory_chunk_reset(memory_chunk);
							buf = j_memory_chunk_get(memory_chunk, length);
						}

						jd_backend_read(bf, buf, length, offset, &bytes_read);
						j_statistics_add(statistics, J_STATISTICS_BYTES_READ, bytes_read);

						j_message_add_operation(reply, sizeof(guint64));
						j_message_append_8(reply, &bytes_read);

						if (bytes_read > 0)
						{
							j_message_add_send(reply, buf, bytes_read);
						}

						j_statistics_add(statistics, J_STATISTICS_BYTES_SENT, bytes_read);
					}

					//jd_backend_close(bf);

					j_message_write(reply, output);
					j_message_unref(reply);

					j_memory_chunk_reset(memory_chunk);
				}
				break;
			case J_MESSAGE_WRITE:
				{
					JMessage* reply = NULL;
					gchar* buf;
					guint64 bytes_written;
					guint64 merge_length = 0;
					guint64 merge_offset = 0;

					if (type_modifier & J_MESSAGE_SAFETY_NETWORK)
					{
						reply = j_message_new_reply(message);
					}

					store = j_message_get_string(message);
					collection = j_message_get_string(message);
					item = j_message_get_string(message);

					/* Guaranteed to work, because memory_chunk is not shared. */
					buf = j_memory_chunk_get(memory_chunk, J_STRIPE_SIZE);
					g_assert(buf != NULL);

					bf = jd_open_file(files, store, collection, item);

					for (i = 0; i < operation_count; i++)
					{
						guint64 length;
						guint64 offset;

						length = j_message_get_8(message);
						offset = j_message_get_8(message);

						/* Check whether we can merge two consecutive operations. */
						if (merge_length > 0 && merge_offset + merge_length == offset && merge_length + length <= J_STRIPE_SIZE)
						{
							merge_length += length;
						}
						else if (merge_length > 0)
						{
							g_input_stream_read_all(input, buf, merge_length, NULL, NULL, NULL);
							j_statistics_add(statistics, J_STATISTICS_BYTES_RECEIVED, merge_length);

							jd_backend_write(bf, buf, merge_length, merge_offset, &bytes_written);
							j_statistics_add(statistics, J_STATISTICS_BYTES_WRITTEN, bytes_written);

							merge_length = 0;
							merge_offset = 0;
						}

						if (merge_length == 0)
						{
							merge_length = length;
							merge_offset = offset;
						}

						if (reply != NULL)
						{
							// FIXME the reply is faked (length should be bytes_written)
							j_message_add_operation(reply, sizeof(guint64));
							j_message_append_8(reply, &length);
						}
					}

					if (merge_length > 0)
					{
						g_input_stream_read_all(input, buf, merge_length, NULL, NULL, NULL);
						j_statistics_add(statistics, J_STATISTICS_BYTES_RECEIVED, merge_length);

						jd_backend_write(bf, buf, merge_length, merge_offset, &bytes_written);
						j_statistics_add(statistics, J_STATISTICS_BYTES_WRITTEN, bytes_written);
					}

					if (type_modifier & J_MESSAGE_SAFETY_STORAGE)
					{
						jd_backend_sync(bf);
						j_statistics_add(statistics, J_STATISTICS_SYNC, 1);
					}

					//jd_backend_close(bf);

					if (reply != NULL)
					{
						j_message_write(reply, output);
						j_message_unref(reply);
					}

					j_memory_chunk_reset(memory_chunk);
				}
				break;
			case J_MESSAGE_STATUS:
				{
					JMessage* reply;

					reply = j_message_new_reply(message);

					store = j_message_get_string(message);
					collection = j_message_get_string(message);

					for (i = 0; i < operation_count; i++)
					{
						guint count = 0;
						guint32 flags;
						gint64 modification_time = 0;
						guint64 size = 0;

						item = j_message_get_string(message);
						flags = j_message_get_4(message);

						bf = jd_open_file(files, store, collection, item);

						if (jd_backend_status(bf, flags, &modification_time, &size))
						{
							j_statistics_add(statistics, J_STATISTICS_FILES_STATED, 1);
						}

						if (flags & J_ITEM_STATUS_MODIFICATION_TIME)
						{
							count++;
						}

						if (flags & J_ITEM_STATUS_SIZE)
						{
							count++;
						}

						j_message_add_operation(reply, count * sizeof(guint64));

						if (flags & J_ITEM_STATUS_MODIFICATION_TIME)
						{
							j_message_append_8(reply, &modification_time);
						}

						if (flags & J_ITEM_STATUS_SIZE)
						{
							j_message_append_8(reply, &size);
						}

						//jd_backend_close(bf);
					}

					j_message_write(reply, output);
					j_message_unref(reply);
				}
				break;
			case J_MESSAGE_STATISTICS:
				{
					JMessage* reply;
					JStatistics* r_statistics;
					gchar get_all;
					guint64 value;

					get_all = j_message_get_1(message);
					r_statistics = (get_all == 0) ? statistics : jd_statistics;

					if (get_all != 0)
					{
						G_LOCK(jd_statistics);
						/* FIXME add statistics of all threads */
					}

					reply = j_message_new_reply(message);
					j_message_add_operation(reply, 8 * sizeof(guint64));

					value = j_statistics_get(r_statistics, J_STATISTICS_FILES_CREATED);
					j_message_append_8(reply, &value);
					value = j_statistics_get(r_statistics, J_STATISTICS_FILES_DELETED);
					j_message_append_8(reply, &value);
					value = j_statistics_get(r_statistics, J_STATISTICS_FILES_STATED);
					j_message_append_8(reply, &value);
					value = j_statistics_get(r_statistics, J_STATISTICS_SYNC);
					j_message_append_8(reply, &value);
					value = j_statistics_get(r_statistics, J_STATISTICS_BYTES_READ);
					j_message_append_8(reply, &value);
					value = j_statistics_get(r_statistics, J_STATISTICS_BYTES_WRITTEN);
					j_message_append_8(reply, &value);
					value = j_statistics_get(r_statistics, J_STATISTICS_BYTES_RECEIVED);
					j_message_append_8(reply, &value);
					value = j_statistics_get(r_statistics, J_STATISTICS_BYTES_SENT);
					j_message_append_8(reply, &value);

					if (get_all != 0)
					{
						G_UNLOCK(jd_statistics);
					}

					j_message_write(reply, output);
					j_message_unref(reply);
				}
				break;
#if 0
			case J_MESSAGE_HELLO:
				{
					JMessage* reply;

					g_atomic_int_add(&jd_thread_num, 1);

					reply = j_message_new_reply(message);
					j_message_write(reply, output);
					j_message_unref(reply);
				}
				break;
#endif
			case J_MESSAGE_REPLY:
			case J_MESSAGE_SAFETY_NETWORK:
			case J_MESSAGE_SAFETY_STORAGE:
			case J_MESSAGE_MODIFIER_MASK:
			default:
				g_warn_if_reached();
				break;
		}
	}

	j_message_unref(message);

	{
		guint64 value;

		G_LOCK(jd_statistics);

		value = j_statistics_get(statistics, J_STATISTICS_FILES_CREATED);
		j_statistics_add(jd_statistics, J_STATISTICS_FILES_CREATED, value);
		value = j_statistics_get(statistics, J_STATISTICS_FILES_DELETED);
		j_statistics_add(jd_statistics, J_STATISTICS_FILES_DELETED, value);
		value = j_statistics_get(statistics, J_STATISTICS_SYNC);
		j_statistics_add(jd_statistics, J_STATISTICS_SYNC, value);
		value = j_statistics_get(statistics, J_STATISTICS_BYTES_READ);
		j_statistics_add(jd_statistics, J_STATISTICS_BYTES_READ, value);
		value = j_statistics_get(statistics, J_STATISTICS_BYTES_WRITTEN);
		j_statistics_add(jd_statistics, J_STATISTICS_BYTES_WRITTEN, value);
		value = j_statistics_get(statistics, J_STATISTICS_BYTES_RECEIVED);
		j_statistics_add(jd_statistics, J_STATISTICS_BYTES_RECEIVED, value);
		value = j_statistics_get(statistics, J_STATISTICS_BYTES_SENT);
		j_statistics_add(jd_statistics, J_STATISTICS_BYTES_SENT, value);

		G_UNLOCK(jd_statistics);
	}

	g_hash_table_destroy(files);

	if (jd_backend_thread_fini != NULL)
	{
		jd_backend_thread_fini();
	}

	j_memory_chunk_free(memory_chunk);
	j_statistics_free(statistics);

	j_trace_leave(G_STRFUNC);

	return TRUE;
}

static
gboolean
jd_daemon (void)
{
	gint fd;
	pid_t pid;

	pid = fork();

	if (pid > 0)
	{
		g_printerr("Daemon started as process %d.\n", pid);
		_exit(0);
	}
	else if (pid == -1)
	{
		return FALSE;
	}

	if (setsid() == -1)
	{
		return FALSE;
	}

	if (g_chdir("/") == -1)
	{
		return FALSE;
	}

	fd = open("/dev/null", O_RDWR);

	if (fd == -1)
	{
		return FALSE;
	}

	if (dup2(fd, STDIN_FILENO) == -1 || dup2(fd, STDOUT_FILENO) == -1 || dup2(fd, STDERR_FILENO) == -1)
	{
		return FALSE;
	}

	if (fd > 2)
	{
		close(fd);
	}

	return TRUE;
}

int
main (int argc, char** argv)
{
	gboolean opt_daemon = FALSE;
	gint opt_port = 4711;

	JConfiguration* configuration;
	GError* error = NULL;
	GMainLoop* main_loop;
	GModule* backend;
	GOptionContext* context;
	GSocketService* socket_service;
	gchar* path;

	GOptionEntry entries[] = {
		{ "daemon", 0, 0, G_OPTION_ARG_NONE, &opt_daemon, "Run as daemon", NULL },
		{ "port", 0, 0, G_OPTION_ARG_INT, &opt_port, "Port to use", "4711" },
		{ NULL, 0, 0, 0, NULL, NULL, NULL }
	};

#if !GLIB_CHECK_VERSION(2,35,1)
	g_type_init();
#endif

	context = g_option_context_new(NULL);
	g_option_context_add_main_entries(context, entries, NULL);

	if (!g_option_context_parse(context, &argc, &argv, &error))
	{
		g_option_context_free(context);

		if (error)
		{
			g_printerr("%s\n", error->message);
			g_error_free(error);
		}

		return 1;
	}

	g_option_context_free(context);

	if (opt_daemon && !jd_daemon())
	{
		return 1;
	}

	socket_service = g_threaded_socket_service_new(-1);

	g_socket_listener_set_backlog(G_SOCKET_LISTENER(socket_service), 128);

	if (!g_socket_listener_add_inet_port(G_SOCKET_LISTENER(socket_service), opt_port, NULL, &error))
	{
		g_object_unref(socket_service);

		if (error != NULL)
		{
			g_printerr("%s\n", error->message);
			g_error_free(error);
		}

		return 1;
	}

	j_trace_init("julea-daemon");

	j_trace_enter(G_STRFUNC);

	configuration = j_configuration_new();

	if (configuration == NULL)
	{
		g_printerr("Could not read configuration.\n");
		return 1;
	}

	path = g_module_build_path(DAEMON_BACKEND_PATH, j_configuration_get_storage_backend(configuration));
	backend = g_module_open(path, G_MODULE_BIND_LOCAL);
	g_free(path);

	g_module_symbol(backend, "backend_init", (gpointer*)&jd_backend_init);
	g_module_symbol(backend, "backend_fini", (gpointer*)&jd_backend_fini);
	g_module_symbol(backend, "backend_thread_init", (gpointer*)&jd_backend_thread_init);
	g_module_symbol(backend, "backend_thread_fini", (gpointer*)&jd_backend_thread_fini);
	g_module_symbol(backend, "backend_create", (gpointer*)&jd_backend_create);
	g_module_symbol(backend, "backend_delete", (gpointer*)&jd_backend_delete);
	g_module_symbol(backend, "backend_open", (gpointer*)&jd_backend_open);
	g_module_symbol(backend, "backend_close", (gpointer*)&jd_backend_close);
	g_module_symbol(backend, "backend_status", (gpointer*)&jd_backend_status);
	g_module_symbol(backend, "backend_sync", (gpointer*)&jd_backend_sync);
	g_module_symbol(backend, "backend_read", (gpointer*)&jd_backend_read);
	g_module_symbol(backend, "backend_write", (gpointer*)&jd_backend_write);

	g_assert(jd_backend_init != NULL);
	g_assert(jd_backend_fini != NULL);
	g_assert(jd_backend_create != NULL);
	g_assert(jd_backend_delete != NULL);
	g_assert(jd_backend_open != NULL);
	g_assert(jd_backend_close != NULL);
	g_assert(jd_backend_status != NULL);
	g_assert(jd_backend_sync != NULL);
	g_assert(jd_backend_read != NULL);
	g_assert(jd_backend_write != NULL);

	if (!jd_backend_init(j_configuration_get_storage_path(configuration)))
	{
		g_printerr("Could not initialize backend.\n");
		return 1;
	}

	j_configuration_unref(configuration);

	jd_statistics = j_statistics_new(FALSE);

	g_socket_service_start(socket_service);
	g_signal_connect(socket_service, "run", G_CALLBACK(jd_on_run), NULL);

	main_loop = g_main_loop_new(NULL, FALSE);

	g_unix_signal_add(SIGHUP, jd_signal, main_loop);
	g_unix_signal_add(SIGINT, jd_signal, main_loop);
	g_unix_signal_add(SIGTERM, jd_signal, main_loop);

	g_main_loop_run(main_loop);
	g_main_loop_unref(main_loop);

	g_socket_service_stop(socket_service);
	g_object_unref(socket_service);

	j_statistics_free(jd_statistics);

	jd_backend_fini();

	g_module_close(backend);

	j_trace_leave(G_STRFUNC);

	j_trace_fini();

	return 0;
}
