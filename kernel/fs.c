#include <stddef.h>
#include <stdint.h>
#include "ata.h"
#include "fs.h"

#define SECTOR_SIZE 512
#define SUPERBLOCK_LBA 1
#define DIRECTORY_LBA 2
#define DATA_LBA_BASE 3
#define ENTRY_SIZE 32

static uint8_t directory[SECTOR_SIZE];
static uint8_t sector_buffer[SECTOR_SIZE];
static uint32_t filesystem_device_id = 0xFFFFFFFFU;
static char read_buffer[FS_CONTENT_MAX + 1];
static int filesystem_ready;
static int disk_available;

static void clear_buffer(uint8_t *buffer)
{
    size_t i;

    for (i = 0; i < SECTOR_SIZE; i++) {
        buffer[i] = 0;
    }
}

static int strings_equal_ignore_case(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        char ca = *a;
        char cb = *b;

        if (ca >= 'a' && ca <= 'z') ca -= 'a' - 'A';
        if (cb >= 'a' && cb <= 'z') cb -= 'a' - 'A';

        if (ca != cb) return 0;

        a++;
        b++;
    }

    return *a == '\0' && *b == '\0';
}

static int valid_name(const char *name)
{
    size_t length = 0;

    if (name == NULL || name[0] == '\0') return 0;

    while (name[length] != '\0') {
        char c = name[length];

        if (c <= 32 || c >= 127 ||
            c == '/' || c == '\\' ||
            c == ':' || c == '*' ||
            c == '?' || c == '"' ||
            c == '<' || c == '>' || c == '|') {
            return 0;
        }

        length++;

        if (length > FS_NAME_MAX) return 0;
    }

    return 1;
}

static uint8_t *entry_at(int index)
{
    return &directory[index * ENTRY_SIZE];
}

static int entry_used(int index)
{
    return entry_at(index)[0] == 1;
}

static int find_file(const char *name)
{
    int i;

    for (i = 0; i < FS_MAX_FILES; i++) {
        uint8_t *entry = entry_at(i);

        if (entry[0] == 1 &&
            strings_equal_ignore_case((const char *)&entry[1], name)) {
            return i;
        }
    }

    return -1;
}

static int find_free_entry(void)
{
    int i;

    for (i = 0; i < FS_MAX_FILES; i++) {
        if (!entry_used(i)) return i;
    }

    return -1;
}

static fs_result_t save_directory(void)
{
    if (!filesystem_ready) return FS_NOT_READY;

    if (!storage_write_sector(filesystem_device_id, DIRECTORY_LBA, directory)) {
        return FS_IO_ERROR;
    }

    return FS_OK;
}

static int fs_get_storage_info(
    uint32_t device_id,
    storage_device_info_t *result)
{
    uint32_t i;
    uint32_t count = storage_device_count();

    for (i = 0; i < count; i++) {
        storage_device_info_t info;

        if (!storage_get_device_info_at(i, &info)) {
            continue;
        }

        if (info.id == device_id) {
            if (result != NULL) {
                *result = info;
            }

            return 1;
        }
    }

    return 0;
}

void fs_initialize(void)
{
    filesystem_ready = 0;
    filesystem_device_id = 0xFFFFFFFFU;

    (void)fs_initialize_device(0U);
}

int fs_initialize_device(uint32_t device_id)
{
    uint8_t superblock[SECTOR_SIZE];
    storage_device_info_t info;

    filesystem_ready = 0;
    filesystem_device_id = device_id;

    if (!fs_get_storage_info(device_id, &info)) {
        return 0;
    }

    /* LBAs 0 through 18 are required by NPTFS v1. */
    if (info.sector_count < 19U) {
        return 0;
    }

    if (!storage_read_sector(
            device_id, SUPERBLOCK_LBA, superblock)) {
        return 0;
    }

    if (superblock[0] != 'N' ||
        superblock[1] != 'P' ||
        superblock[2] != 'T' ||
        superblock[3] != 'F' ||
        superblock[4] != 'S' ||
        superblock[5] != '0' ||
        superblock[6] != '0' ||
        superblock[7] != '1' ||
        superblock[8] != 1) {
        return 0;
    }

    if (!storage_read_sector(
            device_id, DIRECTORY_LBA, directory)) {
        return 0;
    }

    filesystem_ready = 1;
    return 1;
}

int fs_select_device(uint32_t device_id)
{
    return fs_initialize_device(device_id);
}

int fs_get_device_id(uint32_t *device_id)
{
    if (device_id == NULL || filesystem_device_id == 0xFFFFFFFFU) {
        return 0;
    }

    *device_id = filesystem_device_id;
    return 1;
}

int fs_is_ready(void)
{
    return filesystem_ready;
}

fs_result_t fs_format_device(uint32_t device_id)
{
    uint8_t superblock[SECTOR_SIZE];
    storage_device_info_t info;
    int i;

    filesystem_ready = 0;
    filesystem_device_id = device_id;

    if (!fs_get_storage_info(device_id, &info)) {
        return FS_NOT_READY;
    }

    if (info.sector_count < 19U) {
        return FS_NOT_READY;
    }

    /* Invalidate any previous NPTFS signature first. */
    clear_buffer(superblock);

    if (!storage_write_sector(
            device_id, SUPERBLOCK_LBA, superblock)) {
        return FS_IO_ERROR;
    }

    clear_buffer(directory);

    if (!storage_write_sector(
            device_id, DIRECTORY_LBA, directory)) {
        return FS_IO_ERROR;
    }

    clear_buffer(sector_buffer);

    for (i = 0; i < FS_MAX_FILES; i++) {
        if (!storage_write_sector(
                device_id,
                DATA_LBA_BASE + (uint32_t)i,
                sector_buffer)) {
            return FS_IO_ERROR;
        }
    }

    superblock[0] = 'N';
    superblock[1] = 'P';
    superblock[2] = 'T';
    superblock[3] = 'F';
    superblock[4] = 'S';
    superblock[5] = '0';
    superblock[6] = '0';
    superblock[7] = '1';
    superblock[8] = 1;

    if (!storage_write_sector(
            device_id, SUPERBLOCK_LBA, superblock)) {
        return FS_IO_ERROR;
    }

    filesystem_ready = 1;
    return FS_OK;
}

fs_result_t fs_format(void)
{
    if (filesystem_device_id == 0xFFFFFFFFU) {
        return FS_NOT_READY;
    }

    return fs_format_device(filesystem_device_id);
}

fs_result_t fs_create(const char *name)
{
    int index;
    size_t i;
    uint8_t *entry;
    fs_result_t result;

    if (!filesystem_ready) return FS_NOT_READY;
    if (!valid_name(name)) return FS_INVALID_NAME;
    if (find_file(name) >= 0) return FS_ALREADY_EXISTS;

    index = find_free_entry();

    if (index < 0) return FS_FULL;

    entry = entry_at(index);

    for (i = 0; i < ENTRY_SIZE; i++) entry[i] = 0;

    entry[0] = 1;

    for (i = 0; name[i] != '\0'; i++) {
        entry[1 + i] = (uint8_t)name[i];
    }

    entry[17] = 0;
    entry[18] = 0;

    result = save_directory();

    if (result != FS_OK) {
        entry[0] = 0;
        return result;
    }

    return FS_OK;
}

fs_result_t fs_write(const char *name, const char *text)
{
    int index;
    size_t length = 0;
    size_t i;
    uint8_t *entry;
    fs_result_t result;
    int created = 0;

    if (!filesystem_ready) return FS_NOT_READY;
    if (!valid_name(name) || text == NULL) return FS_INVALID_NAME;

    while (text[length] != '\0') {
        length++;

        if (length > FS_CONTENT_MAX) return FS_TOO_LARGE;
    }

    index = find_file(name);

    if (index >= 0 &&
        (entry_at(index)[19] & FS_ATTR_READ_ONLY)) {
        return FS_READ_ONLY;
    }

    if (index < 0) {
        result = fs_create(name);

        if (result != FS_OK) return result;

        index = find_file(name);
        created = 1;
    }

    if (index < 0) return FS_IO_ERROR;

    clear_buffer(sector_buffer);

    for (i = 0; i < length; i++) {
        sector_buffer[i] = (uint8_t)text[i];
    }

    if (!storage_write_sector(filesystem_device_id, DATA_LBA_BASE + (uint32_t)index,
                          sector_buffer)) {
        return FS_IO_ERROR;
    }

    entry = entry_at(index);
    entry[17] = (uint8_t)(length & 0xFF);
    entry[18] = (uint8_t)((length >> 8) & 0xFF);

    result = save_directory();

    if (result != FS_OK && created) {
        entry[0] = 0;
    }

    return result;
}

const char *fs_read(const char *name)
{
    int index;
    uint8_t *entry;
    unsigned int length;
    unsigned int i;

    if (!filesystem_ready || !valid_name(name)) return NULL;

    index = find_file(name);

    if (index < 0) return NULL;

    entry = entry_at(index);
    length = (unsigned int)entry[17]
           | ((unsigned int)entry[18] << 8);

    if (length > FS_CONTENT_MAX) return NULL;

    if (!storage_read_sector(filesystem_device_id, DATA_LBA_BASE + (uint32_t)index,
                         sector_buffer)) {
        return NULL;
    }

    for (i = 0; i < length; i++) {
        read_buffer[i] = (char)sector_buffer[i];
    }

    read_buffer[length] = '\0';

    return read_buffer;
}

fs_result_t fs_delete(const char *name)
{
    int index;
    uint8_t *entry;
    size_t i;
    fs_result_t result;

    if (!filesystem_ready) return FS_NOT_READY;
    if (!valid_name(name)) return FS_INVALID_NAME;

    index = find_file(name);

    if (index < 0) return FS_NOT_FOUND;

    if (entry_at(index)[19] & FS_ATTR_READ_ONLY) {
        return FS_READ_ONLY;
    }

    entry = entry_at(index);

    for (i = 0; i < ENTRY_SIZE; i++) entry[i] = 0;

    result = save_directory();

    if (result != FS_OK) return result;

    return FS_OK;
}

int fs_file_count(void)
{
    int count = 0;
    int i;

    if (!filesystem_ready) return 0;

    for (i = 0; i < FS_MAX_FILES; i++) {
        if (entry_used(i)) count++;
    }

    return count;
}

const char *fs_filename_at(int index)
{
    int current = 0;
    int i;

    if (!filesystem_ready || index < 0) return NULL;

    for (i = 0; i < FS_MAX_FILES; i++) {
        if (entry_used(i)) {
            if (current == index) {
                return (const char *)&entry_at(i)[1];
            }

            current++;
        }
    }

    return NULL;
}

int fs_file_size(const char *name)
{
    int index;
    uint8_t *entry;

    if (!filesystem_ready || !valid_name(name)) return -1;

    index = find_file(name);

    if (index < 0) return -1;

    entry = entry_at(index);

    return (int)entry[17] | ((int)entry[18] << 8);
}


fs_result_t fs_rename(const char *old_name, const char *new_name)
{
    int old_index;
    int new_index;
    uint8_t *entry;
    uint8_t old_name_bytes[FS_NAME_MAX + 1];
    size_t i;
    fs_result_t result;

    if (!filesystem_ready) return FS_NOT_READY;
    if (!valid_name(old_name) || !valid_name(new_name)) {
        return FS_INVALID_NAME;
    }

    old_index = find_file(old_name);

    if (old_index < 0) return FS_NOT_FOUND;

    new_index = find_file(new_name);

    if (new_index >= 0 && new_index != old_index) {
        return FS_ALREADY_EXISTS;
    }

    entry = entry_at(old_index);

    for (i = 0; i < FS_NAME_MAX + 1; i++) {
        old_name_bytes[i] = entry[1 + i];
    }

    for (i = 0; i < FS_NAME_MAX + 1; i++) {
        entry[1 + i] = 0;
    }

    for (i = 0; new_name[i] != '\0'; i++) {
        entry[1 + i] = (uint8_t)new_name[i];
    }

    result = save_directory();

    if (result != FS_OK) {
        for (i = 0; i < FS_NAME_MAX + 1; i++) {
            entry[1 + i] = old_name_bytes[i];
        }

        return result;
    }

    return FS_OK;
}

int fs_get_attributes(const char *name)
{
    int index;

    if (!filesystem_ready || !valid_name(name)) {
        return -1;
    }

    index = find_file(name);

    if (index < 0) return -1;

    return (int)entry_at(index)[19] & FS_ATTR_MASK;
}

fs_result_t fs_set_attributes(const char *name, int attributes)
{
    int index;
    uint8_t *entry;
    uint8_t previous;
    fs_result_t result;

    if (!filesystem_ready) return FS_NOT_READY;
    if (!valid_name(name)) return FS_INVALID_NAME;

    if (attributes < 0 ||
        (attributes & ~FS_ATTR_MASK) != 0) {
        return FS_INVALID_NAME;
    }

    index = find_file(name);

    if (index < 0) return FS_NOT_FOUND;

    entry = entry_at(index);
    previous = entry[19];
    entry[19] = (uint8_t)attributes;

    result = save_directory();

    if (result != FS_OK) {
        entry[19] = previous;
        return result;
    }

    return FS_OK;
}
