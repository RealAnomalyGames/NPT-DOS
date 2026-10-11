#ifndef NPT_DOS_FS_H
#define NPT_DOS_FS_H

#define FS_MAX_FILES 16
#define FS_NAME_MAX 15
#define FS_CONTENT_MAX 255

typedef enum {
    FS_OK,
    FS_NOT_FOUND,
    FS_ALREADY_EXISTS,
    FS_FULL,
    FS_INVALID_NAME,
    FS_TOO_LARGE
} fs_result_t;

void fs_initialize(void);

fs_result_t fs_create(const char *name);
fs_result_t fs_write(const char *name, const char *text);
const char *fs_read(const char *name);
fs_result_t fs_delete(const char *name);

int fs_file_count(void);
const char *fs_filename_at(int index);
int fs_file_size(const char *name);

#endif
