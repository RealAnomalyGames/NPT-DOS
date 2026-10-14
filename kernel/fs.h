#ifndef NPT_DOS_FS_H
#define NPT_DOS_FS_H

#include <stdint.h>

#define FS_MAX_FILES 16
#define FS_NAME_MAX 15
#define FS_CONTENT_MAX 255

#define FS_ATTR_READ_ONLY 0x01
#define FS_ATTR_HIDDEN    0x02
#define FS_ATTR_SYSTEM    0x04
#define FS_ATTR_MASK      0x07

typedef enum {
    FS_OK,
    FS_NOT_FOUND,
    FS_ALREADY_EXISTS,
    FS_FULL,
    FS_INVALID_NAME,
    FS_TOO_LARGE,
    FS_NOT_READY,
    FS_READ_ONLY,
    FS_IO_ERROR
} fs_result_t;

/* Existing filesystem API. */
void fs_initialize(void);
int fs_is_ready(void);
fs_result_t fs_format(void);

fs_result_t fs_create(const char *name);
fs_result_t fs_write(const char *name, const char *text);
const char *fs_read(const char *name);
fs_result_t fs_delete(const char *name);
fs_result_t fs_rename(const char *old_name, const char *new_name);

int fs_file_count(void);
const char *fs_filename_at(int index);
int fs_file_size(const char *name);

int fs_get_attributes(const char *name);
fs_result_t fs_set_attributes(const char *name, int attributes);

/* Device-aware filesystem API. */
int fs_select_device(uint32_t device_id);
int fs_get_device_id(uint32_t *device_id);
int fs_initialize_device(uint32_t device_id);
fs_result_t fs_format_device(uint32_t device_id);

#endif
