/*
 * Copyright © 2008-2011 Kristian Høgsberg
 * Copyright © 2011 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "../config.h"

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "wayland-util.h"
#include "wayland-private.h"

#ifdef HAVE_DLOG
#include <dlog.h>
#define WLLOG_TAG "WLLOG"
#endif

int debug_client = 0;
int debug_server = 0;

WL_EXPORT void
wl_list_init(struct wl_list *list)
{
	list->prev = list;
	list->next = list;
}

WL_EXPORT void
wl_list_insert(struct wl_list *list, struct wl_list *elm)
{
	elm->prev = list;
	elm->next = list->next;
	list->next = elm;
	elm->next->prev = elm;
}

WL_EXPORT void
wl_list_remove(struct wl_list *elm)
{
	elm->prev->next = elm->next;
	elm->next->prev = elm->prev;
	elm->next = NULL;
	elm->prev = NULL;
}

WL_EXPORT int
wl_list_length(const struct wl_list *list)
{
	struct wl_list *e;
	int count;

	count = 0;
	e = list->next;
	while (e != list) {
		e = e->next;
		count++;
	}

	return count;
}

WL_EXPORT int
wl_list_empty(const struct wl_list *list)
{
	return list->next == list;
}

WL_EXPORT void
wl_list_insert_list(struct wl_list *list, struct wl_list *other)
{
	if (wl_list_empty(other))
		return;

	other->next->prev = list;
	other->prev->next = list->next;
	list->next->prev = other->prev;
	list->next = other->next;
}

WL_EXPORT void
wl_array_init(struct wl_array *array)
{
	memset(array, 0, sizeof *array);
}

WL_EXPORT void
wl_array_release(struct wl_array *array)
{
	free(array->data);
}

WL_EXPORT void *
wl_array_add(struct wl_array *array, size_t size)
{
	size_t alloc;
	void *data, *p;

	if (array->alloc > 0)
		alloc = array->alloc;
	else
		alloc = 16;

	while (alloc < array->size + size)
		alloc *= 2;

	if (array->alloc < alloc) {
		if (array->alloc > 0)
			data = realloc(array->data, alloc);
		else
			data = malloc(alloc);

		if (data == NULL)
			return NULL;
		array->data = data;
		array->alloc = alloc;
	}

	p = array->data + array->size;
	array->size += size;

	return p;
}

WL_EXPORT int
wl_array_copy(struct wl_array *array, struct wl_array *source)
{
	if (array->size < source->size) {
		if (!wl_array_add(array, source->size - array->size))
			return -1;
	} else {
		array->size = source->size;
	}

	memcpy(array->data, source->data, source->size);
	return 0;
}

/** \cond */

struct wl_object global_zombie_object;

int
wl_interface_equal(const struct wl_interface *a, const struct wl_interface *b)
{
	/* In most cases the pointer equality test is sufficient.
	 * However, in some cases, depending on how things are split
	 * across shared objects, we can end up with multiple
	 * instances of the interface metadata constants.  So if the
	 * pointers match, the interfaces are equal, if they don't
	 * match we have to compare the interface names.
	 */
	return a == b || strcmp(a->name, b->name) == 0;
}

union map_entry {
	uintptr_t next;
	void *data;
};

#define map_entry_is_free(entry) ((entry).next & 0x1)
#define map_entry_get_data(entry) ((void *)((entry).next & ~(uintptr_t)0x3))
#define map_entry_get_flags(entry) (((entry).next >> 1) & 0x1)

WL_EXPORT void
wl_map_init(struct wl_map *map, uint32_t side)
{
	memset(map, 0, sizeof *map);
	map->side = side;
}

WL_EXPORT void
wl_map_release(struct wl_map *map)
{
	wl_array_release(&map->client_entries);
	wl_array_release(&map->server_entries);
}

WL_EXPORT uint32_t
wl_map_insert_new(struct wl_map *map, uint32_t flags, void *data)
{
	union map_entry *start, *entry;
	struct wl_array *entries;
	uint32_t base;

	if (map->side == WL_MAP_CLIENT_SIDE) {
		entries = &map->client_entries;
		base = 0;
	} else {
		entries = &map->server_entries;
		base = WL_SERVER_ID_START;
	}

	if (map->free_list) {
		start = entries->data;
		entry = &start[map->free_list >> 1];
		map->free_list = entry->next;
	} else {
		entry = wl_array_add(entries, sizeof *entry);
		if (!entry)
			return 0;
		start = entries->data;
	}

	entry->data = data;
	entry->next |= (flags & 0x1) << 1;

	return (entry - start) + base;
}

WL_EXPORT int
wl_map_insert_at(struct wl_map *map, uint32_t flags, uint32_t i, void *data)
{
	union map_entry *start;
	uint32_t count;
	struct wl_array *entries;

	if (i < WL_SERVER_ID_START) {
		entries = &map->client_entries;
	} else {
		entries = &map->server_entries;
		i -= WL_SERVER_ID_START;
	}

	count = entries->size / sizeof *start;
	if (count < i)
		return -1;

	if (count == i)
		wl_array_add(entries, sizeof *start);

	start = entries->data;
	start[i].data = data;
	start[i].next |= (flags & 0x1) << 1;

	return 0;
}

WL_EXPORT int
wl_map_reserve_new(struct wl_map *map, uint32_t i)
{
	union map_entry *start;
	uint32_t count;
	struct wl_array *entries;

	if (i < WL_SERVER_ID_START) {
		if (map->side == WL_MAP_CLIENT_SIDE)
			return -1;

		entries = &map->client_entries;
	} else {
		if (map->side == WL_MAP_SERVER_SIDE)
			return -1;

		entries = &map->server_entries;
		i -= WL_SERVER_ID_START;
	}

	count = entries->size / sizeof *start;

	if (count < i)
		return -1;

	if (count == i) {
		wl_array_add(entries, sizeof *start);
		start = entries->data;
		start[i].data = NULL;
	} else {
		start = entries->data;
		if (start[i].data != NULL) {
			return -1;
		}
	}

	return 0;
}

WL_EXPORT void
wl_map_remove(struct wl_map *map, uint32_t i)
{
	union map_entry *start;
	struct wl_array *entries;

	if (i < WL_SERVER_ID_START) {
		if (map->side == WL_MAP_SERVER_SIDE)
			return;

		entries = &map->client_entries;
	} else {
		if (map->side == WL_MAP_CLIENT_SIDE)
			return;

		entries = &map->server_entries;
		i -= WL_SERVER_ID_START;
	}

	start = entries->data;
	start[i].next = map->free_list;
	map->free_list = (i << 1) | 1;
}

// TIZEN_ONLY(20170328) : leave log about pending requests from clients if sendmsg() fails due to EAGAIN error
int
wl_map_client_entries_count(struct wl_map *map)
{
	struct wl_array* entries = NULL;
	union map_entry* start = NULL;
	uint32_t count = 0;

	if(NULL == map)
		return -1;

	entries = &(map->client_entries);
	start = entries->data;
	count = entries->size / sizeof (*start);
	return count;
}
// END

WL_EXPORT void *
wl_map_lookup(struct wl_map *map, uint32_t i)
{
	union map_entry *start;
	uint32_t count;
	struct wl_array *entries;

	if (i < WL_SERVER_ID_START) {
		entries = &map->client_entries;
	} else {
		entries = &map->server_entries;
		i -= WL_SERVER_ID_START;
	}

	start = entries->data;
	count = entries->size / sizeof *start;

	if (i < count && !map_entry_is_free(start[i]))
		return map_entry_get_data(start[i]);

	return NULL;
}

WL_EXPORT uint32_t
wl_map_lookup_flags(struct wl_map *map, uint32_t i)
{
	union map_entry *start;
	uint32_t count;
	struct wl_array *entries;

	if (i < WL_SERVER_ID_START) {
		entries = &map->client_entries;
	} else {
		entries = &map->server_entries;
		i -= WL_SERVER_ID_START;
	}

	start = entries->data;
	count = entries->size / sizeof *start;

	if (i < count && !map_entry_is_free(start[i]))
		return map_entry_get_flags(start[i]);

	return 0;
}

static enum wl_iterator_result
for_each_helper(struct wl_array *entries, wl_iterator_func_t func, void *data)
{
	union map_entry *start, *end, *p;
	enum wl_iterator_result ret = WL_ITERATOR_CONTINUE;

	start = entries->data;
	end = (union map_entry *) ((char *) entries->data + entries->size);

	for (p = start; p < end; p++)
		if (p->data && !map_entry_is_free(*p)) {
			ret = func(map_entry_get_data(*p), data);
			if (ret != WL_ITERATOR_CONTINUE)
				break;
		}

	return ret;
}

WL_EXPORT void
wl_map_for_each(struct wl_map *map, wl_iterator_func_t func, void *data)
{
	enum wl_iterator_result ret;

	ret = for_each_helper(&map->client_entries, func, data);
	if (ret == WL_ITERATOR_CONTINUE)
		for_each_helper(&map->server_entries, func, data);
}

WL_EXPORT void
wl_debug_client_enable(int enable)
{
	debug_client = !!enable;
}

WL_EXPORT void
wl_debug_server_enable(int enable)
{
	debug_server = !!enable;
}

int debug_dlog;

#ifdef HAVE_DLOG
void
_wl_dlog(const char *fmt, ...)
{
	va_list argp;

	va_start(argp, fmt);
	if (debug_dlog)
		dlog_vprint(DLOG_ERROR, WLLOG_TAG, fmt, argp);
	else
		vfprintf(stderr, fmt, argp);
	va_end(argp);
}
#endif

static void
wl_log_stderr_handler(const char *fmt, va_list arg)
{
	vfprintf(stderr, fmt, arg);
}

wl_log_func_t wl_log_handler = wl_log_stderr_handler;

void
wl_log(const char *fmt, ...)
{
	va_list argp;

	va_start(argp, fmt);
#ifdef HAVE_DLOG
	dlog_vprint(DLOG_ERROR, WLLOG_TAG, fmt, argp);
#endif
	wl_log_handler(fmt, argp);
	va_end(argp);
}

void
wl_abort(const char *fmt, ...)
{
	va_list argp;

	va_start(argp, fmt);
#ifdef HAVE_DLOG
	dlog_vprint(DLOG_ERROR, WLLOG_TAG, fmt, argp);
#endif
	wl_log_handler(fmt, argp);
	va_end(argp);

	abort();
}

/** \endcond */
