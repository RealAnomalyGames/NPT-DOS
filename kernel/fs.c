#include <stddef.h>
#include "fs.h"

typedef struct {
    int used;
    char name[FS_NAME_MAX + 1];
    char content[FS_CONTENT_MAX + 1];
    size_t size;
} fs_file_t;

static fs_file_t files[FS_MAX_FILES];

static char fs_to_upper(char c)
{
    if (c >= 'a' && c <= 'z') {
        return (char)(c - 'a' + 'A');
    }

    return c;
}

static int fs_string_equal(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        if (fs_to_upper(*a) != fs_to_upper(*b)) {
            return 0;
        }

        a++;
        b++;
    }

    return *a == '\0' && *b == '\0';
}

static int fs_validate_name(const char *name)
{
    size_t length = 0;

    if (name == NULL || name[0] == '\0') {
        return 0;
    }

    while (name[length] != '\0') {
        char c = name[length];

        if (c <= 32 || c >= 127 ||
            c == '/' || c == '\\' ||
            c == ':' || c == '*') {
            return 0;
        }

        length++;

        if (length > FS_NAME_MAX) {
            return 0;
        }
    }

    return 1;
}

static int fs_find_index(const char *name)
{
    int i;

    for (i = 0; i < FS_MAX_FILES; i++) {
        if (files[i].used &&
            fs_string_equal(files[i].name, name)) {
            return i;
        }
    }

    return -1;
}

static int fs_find_free_slot(void)
{
    int i;

    for (i = 0; i < FS_MAX_FILES; i++) {
        if (!files[i].used) {
            return i;
        }
    }

    return -1;
}

void fs_initialize(void)
{
    int i;
    int j;

    for (i = 0; i < FS_MAX_FILES; i++) {
        files[i].used = 0;
        files[i].size = 0;

        for (j = 0; j <= FS_NAME_MAX; j++) {
            files[i].name[j] = '\0';
        }

        for (j = 0; j <= FS_CONTENT_MAX; j++) {
            files[i].content[j] = '\0';
        }
    }
}

fs_result_t fs_create(const char *name)
{
    int slot;
    size_t i;

    if (!fs_validate_name(name)) {
        return FS_INVALID_NAME;
    }

    if (fs_find_index(name) >= 0) {
        return FS_ALREADY_EXISTS;
    }

    slot = fs_find_free_slot();

    if (slot < 0) {
        return FS_FULL;
    }

    files[slot].used = 1;
    files[slot].size = 0;

    for (i = 0; i <= FS_NAME_MAX; i++) {
        files[slot].name[i] = '\0';
    }

    for (i = 0; name[i] != '\0'; i++) {
        files[slot].name[i] = name[i];
    }

    files[slot].content[0] = '\0';

    return FS_OK;
}

fs_result_t fs_write(const char *name, const char *text)
{
    int slot;
    size_t length = 0;
    size_t i;
    fs_result_t result;

    if (text == NULL) {
        return FS_INVALID_NAME;
    }

    while (text[length] != '\0') {
        length++;

        if (length > FS_CONTENT_MAX) {
            return FS_TOO_LARGE;
        }
    }

    slot = fs_find_index(name);

    if (slot < 0) {
        result = fs_create(name);

        if (result != FS_OK) {
            return result;
        }

        slot = fs_find_index(name);
    }

    for (i = 0; i < length; i++) {
        files[slot].content[i] = text[i];
    }

    files[slot].content[length] = '\0';
    files[slot].size = length;

    return FS_OK;
}

const char *fs_read(const char *name)
{
    int slot = fs_find_index(name);

    if (slot < 0) {
        return NULL;
    }

    return files[slot].content;
}

fs_result_t fs_delete(const char *name)
{
    int slot;
    size_t i;

    if (!fs_validate_name(name)) {
        return FS_INVALID_NAME;
    }

    slot = fs_find_index(name);

    if (slot < 0) {
        return FS_NOT_FOUND;
    }

    files[slot].used = 0;
    files[slot].size = 0;

    for (i = 0; i <= FS_NAME_MAX; i++) {
        files[slot].name[i] = '\0';
    }

    for (i = 0; i <= FS_CONTENT_MAX; i++) {
        files[slot].content[i] = '\0';
    }

    return FS_OK;
}

int fs_file_count(void)
{
    int count = 0;
    int i;

    for (i = 0; i < FS_MAX_FILES; i++) {
        if (files[i].used) {
            count++;
        }
    }

    return count;
}

const char *fs_filename_at(int index)
{
    int current = 0;
    int i;

    if (index < 0) {
        return NULL;
    }

    for (i = 0; i < FS_MAX_FILES; i++) {
        if (files[i].used) {
            if (current == index) {
                return files[i].name;
            }

            current++;
        }
    }

    return NULL;
}

int fs_file_size(const char *name)
{
    int slot = fs_find_index(name);

    if (slot < 0) {
        return -1;
    }

    return (int)files[slot].size;
}
