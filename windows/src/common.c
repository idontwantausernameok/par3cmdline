
// avoid error of MSVC
#define _CRT_SECURE_NO_WARNINGS

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

// MSVC headers
#include <search.h>
#include <io.h>

#include "common.h"


// return pointer of filename
char * offset_file_name(char *file_path)
{
	int i;

	for (i = (int)strlen(file_path) - 2; i >= 0; i--){
		if ((file_path[i] == '\\') || (file_path[i] == '/'))
			break;
	}
	i++;

	return file_path + i;
}

// convert relative path to absolute path
int get_absolute_path(char *absolute_path, char *relative_path, size_t max)
{
	// MSVC
	struct _finddata_t c_file;
	intptr_t handle;

	// Confirm the path is valid.
	handle = _findfirst(relative_path, &c_file);
	if (handle != -1){
		_findclose(handle);

		if (_fullpath(absolute_path, c_file.name, max) == NULL){
			perror("Failed to make absolute path");
			return 1;
		}
	} else {
		if (_fullpath(absolute_path, relative_path, max) == NULL){
			perror("Failed to make absolute path");
			return 2;
		}
	}

	return 0;
}

// copy filename, remove cover, replace directory mark
size_t path_copy(char *dst, char *src, size_t max)
{
	char *tmp_p;
	size_t len;

	tmp_p = src;
	len = strlen(tmp_p);
	if ( (tmp_p[0] == '"') && (tmp_p[len - 1] == '"') ){
		tmp_p++;
		len -= 2;
	}
	if (len >= max){
		dst[0] = 0;
		return 0;
	}

	memcpy(dst, tmp_p, len);
	dst[len] = 0;

	// replace directory mark from Windows OS style "\" to UNIX style "/"
	tmp_p = strchr(dst, '\\');
	while (tmp_p != NULL){
		tmp_p[0] = '/';
		tmp_p = strchr(tmp_p, '\\');
	}

	return len;
}

// Because Argz Functions don't exit on MSVC, I made similar functions.

// add a name to the end of names
// return 0 for success, else ENOMEM for memory error
int namez_add(char **namez, size_t *namez_len, size_t *namez_max, const char *str)
{
	char *list_buf;
	size_t list_len, list_max, len;

	if (str == NULL)
		return 0;
	if (str[0] == 0)
		return 0;

	list_buf = *namez;
	list_len = *namez_len;
	list_max = *namez_max;

	len = strlen(str);

	if (list_buf == NULL){	// allocate memory at first
		size_t alloc_size;

		// first buffer size is multiple of 1024, larger than _MAX_FNAME * 4
		alloc_size = _MAX_FNAME * 4;
		if (alloc_size & 1023){
			alloc_size = (alloc_size & ~1023) + 1024;
		}
		//printf("alloc_size = %d\n", alloc_size);

		list_buf = malloc(alloc_size);
		if (list_buf == NULL)
			return 8;
		list_len = 0;
		list_max = alloc_size;
	} else if (list_len + len >= list_max){	// increase memory area
		char *tmp_p;
		size_t alloc_size;

		// additional size is multiple of 1024, larger than _MAX_FNAME * 2
		alloc_size = _MAX_FNAME * 2;
		if (alloc_size & 1023){
			alloc_size = (alloc_size & ~1023) + 1024;
		}
		//printf("alloc_size = %d\n", alloc_size);

		tmp_p = realloc(list_buf, list_max + alloc_size);
		if (tmp_p == NULL)
			return 8;
		list_buf = tmp_p;
		list_max += alloc_size;
	}

	memcpy(list_buf + list_len, str, len);
	list_len += len;
	list_buf[list_len] = 0;
	list_len++;

	*namez = list_buf;
	*namez_len = list_len;
	*namez_max = list_max;
	return 0;
}

// return count of names
int namez_count(char *namez, size_t namez_len)
{
	int num;
	size_t off, len;

	if (namez == NULL)
		return 0;
	if (namez[0] == 0)
		return 0;

	num = 0;
	off = 0;
	while (off < namez_len){
		num++;
		len = strlen(namez + off);
		off += len + 1;
	}

	return num;
}

// remove an entry from names
// return 0 for success, 1 for cannot find
int namez_delete(char *namez, size_t *namez_len, char *entry)
{
	size_t list_len, len, off;

	if (entry == NULL)
		return 0;
	if (entry[0] == 0)
		return 0;

	list_len = *namez_len;

	if (namez == NULL)
		return 1;

	if ( (entry > namez) && ((size_t)(entry - namez) < list_len) ){
		// entry is an item on the list
		off = (size_t)(entry - namez);
	} else {
		// if entry is outside, search entry at first
		off = 0;
		while (off < list_len){
			if (_stricmp(namez + off, entry) == 0){
				break;
			}
			len = strlen(namez + off);
			off += len + 1;
		}
		if (off >= list_len)
			return 1;
	}

	len = strlen(entry) + 1;
	memmove(namez + off, namez + off + len, list_len - off - len);
	list_len -= len;

	*namez_len = list_len;
	return 0;
}

// search a match from names
// return found position, or NULL for cannot find
char * namez_search(char *namez, size_t namez_len, char *match)
{
	size_t len, off;

	if (match == NULL)
		return NULL;
	if (match[0] == 0)
		return NULL;
	if (namez == NULL)
		return NULL;

	off = 0;
	while (off < namez_len){
		if (_stricmp(namez + off, match) == 0){
			return namez + off;
		}
		len = strlen(namez + off);
		off += len + 1;
	}

	return NULL;
}

static int compare_string( const void *arg1, const void *arg2 )
{
	return strcmp( * ( char** ) arg1, * ( char** ) arg2 );
}

// sort names
// return number of names for success, else -1 for error
int namez_sort(char *namez, size_t namez_len)
{
	char *list_buf, **list_name;
	int num, max;
	size_t off, len;

	if (namez == NULL)
		return 0;
	if (namez[0] == 0)
		return 0;

	// get count of names at first
	max = 0;
	off = 0;
	while (off < namez_len){
		max++;
		len = strlen(namez + off);
		off += len + 1;
	}
	if (max <= 1)
		return max;	// when there is only one name, no need to sort.

	// allocate memory for temporary area
	list_buf = malloc(namez_len);
	if (list_buf == NULL)
		return -1;
	memcpy(list_buf, namez, namez_len);

	// allocate memory for offset
	list_name = malloc(max * sizeof(char *));
	if (list_name == NULL){
		free(list_buf);
		return -1;
	}

	// set offset of names
	num = 0;
	off = 0;
	while (off < namez_len){
		list_name[num] = list_buf + off;
		//printf("list_name[%d] = %s\n", num, list_buf + off);
		num++;
		len = strlen(list_buf + off);
		off += len + 1;
	}

	// quick sort
	qsort( (void *)list_name, (size_t)max, sizeof(char *), compare_string );

	// put back
	off = 0;
	for (num = 0; num < max; num++){
		//printf("list_name[%d] = %s\n", num, list_name[num]);
		len = strlen(list_name[num]);
		memcpy(namez + off, list_name[num], len);
		off += len;
		namez[off] = 0;
		off++;
	}

	free(list_buf);
	free(list_name);
	return max;
}
