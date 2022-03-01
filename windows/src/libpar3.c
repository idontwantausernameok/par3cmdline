
// avoid error of MSVC
#define _CRT_SECURE_NO_WARNINGS

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// MSVC headers
#include <sys/types.h>
#include <sys/stat.h>
#include <direct.h>
#include <io.h>

#include "libpar3.h"
#include "hash.h"
#include "common.h"
#include "create.h"
#include "packet.h"
#include "trial.h"
#include "write.h"


// recursive search into sub-directories
static int path_search_recursive(PAR3_CTX *par3_ctx, char *sub_dir)
{
	char new_dir[_MAX_PATH * 2];
	int ret;
	size_t dir_len;

	// MSVC
	struct _finddata_t c_file;
	intptr_t handle;

	//printf("recursive search '%s'\n", sub_dir);
	dir_len = strlen(sub_dir);
	memcpy(new_dir, sub_dir, dir_len);
	new_dir[dir_len] = '/';
	dir_len++;
	new_dir[dir_len] = 0;

	handle = _findfirst("*", &c_file);
	if (handle != -1){
		do {
			// ignore "." or ".."
			if ( (strcmp(c_file.name, ".") == 0) || (strcmp(c_file.name, "..") == 0) )
				continue;
			// ignore hidden or system files
			if ( ((c_file.attrib & _A_HIDDEN) != 0) || ((c_file.attrib & _A_SYSTEM) != 0) )
				continue;

			// add relative path to the found filename
			strcpy(new_dir + dir_len, c_file.name);
			//printf("found = '%s'\n", new_dir);
			if (strlen(new_dir) >= _MAX_PATH){
				printf("Found file path is too long '%s'\n", new_dir);
				_findclose(handle);
				return RET_FILE_IO_ERROR;
			}

			if ((c_file.attrib & _A_SUBDIR) == 0){	// when the name is a file

				// check name in list, and ignore if exist
				if (namez_search(par3_ctx->input_file_name, par3_ctx->input_file_name_len, new_dir) != NULL)
					continue;

				// add found filename with relative path
				if ( namez_add(&(par3_ctx->input_file_name), &(par3_ctx->input_file_name_len), &(par3_ctx->input_file_name_max), new_dir) != 0){
					_findclose(handle);
					return RET_MEMORY_ERROR;
				}

			} else {	// recursive search is enabled

				// check name in list, and ignore if exist
				if (namez_search(par3_ctx->input_dir_name, par3_ctx->input_dir_name_len, new_dir) != NULL)
					continue;

				// add found filename with relative path
				if ( namez_add(&(par3_ctx->input_dir_name), &(par3_ctx->input_dir_name_len), &(par3_ctx->input_dir_name_max), new_dir) != 0){
					_findclose(handle);
					return RET_MEMORY_ERROR;
				}

				// goto inner directory
				if (_chdir(c_file.name) != 0){
					perror("Failed to go sub directory\n");
					return RET_FILE_IO_ERROR;
				}

				// try to search inner directory
				ret = path_search_recursive(par3_ctx, new_dir);
				if (ret != 0){
					_findclose(handle);
					return ret;
				}

				// return to parent (this) directory
				if (_chdir("..") != 0){
					perror("Failed to return parent directory\n");
					return RET_FILE_IO_ERROR;
				}
			}

		} while( _findnext( handle, &c_file ) == 0 );

		_findclose(handle);
	}

	return 0;
}

// match_path may be relative path from current working directory
int path_search(PAR3_CTX *par3_ctx, char *match_path, int flag_recursive)
{
	char *tmp_p, *match_name;
	char cur_dir[_MAX_PATH], new_dir[_MAX_PATH * 2];
	int ret;
	size_t dir_len, len, base_len;

	// MSVC
	struct _finddata_t c_file;
	intptr_t handle;

	// when match_path includes directory, change to the directory at first
	tmp_p = strrchr(match_path, '/');
	if (tmp_p != NULL){
		match_name = tmp_p + 1;

		// directory may be a relative path from basepath
		len = (size_t)(tmp_p - match_path);
		memcpy(new_dir + 2, match_path, len);
		new_dir[0] = '.';
		new_dir[1] = '/';
		new_dir[2 + len] = 0;
		//printf("new_dir = '%s'\n", new_dir);

		// store current working directory, and will resume later
		tmp_p = _getcwd(cur_dir, _MAX_PATH);
		if (tmp_p == NULL){
			perror("Failed to get current working directory\n");
			return RET_FILE_IO_ERROR;
		}

		// move to the sub directory
		if (_chdir(new_dir) != 0){
			perror("Failed to change working directory\n");
			return RET_FILE_IO_ERROR;
		}

		// get the new working directory
		tmp_p = _getcwd(new_dir, _MAX_PATH);
		if (tmp_p == NULL){
			perror("Failed to get new working directory\n");
			return RET_FILE_IO_ERROR;
		}
		//printf("new_dir = '%s'\n", new_dir);

		// check the directory is a child
		base_len = strlen(cur_dir);
		if (memcmp(cur_dir, new_dir, base_len) != 0){	// relative path is out side
			// return to original working directory
			if (_chdir(cur_dir) != 0){
				perror("Failed to resume working directory\n");
				return 6;
			}
			printf("Ignoring out of basepath source file: %s\n", match_path);
			return RET_FILE_IO_ERROR;
		}
		base_len++;	// add the last "/"

		// replace directory mark from Windows OS style "\" to UNIX style "/"
		tmp_p = strchr(new_dir + base_len, '\\');
		while (tmp_p != NULL){
			tmp_p[0] = '/';
			tmp_p = strchr(tmp_p, '\\');
		}
		strcat(new_dir, "/");
		//printf("dir path = '%s'\n", new_dir);

		// check case for sensitive system
		tmp_p = strchr(new_dir + base_len, '/');
		while (tmp_p != NULL){
			tmp_p[0] = 0;
			//printf("path component = '%s'\n", new_dir);
			handle = _findfirst(new_dir, &c_file);
			if (handle != -1){
				// If case is different, use the original case.
				//printf("found component = '%s'\n", c_file.name);
				len = strlen(c_file.name);
				if (strcmp(tmp_p - len, c_file.name) != 0)
					strcpy(tmp_p - len, c_file.name);
				_findclose(handle);
			}
			tmp_p[0] = '/';
			tmp_p = strchr(tmp_p + 1, '/');
		}

		// get the relative path
		dir_len = strlen(new_dir) - base_len;
		memmove(new_dir, new_dir + base_len, dir_len + 1);	// copy path, which inlcudes the last null string
		//printf("relative path = '%s'\n", new_dir);

	} else {
		match_name = match_path;
		dir_len = 0;
	}

	handle = _findfirst(match_name, &c_file);
	if (handle != -1){
		do {
			// ignore "." or ".."
			if ( (strcmp(c_file.name, ".") == 0) || (strcmp(c_file.name, "..") == 0) )
				continue;
			// ignore hidden or system files
			if ( ((c_file.attrib & _A_HIDDEN) != 0) || ((c_file.attrib & _A_SYSTEM) != 0) )
				continue;

			// found filename may different case from the specified name
			// add relative path to the found filename
			strcpy(new_dir + dir_len, c_file.name);
			//printf("found = '%s'\n", new_dir);
			if (strlen(new_dir) >= _MAX_PATH){
				printf("Found file path is too long '%s'\n", new_dir);
				_findclose(handle);
				return RET_FILE_IO_ERROR;
			}

			if ((c_file.attrib & _A_SUBDIR) == 0){	// When the name is file

				// check name in list, and ignore if exist
				if (namez_search(par3_ctx->input_file_name, par3_ctx->input_file_name_len, new_dir) != NULL)
					continue;

				// add found filename with relative path
				if ( namez_add(&(par3_ctx->input_file_name), &(par3_ctx->input_file_name_len), &(par3_ctx->input_file_name_max), new_dir) != 0){
					_findclose(handle);
					return RET_MEMORY_ERROR;
				}

			} else if (flag_recursive != 0){	// When the name is a directory and recursive search is enabled

				// check name in list, and ignore if exist
				if (namez_search(par3_ctx->input_dir_name, par3_ctx->input_dir_name_len, new_dir) != NULL)
					continue;

				// add found filename with relative path
				if ( namez_add(&(par3_ctx->input_dir_name), &(par3_ctx->input_dir_name_len), &(par3_ctx->input_dir_name_max), new_dir) != 0){
					_findclose(handle);
					return RET_MEMORY_ERROR;
				}

				// goto inner directory
				if (_chdir(c_file.name) != 0){
					perror("Failed to go sub directory\n");
					return RET_FILE_IO_ERROR;
				}

				// try to search inner directory
				ret = path_search_recursive(par3_ctx, new_dir);
				if (ret != 0){
					_findclose(handle);
					return ret;
				}

				// return to parent (this) directory
				if (_chdir("..") != 0){
					perror("Failed to return parent directory\n");
					return RET_FILE_IO_ERROR;
				}
			}

		} while( _findnext( handle, &c_file ) == 0 );

		_findclose(handle);
	}

	// resume original working directory
	if (match_name != match_path){
		if (_chdir(cur_dir) != 0){
			perror("Failed to resume working directory\n");
			return RET_FILE_IO_ERROR;
		}
	}

	return 0;
}

// get information of input files
int get_file_status(PAR3_CTX *par3_ctx)
{
	char *list_name;
	int ret;
	uint32_t num;
	size_t len;
	uint64_t file_size;
	struct _stat64 stat_buf;
	PAR3_FILE_CTX *file_p;

	// Allocate directory list at here.
	num = par3_ctx->input_dir_count;
	if (num > 0){
		PAR3_DIR_CTX *dir_p;

		dir_p = malloc(sizeof(PAR3_DIR_CTX) * num);
		if (dir_p == NULL){
			perror("Failed to allocate memory for input directories");
			return RET_MEMORY_ERROR;
		}
		par3_ctx->input_dir_list = dir_p;

		list_name = par3_ctx->input_dir_name;
		while (num > 0){
			dir_p->name = list_name;	// pointer to the directory name

			len = strlen(list_name);
			list_name += len + 1;

			dir_p++;
			num--;
		}
	}

	num = par3_ctx->input_file_count;
	if (num == 0)
		return 0;

	file_p = malloc(sizeof(PAR3_FILE_CTX) * num);
	if (file_p == NULL){
		perror("Failed to allocate memory for input files");
		return RET_MEMORY_ERROR;
	}
	par3_ctx->input_file_list = file_p;

	list_name = par3_ctx->input_file_name;
	par3_ctx->total_file_size = 0;
	par3_ctx->max_file_size = 0;
	while (num > 0){
		ret = _stat64(list_name, &stat_buf);
		if (ret != 0){
			printf("Failed to get status information of '%s'\n", list_name);
			return RET_FILE_IO_ERROR;
		}
		file_size = stat_buf.st_size;
		//printf("st_mode = %04x '%s'\n", stat_buf.st_mode, list_name);

		file_p->name = list_name;	// pointer to the file name
		file_p->size = file_size;	// 64-bit unsigned integer
		file_p->crc = 0;

		par3_ctx->total_file_size += file_size;
		if (par3_ctx->max_file_size < file_size)
			par3_ctx->max_file_size = file_size;

		len = strlen(list_name);
		list_name += len + 1;
		file_p++;
		num--;
	}

	if (par3_ctx->noise_level >= 0){
		printf("Total file size = %I64u\n", par3_ctx->total_file_size);
		printf("Max file size = %I64u\n", par3_ctx->max_file_size);
	}

	return 0;
}

// suggest a block size for given input files
// block count = block size * 1%
uint64_t suggest_block_size(PAR3_CTX *par3_ctx)
{
	uint64_t block_size, block_count;
	long double f;

	// If every input files are smaller than 40 bytes, block size will be 40.
	if (par3_ctx->max_file_size < 40)
		return 40;

	// total file size = block size * block count
	// total file size = block size * block size * 1%
	// total file size * 100 = block size * block size
	// block size = root(total file size) * 10
	f = (long double)(par3_ctx->total_file_size);
	f = sqrtl(f) * 10;
	block_size = (uint64_t)f;

	// Block size should not become larger than the max input file.
	if (block_size > par3_ctx->max_file_size)
		block_size = par3_ctx->max_file_size;

	// Block size should be larger than 8 bytes.
	if (block_size < 8)
		block_size = 8;

	// Block size is good to be power of 2.
	block_count = 8;
	while (block_count * 2 <= block_size)
		block_count *= 2;
	//printf("block_size = %I64u, power of 2 = %I64u\n", block_size, block_count);
	block_size = block_count;

	// test possible number of blocks
	block_count = calculate_block_count(par3_ctx, block_size);
	//printf("1st block_count = %I64u\n", block_count);
	if (block_count > 128){	// If range is 16-bit Reed Solomon Codes, more block count is possible.
		if (block_count <= 1000){	// When there are too few blocks
			block_size /= 2;
			if (block_size < 40)
				block_size = 40;	// The miminum block size will be 40 bytes.
		}
	}
	while (block_count > 32768){	// When there are too many blocks
		block_size *= 2;
		block_count = calculate_block_count(par3_ctx, block_size);
	}
	//printf("2nd block_count = %I64u\n", block_count);

	// If total number of input blocks is equal or less than 128,
	// PAR3 uses 8-bit Reed-Solomon Codes.
	// Or else, PAR3 uses 16-bit Reed-Solomon Codes for 129 or more input blocks.
	if ((block_count > 128) && (block_size & 1)){
		// Block size must be multiple of 2 for 16-bit Reed-Solomon Codes.
		block_size += 1;
	}

	return block_size;
}

// try to calculate number of blocks for given input file and block size
uint64_t calculate_block_count(PAR3_CTX *par3_ctx, uint64_t block_size)
{
	uint32_t num;
	uint64_t file_size, block_count, full_count, tail_size;
	PAR3_FILE_CTX *file_p;

	num = par3_ctx->input_file_count;
	if (num <= 0)
		return 0;

	file_p = par3_ctx->input_file_list;

	block_count = 0;
	while (num > 0){
		file_size = file_p->size;
		if (file_size > 0){
			// how many full size block in a file
			full_count = file_size / block_size;
			block_count += full_count;

			// if tail chunk size is equal or larger than 40 bytes, it will make block
			tail_size = file_size - (block_size * full_count);
			if (tail_size >= 40)
				block_count++;
		}

		file_p++;
		num--;
	}

	return block_count;
}

// Comparison functions
 // Sort files by tail size for tail packing.
static int compare_tail_size( const void *arg1, const void *arg2 )
{
	PAR3_FILE_CTX *file1_p, *file2_p;

	file1_p = ( PAR3_FILE_CTX * ) arg1;
	file2_p = ( PAR3_FILE_CTX * ) arg2;

	// Move long tail size to the former.
	if (file1_p->chk[0] < file2_p->chk[0])
		return 1;
	if (file1_p->chk[0] > file2_p->chk[0])
		return -1;

	// Move long file size to the former.
	if (file1_p->size < file2_p->size)
		return 1;
	if (file1_p->size > file2_p->size)
		return -1;

	return strcmp( file1_p->name, file2_p->name );
}

// Move directory with children to the former.
// Move file or directory without children to the latter.
static int compare_directory( const void *arg1, const void *arg2 )
{
	PAR3_DIR_CTX *dir1_p, *dir2_p;
	char *str1, *str2, *dir1, *dir2;
	int ret;

	dir1_p = ( PAR3_DIR_CTX * ) arg1;
	dir2_p = ( PAR3_DIR_CTX * ) arg2;
	str1 = dir1_p->name;
	str2 = dir2_p->name;

	while(1) {
		// check directory
		dir1 = strchr(str1, '/');
		dir2 = strchr(str2, '/');

		if (dir1 == NULL){
			if (dir2 == NULL){	// when both names don't have directory
				// just compare as name
				return strcmp(str1, str2);
			} else {
				// when 2nd name has directory and 1st name has not
				dir2[0] = 0;
				ret = strcmp(str1, str2);
				dir2[0] = '/';
				if (ret != 0)
					return ret;
				return 1;
			}
		} else {
			if (dir2 == NULL){
				// when 1st name has directory and 2nd name has not
				dir1[0] = 0;
				ret = strcmp(str1, str2);
				dir1[0] = '/';
				if (ret != 0)
					return ret;
				return -1;
			} else {	// when both names have directory
				// compare sub-directory
				dir1[0] = 0;
				dir2[0] = 0;
				ret = strcmp(str1, str2);
				dir1[0] = '/';
				dir2[0] = '/';
				if (ret != 0)
					return ret;

				// goto child directory
				str1 = dir1 + 1;
				str2 = dir2 + 1;
			}
		}
	}

	return ret;
}

// sort input files for efficient tail packing.
int sort_input_set(PAR3_CTX *par3_ctx)
{
	uint32_t num;

	num = par3_ctx->input_file_count;
	if (num > 0){
		PAR3_FILE_CTX *file_p;
		uint64_t block_size, file_size, tail_size;

		// set tail size of each file temporary
		block_size = par3_ctx->block_size;
		file_p = par3_ctx->input_file_list;
		while (num > 0){
			file_size = file_p->size;
			if (file_size > 0){
				tail_size = file_size % block_size;
				file_p->chk[0] = tail_size;
			} else {
				file_p->chk[0] = 0;
			}
			//printf("file size = %I64u, tail size = %I64u\n", file_size, tail_size);

			file_p++;
			num--;
		}

		num = par3_ctx->input_file_count;
		file_p = par3_ctx->input_file_list;

		if (num > 1){
			// quick sort
			qsort( (void *)file_p, (size_t)num, sizeof(PAR3_FILE_CTX), compare_tail_size );
		}

		if (par3_ctx->noise_level >= 0){
			while (num > 0){
				printf("input file = '%s' %I64u / %I64u\n", file_p->name, file_p->chk[0], file_p->size);

				file_p++;
				num--;
			}
			printf("\n");
		}
	}

	num = par3_ctx->input_dir_count;
	if (num > 0){
		PAR3_DIR_CTX *dir_p;

		dir_p = par3_ctx->input_dir_list;

		if (num > 1){
			// quick sort
			qsort( (void *)dir_p, (size_t)num, sizeof(PAR3_DIR_CTX), compare_directory );
		}

		if (par3_ctx->noise_level >= 0){
			while (num > 0){
				printf("input dir  = '%s'\n", dir_p->name);

				dir_p++;
				num--;
			}
			printf("\n");
		}
	}

	return 0;
}


// add text in Creator Packet
int add_creator_text(PAR3_CTX *par3_ctx, char *text)
{
	uint8_t *tmp_p;
	size_t len, alloc_size;

	len = strlen(text);
	if (len == 0)
		return 0;

	if (par3_ctx->creator_packet == NULL){	// When there is no packet yet, allocate now.
		alloc_size = 48 + len;
		par3_ctx->creator_packet = malloc(alloc_size);
		if (par3_ctx->creator_packet == NULL){
			perror("Failed to allocate memory for Creator Packet");
			return RET_MEMORY_ERROR;
		}
	} else {	// When there is packet already, add new text to previous text.
		alloc_size = par3_ctx->creator_packet_size + len;
		tmp_p = realloc(par3_ctx->creator_packet, alloc_size);
		if (tmp_p == NULL){
			perror("Failed to re-allocate memory for Creator Packet");
			return RET_MEMORY_ERROR;
		}
		par3_ctx->creator_packet = tmp_p;
	}
	par3_ctx->creator_packet_size = alloc_size;
	memcpy(par3_ctx->creator_packet + alloc_size - len, text, len);

	return 0;
}

// add text in Comment Packet
int add_comment_text(PAR3_CTX *par3_ctx, char *text)
{
	uint8_t *tmp_p;
	size_t len, alloc_size;

	// If text is covered by ", remove them.
	len = strlen(text);
	if ( (len > 2) && (text[0] == '"') && (text[len - 1] == '"') ){
		text++;
		len -= 2;
	}
	if (len == 0)
		return 0;

	if (par3_ctx->comment_packet == NULL){	// When there is no packet yet, allocate now.
		alloc_size = 48 + len;
		par3_ctx->comment_packet = malloc(alloc_size);
		if (par3_ctx->comment_packet == NULL){
			perror("Failed to allocate memory for Comment Packet");
			return RET_MEMORY_ERROR;
		}
	} else {	// When there is packet already, add new comment to previous comment.
		alloc_size = par3_ctx->comment_packet_size + 1 + len;
		tmp_p = realloc(par3_ctx->comment_packet, alloc_size);
		if (tmp_p == NULL){
			perror("Failed to re-allocate memory for Comment Packet");
			return RET_MEMORY_ERROR;
		}
		par3_ctx->comment_packet = tmp_p;
		tmp_p += par3_ctx->comment_packet_size;
		tmp_p[0] = '\n';	// Put "\n" between comments.
	}
	par3_ctx->comment_packet_size = alloc_size;
	memcpy(par3_ctx->comment_packet + alloc_size - len, text, len);

	return 0;
}


int par3_trial(PAR3_CTX *par3_ctx,
	char command_recovery_file_scheme,
	char command_data_packet,
	char command_deduplication)
{
	int ret;

	// Load input blocks on memory.
	if (par3_ctx->block_count == 0){
		ret = map_chunk_tail(par3_ctx);
	} else if (command_deduplication == '1'){	// Simple deduplication
		ret = map_input_block(par3_ctx);
	} else if (command_deduplication == '2'){	// Deduplication with slide search
		ret = map_input_block_slide(par3_ctx);
	} else {
		ret = map_input_block_simple(par3_ctx);
	}
	if (ret != 0)
		return ret;

	// Creator Packet, Comment Packet, Start Packet
	ret = make_start_packet(par3_ctx);
	if (ret != 0)
		return ret;

	// Only when recovery blocks will be created, make Matrix Packet.
	if (par3_ctx->recovery_block_count > 0){
		ret = make_matrix_packet(par3_ctx);
		if (ret != 0)
			return ret;
	}

	// File Packet, Directory Packet, Root Packet
	ret = make_file_packet(par3_ctx);
	if (ret != 0)
		return ret;

	// External Data Packet
	ret = make_ext_data_packet(par3_ctx);
	if (ret != 0)
		return ret;

	// Try Index File
	try_index_file(par3_ctx);

	// Try PAR3 files with input blocks
	if ( (par3_ctx->block_count > 0) && (command_data_packet != 0) ){
		ret = duplicate_common_packet(par3_ctx);
		if (ret != 0)
			return ret;
		ret = try_archive_file(par3_ctx, command_recovery_file_scheme);
		if (ret != 0)
			return ret;
	}

	return 0;
}

int par3_create(PAR3_CTX *par3_ctx,
	char command_recovery_file_scheme,
	char command_data_packet,
	char command_deduplication)
{
	int ret;

	// Load input blocks on memory.
	if (par3_ctx->block_count == 0){
		ret = map_chunk_tail(par3_ctx);
	} else if (command_deduplication == '1'){	// Simple deduplication
		ret = map_input_block(par3_ctx);
	} else if (command_deduplication == '2'){	// Deduplication with slide search
		ret = map_input_block_slide(par3_ctx);
	} else {
		ret = map_input_block_simple(par3_ctx);
	}
	if (ret != 0)
		return ret;

	// Creator Packet, Comment Packet, Start Packet
	ret = make_start_packet(par3_ctx);
	if (ret != 0)
		return ret;

	// Only when recovery blocks will be created, make Matrix Packet.
	if (par3_ctx->recovery_block_count > 0){
		ret = make_matrix_packet(par3_ctx);
		if (ret != 0)
			return ret;
	}

	// File Packet, Directory Packet, Root Packet
	ret = make_file_packet(par3_ctx);
	if (ret != 0)
		return ret;

	// External Data Packet
	ret = make_ext_data_packet(par3_ctx);
	if (ret != 0)
		return ret;

	// Write Index File
	ret = write_index_file(par3_ctx);
	if (ret != 0)
		return ret;

	// Write PAR3 files with input blocks
	if ( (par3_ctx->block_count > 0) && (command_data_packet != 0) ){
		ret = duplicate_common_packet(par3_ctx);
		if (ret != 0)
			return ret;
		ret = write_archive_file(par3_ctx, command_recovery_file_scheme);
		if (ret != 0)
			return ret;
	}




/*
{
	FILE *fp;
	fp = fopen("debug_file.txt", "wb");
	if (fp != NULL){
	
		fwrite(par3_ctx->common_packet, 1, par3_ctx->common_packet_size, fp);
	
		fclose(fp);
	}
}
*/


	return 0;
}

// This function releases all allocated memory.
void par3_release(PAR3_CTX *par3_ctx)
{
	if (par3_ctx->input_file_name){
		free(par3_ctx->input_file_name);
		par3_ctx->input_file_name = NULL;
	}
	if (par3_ctx->input_file_list){
		free(par3_ctx->input_file_list);
		par3_ctx->input_file_list = NULL;
	}
	if (par3_ctx->input_dir_name){
		free(par3_ctx->input_dir_name);
		par3_ctx->input_dir_name = NULL;
	}
	if (par3_ctx->input_dir_list){
		free(par3_ctx->input_dir_list);
		par3_ctx->input_dir_list = NULL;
	}
	if (par3_ctx->chunk_list){
		free(par3_ctx->chunk_list);
		par3_ctx->chunk_list = NULL;
	}
	if (par3_ctx->map_list){
		free(par3_ctx->map_list);
		par3_ctx->map_list = NULL;
	}
	if (par3_ctx->block_list){
		free(par3_ctx->block_list);
		par3_ctx->block_list = NULL;
	}
	if (par3_ctx->input_block){
		free(par3_ctx->input_block);
		par3_ctx->input_block = NULL;
	}
	if (par3_ctx->work_buf){
		free(par3_ctx->work_buf);
		par3_ctx->work_buf = NULL;
	}
	if (par3_ctx->crc_list){
		free(par3_ctx->crc_list);
		par3_ctx->crc_list = NULL;
	}
	if (par3_ctx->creator_packet){
		free(par3_ctx->creator_packet);
		par3_ctx->creator_packet = NULL;
	}
	if (par3_ctx->comment_packet){
		free(par3_ctx->comment_packet);
		par3_ctx->comment_packet = NULL;
	}
	if (par3_ctx->matrix_packet){
		free(par3_ctx->matrix_packet);
		par3_ctx->matrix_packet = NULL;
	}
	if (par3_ctx->file_packet){
		free(par3_ctx->file_packet);
		par3_ctx->file_packet = NULL;
	}
	if (par3_ctx->dir_packet){
		free(par3_ctx->dir_packet);
		par3_ctx->dir_packet = NULL;
	}
	if (par3_ctx->root_packet){
		free(par3_ctx->root_packet);
		par3_ctx->root_packet = NULL;
	}
	if (par3_ctx->ext_data_packet){
		free(par3_ctx->ext_data_packet);
		par3_ctx->ext_data_packet = NULL;
	}
	if (par3_ctx->common_packet){
		free(par3_ctx->common_packet);
		par3_ctx->common_packet = NULL;
	}
}
