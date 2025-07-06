#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <termios.h>
#include <ctype.h>
#include <stdbool.h>
#include <fcntl.h>

#define INPUT_BUF 200
#define PERMS 0644
#define FILENAME_SIZE 200
#define BLOCK_SIZE 512
#define MAX_FILE_SIZE 3000
#define MAX_FILES_PER_DIR 10
#define DATABLOCK_NUM 5
#define BITMAP_SIZE 512

typedef struct {
    unsigned int datablocks[DATABLOCK_NUM];
} Datastream;

typedef struct {
    int size;
    int datablocks[DATABLOCK_NUM]; 
} FileData;

typedef struct {
    unsigned int nodeid;
    unsigned int offset;
    char filename[FILENAME_SIZE];
    unsigned int size;
    unsigned int type;
    unsigned int parent_nodeid;
    unsigned int parent_offset;
    time_t creation_time;
    time_t access_time;
    unsigned int permissions;
    time_t modification_time;
    Datastream data;
    FileData file_data;
} MDS;

typedef struct {
    unsigned int datablocks_size;
    unsigned int metadata_size;
    unsigned int root_mds_offset;
    unsigned int latest_nodeid;
} Superblock;

typedef struct {
    unsigned char array[BITMAP_SIZE];
} Bitmap;

typedef struct list_node {
    unsigned int nodeid;
    unsigned int offset;
    char filename[FILENAME_SIZE];
    struct list_node *parent_dir;
} list_node;

typedef struct {
    unsigned int nodeid;
    unsigned int offset;
    char filename[FILENAME_SIZE];
    bool active;
} data_type;

void add_dir_to_path(list_node**, unsigned int, unsigned int, char*);
void back_to_path(list_node**, unsigned int, int);
int parent_offset(list_node **);
void print_current_path(list_node**);
int edit_commands(char*, int, list_node**);
void fs_create(char*, int , int , int, int);
int fs_workwith(int, char*, list_node**);
int fs_mkdir(int, char*, list_node**);
int fs_touch(int, bool, bool, char*, list_node**);
void fs_pwd(int, list_node**);
void fs_cd(int, list_node**, char*);
void fs_ls(int, bool, bool, bool, bool, bool, bool, char*, list_node**);
void fs_mv(int,  list_node**, char*, char*, bool);
void fs_rm(int,  list_node **, char *, bool, bool);
void fs_ln(int,  list_node **, char*, char*);
void fs_import(int,  list_node**, char*, char*);
void fs_export(int,  list_node**, char*, char*);
void fs_cat(int,  list_node**, char*, char*);
int find_path(int, list_node**, char *, bool);
void destroy_list(list_node **);
void add_to_bitmap(int, int);
int get_space(int);
void delete_from_bitmap(int, int);
void fs_modify(int fs_file, list_node **current, char *path, unsigned int permissions);
void fs_edit(int fs_file, list_node **current, char *filename);
void fs_read_file(int fs_file, char *filename, list_node **current);

int main() {
    int fs_file = -1;
    list_node *current = NULL;
    char input[INPUT_BUF];
    print_current_path(&current); printf(">"); fgets(input, INPUT_BUF, stdin);
    while(strcmp(input, "exit\n")) {
        fs_file = edit_commands(input, fs_file, &current);
        memset(input, 0, INPUT_BUF);
        print_current_path(&current); printf(">"); fgets(input, INPUT_BUF, stdin);
    }
    destroy_list(&current);
}

int edit_commands(char *command, int fs_file, list_node **current) {
    if (strncmp(command, (char*)"fs_create ", 10) == 0 && strlen(command) > 11) {
        char *check_options = &(command[10]);
        char *options = strtok(check_options, "\n");
        char *flags = strtok(options, "-");
        char name[FILENAME_SIZE], *flag_name;
        int i = 0;
        strcpy(name, flags);
        int bs = BLOCK_SIZE, fns = FILENAME_SIZE, cfs = MAX_FILE_SIZE, mdfn = MAX_FILES_PER_DIR;
        while(flags != NULL) {
            if(i == 1) memset(name, 0, FILENAME_SIZE);
            if(strncmp(flags, (char*)"bs", 2) == 0) sscanf(flags, "%*s %d %s", &bs, name);
            else if(strncmp(flags, (char*)"cfs", 3) == 0) sscanf(flags, "%*s %d %s", &cfs, name);
            else if(strncmp(flags, (char*)"fns", 3) == 0) sscanf(flags, "%*s %d %s", &fns, name);
            else if(strncmp(flags, (char*)"mdfn", 4) == 0) sscanf(flags, "%*s %d %s", &mdfn, name);
            flags = strtok(NULL, "-");
            i++;
        }
        if (strlen(name) > 0) fs_create(name, bs, fns, cfs, mdfn);
        else printf("Name for fs_file is missing.\n");
        return -1;
    }

    else if(strncmp(command, "fs_edit ", 8) == 0 && strlen(command) > 9) {
        char *filename = command + 8;
        strtok(filename, "\n");
        fs_edit(fs_file, current, filename);
    }
    else if(strncmp(command, (char*)"fs_workwith ", 12) == 0 && strlen(command) > 13) {
        char *check_file = &(command[12]);
        char *file = strtok(check_file, "\n");
        fs_file = fs_workwith(fs_file, file, current);
    }
    else if(strncmp(command, (char*)"fs_mkdir ", 9) == 0 && strlen(command) > 10) {
        char *check_dirs = &(command[9]);
        char *dirs = strtok(check_dirs, "\n");
        fs_mkdir(fs_file, dirs, current);
    }
    else if(strncmp(command, (char*)"fs_touch ", 9) == 0 && strlen(command) > 10) {
        strtok(command, "\n");
        int index = 0, size = strlen(command) + 1;
        char *check_path = malloc(size); memset(check_path, 0, size); strcpy(check_path, command);
        char options[FILENAME_SIZE];
        char filename[FILENAME_SIZE]; memset(filename, 0, FILENAME_SIZE);
        bool time_acc = true, time_edit = false;

        while(check_path[index] != '\0') {
            int ptr = 0; int index_begin = index;
            while((check_path[index] != ' ') && (check_path[index] != '\0')) {
                index++;
                ptr++;
            }
            memset(options, 0, FILENAME_SIZE);
            strncpy(options, check_path + index_begin, ptr);
            if(check_path[index] != '\0') index++;

            if(strcmp(options, (char*)"fs_touch") != 0) {
                if(strcmp(options, (char*)"-a") == 0) time_acc = true;
                else if(strcmp(options, (char*)"-m") == 0) time_edit = true;
                else { strcpy(filename, check_path + index_begin); break; }
            }
        }
        fs_touch(fs_file, time_acc, time_edit, filename, current);

        free(check_path);
    }
    else if(strncmp(command, (char*)"fs_pwd", 6) == 0) {
        fs_pwd(fs_file, current);
    }
    else if(strncmp(command, (char*)"fs_cd ", 6) == 0 && strlen(command) > 7) {
        char *check_path = &(command[6]);
        char *path = strtok(check_path, "\n");
        fs_cd(fs_file, current, path);
    }
    else if(strncmp(command, (char*)"fs_ls", 5) == 0 || (strncmp(command, (char*)"fs_ls ", 6) == 0 && strlen(command) > 7) {
        strtok(command, "\n");
        int index = 0, size = strlen(command) + 1;
        char *check_path = malloc(size); memset(check_path, 0, size); strcpy(check_path, command);
        char options[FILENAME_SIZE];
        bool a = false, r = false, l = false, u = false, d = false, h = false;
        char filename[FILENAME_SIZE]; memset(filename, 0, FILENAME_SIZE);
        while(check_path[index] != '\0') {
            int ptr = 0; int index_begin = index;
            while((check_path[index] != ' ') && (check_path[index] != '\0')) {
                index++;
                ptr++;
            }
            memset(options, 0, FILENAME_SIZE);
            strncpy(options, check_path + index_begin, ptr);
            if(check_path[index] != '\0') index++;

            if(strcmp(options, (char*)"fs_ls") != 0) {
                if(strcmp(options, (char*)"-a") == 0) a = true;
                else if(strcmp(options, (char*)"-r") == 0) r = true;
                else if(strcmp(options, (char*)"-l") == 0) l = true;
                else if(strcmp(options, (char*)"-u") == 0) u = true;
                else if(strcmp(options, (char*)"-d") == 0) d = true;
                else if(strcmp(options, (char*)"-h") == 0) h = true;
                else { strcpy(filename, check_path + index_begin); break; }
            }
        }
        fs_ls(fs_file, a, r, l, u, d, h, filename, current);

        free(check_path);
    }
    else if(strncmp(command, (char*)"fs_modify ", 10) == 0 && strlen(command) > 11) {
        char *check_path = &(command[10]);
        strtok(check_path, "\n");
        char *tmp = strrchr(command, ' '), *permissions_str;
        if(tmp != NULL) {
            permissions_str = tmp + 1;
            unsigned int permissions;
            if(sscanf(permissions_str, "%o", &permissions) == 1) {
                *tmp = '\0';
                fs_modify(fs_file, current, check_path, permissions);
            } else {
                printf("fs_modify: invalid permissions format (use octal, e.g., 755)\n");
            }
        }
        else {
            printf("fs_modify: missing file operand or permissions\n");
            printf("Usage: fs_modify <path> <permissions>\n");
        }
    }
    else if(strncmp(command, (char*)"fs_mv ", 6) == 0 && strlen(command) > 7) {
        char *check_path = &(command[6]);
        bool i = false;
        char path[FILENAME_SIZE]; memset(path, 0, FILENAME_SIZE);
        strtok(check_path, "\n");
        if(strncmp(command, "fs_mv -i ", 9) == 0 ) { i = true; strncpy(path, check_path + 3, strlen(check_path)); }
        else strncpy(path, check_path, strlen(check_path));

        char *tmp = check_path, *destination = check_path, *source = path;
        tmp = strrchr(command, ' ');
        if(tmp != NULL) {
            destination = tmp + 1;
            fs_mv(fs_file, current, source, destination, i);
        }
        else printf("fs_mv: missing file operand\n");
    }
    else if(strncmp(command, (char*)"fs_rm ", 6) == 0 && strlen(command) > 7) {
        bool r = false;
        char *check_path = &(command[6]);
        char *path = strtok(check_path, "\n"), *pathname = NULL;

        if(path != NULL) {
            if(strncmp(path, "-r", 2) == 0) {
                r = true;
                path += 2;
                while(*path != '\0') {
                    if(*path == ' ') path++;
                    else { pathname = path; break; }
                }
            }
            else pathname = path;
        }
        if(pathname != NULL) fs_rm(fs_file, current, pathname, r, true);
        else printf("fs_rm: missing file operand\n");
    }
    else if(strncmp(command, (char*)"fs_ln ", 6) == 0 && strlen(command) > 7) {
        char *check_path = &(command[6]);
        char *source = strtok(check_path, " \n"), *output;
        if (source != NULL) {
            output = strtok(NULL, " \n");
            if (output != NULL) fs_ln(fs_file, current, source, output);
            else printf("fs_ln: failed to create hard link '%s'\n", source);
        }
        else printf("fs_ln: missing file operand\n");
    }
    else if(strncmp(command, (char*)"fs_cat ", 7) == 0 && strlen(command) > 8) {
        strtok(command, "\n");
        int index = 0, size = strlen(command) + 1, sources_len = 0, begin_source = 0;
        char *check_path = malloc(size); memset(check_path, 0, size); strcpy(check_path, command);
        char options[FILENAME_SIZE]; memset(options, 0, FILENAME_SIZE);
        char filename[FILENAME_SIZE]; memset(filename, 0, FILENAME_SIZE);
        bool sources_found = false, found_o = false;

        while(check_path[index] != '\0') {
            int ptr = 0; int index_begin = index;
            if(strcmp(options, "fs_cat") == 0) {
                sources_found = true;
                begin_source = index;
            }
            if(strcmp(options, "-o") == 0) found_o = true;

            while((check_path[index] != ' ') && (check_path[index] != '\0')) {
                index++;
                ptr++;
                if(sources_found) sources_len++;
            }
            memset(options, 0, FILENAME_SIZE);
            strncpy(options, check_path + index_begin, ptr);
            if(check_path[index] != '\0') index++;
            if(strcmp(options, "-o") == 0) { sources_found = false; sources_len -= 2;}

            char *sources = malloc(strlen(check_path) + 1); memset(sources, 0, strlen(check_path) + 1);
            strncpy(sources, check_path + begin_source, sources_len + 1);
            if(index == strlen(check_path)) {
                if(found_o) fs_cat(fs_file, current, sources, options);
                else printf("fs_cat: missing file operand\n");
            }
            free(sources);
        }
        free(check_path);
    }
    else if(strncmp(command, (char*)"fs_cp ", 6) == 0 && strlen(command) > 7) {
        char *check_path = &(command[6]);
        char *path = strtok(check_path, "\n");
    }
    else if(strncmp(command, (char*)"fs_import ", 10) == 0 && strlen(command) > 11) {
        char *check_path = &(command[10]);
        strtok(check_path, "\n");
        char *tmp = strrchr(command, ' '), *directory;
        if(tmp != NULL) {
            directory = tmp + 1;
            fs_import(fs_file, current, check_path, directory);
        }
        else printf("fs_import: missing file operand\n");
    }
    else if(strncmp(command, (char*)"fs_export ", 10) == 0 && strlen(command) > 11) {
        char *check_path = &(command[10]);
        strtok(check_path, "\n");
        char *tmp = strrchr(command, ' '), *directory;
        if(tmp != NULL) {
            directory = tmp + 1;
            fs_export(fs_file, current, check_path, directory);
        }
        else printf("fs_export: missing file operand\n");
    }
    else { strtok(command, "\n"); printf("%s: command not found\n", command); }
    return fs_file;
}

void add_dir_to_path(list_node **current, unsigned int nodeid, unsigned int offset, char *filename) {
    list_node *new, *tmp = *current;
    new = (list_node *)malloc(sizeof(list_node));
    new->nodeid = nodeid;
    new->offset = offset;
    strncpy(new->filename, filename, FILENAME_SIZE);
    if (tmp != NULL) new->parent_dir = tmp;
    else { new->parent_dir = NULL; }
    (*current) = &(*new);
}

void destroy_list(list_node **current) {
    list_node *tmp = *current, *delete = *current;
    while(tmp != NULL) {
        delete = tmp->parent_dir;
        free(tmp);
        tmp = delete;
    }
    (*current) = NULL;
}

void back_to_path(list_node **current, unsigned int nodeid, int back_depth) {
    list_node *delete = *current, *tmp = *current;
    bool found = false;
    int i = 1;
    while((tmp != NULL) && (!found)) {
        if(back_depth == -1) { if (tmp->nodeid == nodeid) found = true; }
        else { if(i < back_depth) i++; else found = true; }
        (*current) = &(*(delete->parent_dir));
        free(delete);
        tmp = *current;
    }
}

int parent_offset(list_node **current) {
    list_node *tmp = *current;
    int par_off = -1;
    if (tmp != NULL) {
        if (tmp->parent_dir != NULL) {
            tmp = tmp->parent_dir;
            par_off = tmp->offset;
        }
    }
    return par_off;
}

void print_current_path(list_node **current) {
    list_node *tmp = *current;
    char *full_path = malloc(FILENAME_SIZE * sizeof(char)), *current_path = malloc(FILENAME_SIZE * sizeof(char));
    memset(full_path, 0, sizeof(char) * FILENAME_SIZE);
    while(tmp != NULL) {
        free(current_path); current_path = malloc(strlen(full_path) + strlen(tmp->filename) + 2);
        sprintf(current_path, "/%s%s", tmp->filename, full_path);
        free(full_path); full_path = malloc(strlen(current_path) + 1);
        sprintf(full_path, "%s", current_path);
        tmp = tmp->parent_dir;
    }
    printf("%s", full_path);
    free(current_path); free(full_path);
}

int find_path(int fs_file, list_node **current, char *path, bool change_pathlist) {
    int current_offset = (*current)->offset;
    bool relative_path = true;

    if(fs_file > 0) {
        Superblock *superblock = malloc(sizeof(Superblock));
        MDS mds, currentMDS;
        char str_path[FILENAME_SIZE]; memset(str_path, 0, FILENAME_SIZE); strcpy(str_path, path);
        char *full_path = str_path;
        if(strncmp(full_path, "/", 1) == 0) relative_path = false;
        full_path = strtok(str_path, "/");

        while(full_path != NULL) {
            if (relative_path) {
                if(strcmp(full_path, ".") == 0) {
                }
                else if(strcmp(full_path, "..") == 0) {
                    if((*current)->nodeid != 0) {
                        lseek(fs_file, 0, SEEK_SET);
                        read(fs_file, superblock, sizeof(Superblock));
                        lseek(fs_file, current_offset, SEEK_SET);
                        read(fs_file, &currentMDS, superblock->metadata_size);
                        current_offset = currentMDS.parent_offset;
                        if(change_pathlist) back_to_path(current, 0, 1);
                    }
                }
                else {
                    int fd_current, free_offset;
                    data_type data;
                    lseek(fs_file, 0, SEEK_SET);
                    read(fs_file, superblock, sizeof(Superblock));
                    bool exists_already = false;
                    lseek(fs_file, current_offset, SEEK_SET);
                    read(fs_file, &currentMDS, superblock->metadata_size);
                    for(int i = 0; i < DATABLOCK_NUM; i++) {
                        lseek(fs_file, currentMDS.data.datablocks[i], SEEK_SET);
                        for (int j = 0; j < superblock->datablocks_size/(sizeof(data_type)); j++) {
                            read(fs_file, &data, sizeof(data_type));
                            if(data.active == true) {
                                if(strcmp(data.filename, full_path) == 0) {
                                    lseek(fs_file, data.offset, SEEK_SET);
                                    read(fs_file, &mds, sizeof(MDS));
                                    if(mds.type == 2) {
                                        if(change_pathlist) add_dir_to_path(current, data.nodeid, data.offset, data.filename);
                                    }
                                    else if(change_pathlist) printf("%s: Not a directory\n", full_path);
                                    current_offset = data.offset;
                                    exists_already = true;
                                    i = DATABLOCK_NUM;
                                    break;
                                }
                            }
                        }
                    }
                    if(!exists_already) { free(superblock); return -1; }
                }
            }
            full_path = strtok(NULL, "/");
        }
        free(superblock);
    }
    return current_offset;
}

int find_file(int fs_file, char *filename) {
    if(fs_file > 0) {
        Superblock *superblock = malloc(sizeof(Superblock));
        int fd_current, free_offset, i = 0;
        data_type data;
        MDS mds, currentMDS;
        lseek(fs_file, 0, SEEK_SET);
        read(fs_file, superblock, sizeof(Superblock));
        bool exists_already = false;

        while(true) {
            lseek(fs_file, sizeof(Superblock) + sizeof(Bitmap) + (i * (superblock->metadata_size + (DATABLOCK_NUM * superblock->datablocks_size))), SEEK_SET);
            if(read(fs_file, &currentMDS, superblock->metadata_size) > 0) {
                if (strcmp(currentMDS.filename, filename) == 0) { free(superblock); return currentMDS.offset; }
            }
            else { free(superblock); return -1; }
            i++;
        }
    }
}

void add_to_bitmap(int offset, int fs_file) {
    int record_packet = 0, fs_place = 0, bitmap_byte_place = 0, bitmap_bit_place = 0;
    lseek(fs_file, 0, SEEK_SET);
    Superblock *superblock = malloc(sizeof(Superblock));
    read(fs_file, superblock, sizeof(Superblock));

    Bitmap *bitmap = malloc(sizeof(Bitmap));
    read(fs_file, bitmap, sizeof(Bitmap));
    record_packet = superblock->metadata_size + (superblock->datablocks_size) * DATABLOCK_NUM;

    offset = offset - sizeof(Superblock) - sizeof(Bitmap);
    fs_place = offset / record_packet;

    bitmap_byte_place = fs_place / 8;
    bitmap_bit_place = fs_place % 8;
    unsigned char one = 1;
    one = one << bitmap_bit_place;
    bitmap->array[bitmap_byte_place] = bitmap->array[bitmap_byte_place] | one;

    lseek(fs_file, sizeof(Superblock), SEEK_SET);
    write(fs_file, bitmap, sizeof(Bitmap));

    free(bitmap);
    free(superblock);
}

int get_space(int fs_file) {
    int i = 0, bitmap_bit_place = 0, record_packet = 0, fs_place = 0, offset = 0;
    lseek(fs_file, 0, SEEK_SET);

    Superblock *superblock = malloc(sizeof(Superblock));
    read(fs_file, superblock, sizeof(Superblock));
    record_packet = superblock->metadata_size + (superblock->datablocks_size) * DATABLOCK_NUM;

    Bitmap *bitmap = malloc(sizeof(Bitmap));
    read(fs_file, bitmap, sizeof(Bitmap));

    while(bitmap->array[i] == 255) i++;

    unsigned char a = bitmap->array[i];
    int j = 0;
    while(a != 0 && j < 8) {
        a = a >> 1;
        j++;
    }
    fs_place = i * 8 + j;
    offset = fs_place * record_packet + sizeof(Superblock) + sizeof(Bitmap);

    free(superblock); free(bitmap);
    return offset;
}

void delete_from_bitmap(int offset, int fs_file) {
    int record_packet = 0, fs_place = 0, bitmap_byte_place = 0, bitmap_bit_place = 0;
    lseek(fs_file, 0, SEEK_SET);
    Superblock *superblock = malloc(sizeof(Superblock));
    read(fs_file, superblock, sizeof(Superblock));

    Bitmap *bitmap = malloc(sizeof(Bitmap));
    read(fs_file, bitmap, sizeof(Bitmap));

    record_packet = superblock->metadata_size + (superblock->datablocks_size) * DATABLOCK_NUM;

    offset = offset - sizeof(Superblock) - sizeof(Bitmap);
    fs_place = offset / record_packet;
    bitmap_byte_place = fs_place / 8;
    bitmap_bit_place = fs_place % 8;
    unsigned char one = 1;
    one = one << bitmap_bit_place;
    one = ~one;
    bitmap->array[bitmap_byte_place] = bitmap->array[bitmap_byte_place] & one;

    lseek(fs_file, sizeof(Superblock), SEEK_SET);
    write(fs_file, bitmap, sizeof(Bitmap));

    free(bitmap);
    free(superblock);
}

void fs_create(char* fs_name, int datablock_size, int filenames_size, int max_file_size, int max_files_in_dirs) {
    int fs_file;

    char *name = malloc(sizeof(char) * strlen(fs_name) + 5);
    char *pathname = malloc(sizeof(char) * strlen(fs_name) + 7);
    strcpy(name, fs_name); strcat(name, ".fs");

    if((fs_file = open(name, O_CREAT | O_RDWR, PERMS)) < 0) {
        perror("Unable to create file.");
        exit(1);
    }
    lseek(fs_file, 0, SEEK_SET);

    Bitmap bitmap;
    memset(bitmap.array, 0, BITMAP_SIZE);

    Superblock superblock;
    superblock.datablocks_size = datablock_size;
    superblock.metadata_size = sizeof(MDS);
    superblock.root_mds_offset = sizeof(superblock) + sizeof(bitmap);
    superblock.latest_nodeid = 0;
    write(fs_file, &superblock, sizeof(superblock));
    write(fs_file, &bitmap, sizeof(bitmap));

    MDS root_mds;
    root_mds.nodeid = 0;
    root_mds.offset = superblock.root_mds_offset;
    root_mds.type = 2;
    root_mds.parent_nodeid = -1;
    root_mds.parent_offset = -1;
    strcpy(root_mds.filename, name);
    root_mds.creation_time = time(0); root_mds.access_time = time(0); root_mds.modification_time = time(0);
    root_mds.data.datablocks[0] = root_mds.offset + superblock.metadata_size;
    for(int i = 1; i < DATABLOCK_NUM; i++) {
        root_mds.data.datablocks[i] = root_mds.data.datablocks[i - 1] + superblock.datablocks_size;
    }
    write(fs_file, &root_mds, sizeof(root_mds));

    data_type data;
    data.nodeid = 0;
    data.offset = 0;
    memset(data.filename, 0, FILENAME_SIZE);
    data.active = false;

    for(int i = 0; i < DATABLOCK_NUM; i++) {
        lseek(fs_file, root_mds.data.datablocks[i], SEEK_SET);
        for (int j = 0; j < superblock.datablocks_size / (sizeof(data_type)); j++) {
            write(fs_file, &data, sizeof(data_type));
        }
    }
    add_to_bitmap(sizeof(Superblock) + sizeof(Bitmap), fs_file);

    close(fs_file);
    free(name); free(pathname);
}

int fs_workwith(int fs_file, char *filename, list_node **current){
    if(fs_file!=-1) { close(fs_file); back_to_path(current, 0, -1); }
    if((fs_file = open(filename, O_RDWR))<0) { perror("Unable to open file."); return -1;}
    else{
        lseek(fs_file, 0, SEEK_SET);
        unsigned int mds_offset, nodeid, offset;
        char *filename;
        Superblock *superblock = malloc(sizeof(Superblock));
        MDS *mds = malloc(sizeof(MDS));

        read(fs_file, superblock, sizeof(Superblock));
        mds_offset = superblock->root_mds_offset;

        lseek(fs_file, mds_offset, SEEK_SET);
        read(fs_file, mds, sizeof(MDS));
        nodeid = mds->nodeid;
        offset = mds->offset;
        filename = mds->filename;
        add_dir_to_path(current, nodeid, offset, filename);
        free(superblock); free(mds);
    }
    return fs_file;
}

int fs_mkdir(int fs_file, char *dirnames, list_node **current){
    int return_offset = -1;
    if(fs_file>0){
        Superblock *superblock = malloc(sizeof(Superblock));
        int free_offset;
        char *dirs = dirnames;
        strtok(dirs, " ");
        while(dirs!=NULL){
            data_type data;
            MDS mds, currentMDS;

            lseek(fs_file, 0, SEEK_SET);
            read(fs_file, superblock, sizeof(Superblock));

            free_offset = get_space(fs_file);
            return_offset = free_offset;
            superblock->latest_nodeid++;
            mds.nodeid = superblock->latest_nodeid;
            mds.offset = free_offset;
            mds.size = 0;
            mds.type = 2;
            mds.parent_nodeid = (*current)->nodeid;
            mds.parent_offset = (*current)->offset;
            strcpy(mds.filename, dirs);
            mds.creation_time = time(0); mds.access_time = time(0); mds.modification_time = time(0);
            mds.data.datablocks[0] = mds.offset + superblock->metadata_size;

            for(int i = 1; i < DATABLOCK_NUM; i++){
                mds.data.datablocks[i] = mds.data.datablocks[i-1] + superblock->datablocks_size;
            }

            lseek(fs_file, mds.offset, SEEK_SET);
            write(fs_file, &mds, superblock->metadata_size);

            lseek(fs_file, 0, SEEK_SET);
            write(fs_file, superblock, sizeof(Superblock));

            data.nodeid = 0;
            data.offset = 0;
            memset(data.filename, 0, FILENAME_SIZE);
            data.active = false;

            for(int i = 0; i < DATABLOCK_NUM; i++){
                lseek(fs_file, mds.data.datablocks[i], SEEK_SET);
                for (int j = 0; j < superblock->datablocks_size/(sizeof(data_type)); j++) {
                    write(fs_file, &data, sizeof(data_type));
                }
            }

            lseek(fs_file, (*current)->offset, SEEK_SET);
            read(fs_file, &currentMDS, superblock->metadata_size);

            for(int i = 0; i < DATABLOCK_NUM; i++) {
                lseek(fs_file, currentMDS.data.datablocks[i], SEEK_SET);
                for (int j = 0; j < superblock->datablocks_size/(sizeof(data_type)); j++) {
                    read(fs_file, &data, sizeof(data_type));
                    if(data.active == false){
                        data.nodeid = mds.nodeid;
                        data.offset = mds.offset;
                        strcpy(data.filename, mds.filename);
                        data.active = true;
                        lseek(fs_file, -sizeof(data_type), SEEK_CUR);
                        write(fs_file, &data, sizeof(data_type));
                        i = DATABLOCK_NUM;
                        break;
                    }
                }
            }
            add_to_bitmap(mds.offset, fs_file);
            dirs = strtok(NULL, " ");
        }
        free(superblock);
    }
    else printf("fs_mkdir: execute first fs_workwith\n");
    return return_offset;
}

int fs_touch(int fs_file, bool time_acc, bool time_edit, char *filenames, list_node **current){
    int fd_current, return_offset=-1;
    if(fs_file>0){
        Superblock *superblock = malloc(sizeof(Superblock));
        int free_offset, source_offset;
        int index=0, size = strlen(filenames)+1;
        char *check_path = malloc(size); memset(check_path, 0, size); strcpy(check_path, filenames);
        char str_source[FILENAME_SIZE];

        while(check_path[index]!='\0'){
            bool exists_already = false;
            bool empty_space = false;
            data_type data;
            MDS mds, currentMDS, fileMDS;
            lseek(fs_file, 0, SEEK_SET);
            read(fs_file, superblock, sizeof(Superblock));

            int ptr=0; int index_begin=index;
            while((check_path[index]!=' ') && (check_path[index] != '\0')){
                index++;
                ptr++;
            }
            memset(str_source, 0, FILENAME_SIZE);
            strncpy(str_source, check_path+index_begin, ptr);
            if(check_path[index] != '\0') index++;

            lseek(fs_file, (*current)->offset, SEEK_SET);
            read(fs_file, &currentMDS, superblock->metadata_size);
            for(int i = 0; i < DATABLOCK_NUM; i++){
                lseek(fs_file, currentMDS.data.datablocks[i], SEEK_SET);
                for (int j = 0; j < superblock->datablocks_size/(sizeof(data_type)); j++){
                    read(fs_file, &data, sizeof(data_type));
                    int current_pointer = lseek(fs_file, 0, SEEK_CUR);
                    if(data.active == true){
                        if(strcmp(data.filename, str_source) == 0){
                            if(time_acc || time_edit){
                                lseek(fs_file, data.offset, SEEK_SET);
                                read(fs_file, &fileMDS, superblock->metadata_size);
                                if(time_acc) fileMDS.access_time = time(0);
                                if(time_edit) fileMDS.modification_time = time(0);
                                lseek(fs_file, data.offset, SEEK_SET);
                                write(fs_file, &fileMDS, superblock->metadata_size);
                                lseek(fs_file, current_pointer, SEEK_SET);
                                return_offset = data.offset;
                            }
                            exists_already = true;
                            i = DATABLOCK_NUM;
                            break;
                        }
                    }
                    else{
                        empty_space = true;
                    }
                }
            }
            if(!exists_already && empty_space){
                free_offset = get_space(fs_file);
                return_offset = free_offset;
                superblock->latest_nodeid++;
                mds.nodeid = superblock->latest_nodeid;
                mds.offset = free_offset;
                mds.size = 0;
                mds.type = 1;
                mds.parent_nodeid = (*current)->nodeid;
                mds.parent_offset = (*current)->offset;
                strcpy(mds.filename, str_source);
                mds.creation_time = time(0); mds.access_time = time(0); mds.modification_time = time(0);
                mds.data.datablocks[0] = mds.offset + superblock->metadata_size;
                for(int i = 1; i < DATABLOCK_NUM; i++){
                    mds.data.datablocks[i] = mds.data.datablocks[i-1] + superblock->datablocks_size;
                }
                lseek(fs_file, mds.offset, SEEK_SET);
                write(fs_file, &mds, superblock->metadata_size);

                lseek(fs_file, 0, SEEK_SET);
                write(fs_file, superblock, sizeof(Superblock));

                for(int i = 0; i < DATABLOCK_NUM; i++){
                    lseek(fs_file, currentMDS.data.datablocks[i], SEEK_SET);
                    for (int j = 0; j < superblock->datablocks_size/(sizeof(data_type)); j++){
                        read(fs_file, &data, sizeof(data_type));
                        if(data.active == false){
                            data.nodeid = mds.nodeid;
                            data.offset = mds.offset;
                            strcpy(data.filename, mds.filename);
                            data.active = true;
                            lseek(fs_file, -sizeof(data_type), SEEK_CUR);
                            write(fs_file, &data, sizeof(data_type));
                            i = DATABLOCK_NUM;
                            break;
                        }
                    }
                }
                add_to_bitmap(mds.offset, fs_file);
            }
        }
        free(superblock); free(check_path);
    }
    else printf("fs_touch: execute first fs_workwith\n");
    return return_offset;
}

void fs_pwd(int fs_file, list_node **current){
    if(fs_file>0){
        print_current_path(current);
        printf("\n");
    }
    else printf("fs_pwd: execute first fs_workwith\n");
}

void fs_cd(int fs_file, list_node **current, char *path){
    if(fs_file>0) {
        if(find_path(fs_file, current, path, true)<0)
            printf("fs_cd: failed to access %s: no such file or directory\n", path);
    }
    else printf("fs_cd: execute first fs_workwith\n");
}

void fs_ls(int fs_file, bool a, bool r, bool l, bool u, bool d, bool h, char *filename, list_node **current){
    if(fs_file>0){
        Superblock *superblock = malloc(sizeof(Superblock));
        int index=0, size = strlen(filename)+1;
        char *check_path = malloc(size); memset(check_path, 0, size); strcpy(check_path, filename);
        char str_source[FILENAME_SIZE];
        bool must_print = true;
        while((check_path[index]!='\0') || must_print){
            data_type data;
            MDS currentMDS, fileMDS;

            lseek(fs_file, 0, SEEK_SET);
            Superblock *superblock = malloc(sizeof(Superblock));
            read(fs_file, superblock, sizeof(Superblock));

            int ptr=0; int index_begin=index;
            while((check_path[index]!=' ') && (check_path[index] != '\0')){
                index++;
                ptr++;
            }
            memset(str_source, 0, FILENAME_SIZE);
            strncpy(str_source, check_path+index_begin, ptr);
            if(check_path[index] != '\0') index++;

            lseek(fs_file, (*current)->offset, SEEK_SET);
            read(fs_file, &currentMDS, superblock->metadata_size);
            for(int i = 0; i < DATABLOCK_NUM; i++){
                lseek(fs_file, currentMDS.data.datablocks[i], SEEK_SET);
                for (int j = 0; j < superblock->datablocks_size/(sizeof(data_type)); j++){
                    read(fs_file, &data, sizeof(data_type));
                    int current_pointer = lseek(fs_file, 0, SEEK_CUR);
                    bool print=false, print_f=false, print_d=false, print_l=false, print_files=true;
                    lseek(fs_file, data.offset, SEEK_SET);
                    read(fs_file, &fileMDS, superblock->metadata_size);

                    if(u) print_f=true;
                    if(d) print_d=true;
                    if(h) print_l=true;
                    if(!u && !d && !h) { print_f=true; print_d=true; print_l=true; }

                    if(data.active == true){
                        if(str_source[0]!='\0'){
                            if(strcmp(str_source, data.filename)!=0) print_files=false;
                        }
                        if(print_files){
                            if(a){
                                if(fileMDS.type == 2) {
                                    if(print_d) { printf("%s \t",data.filename); print = true;}
                                    else print = false;
                                }
                                else if(fileMDS.type == 1) {
                                    if(print_f) { printf("%s \t",data.filename); print = true;}
                                    else print = false;
                                }
                                else {
                                    if(print_l) { printf("%s \t",data.filename); print = true;}
                                    else print = false;
                                }
                            }
                            else{
                                if(strncmp(data.filename, ".", 1)!=0) {
                                    if(fileMDS.type == 2) {
                                        if(print_d) { printf("%s \t",data.filename); print = true;}
                                        else print = false;
                                    }
                                    else if(fileMDS.type == 1) {
                                        if(print_f) { printf("%s \t",data.filename); print = true;}
                                        else print = false;
                                    }
                                    else {
                                        if(print_l) { printf("%s \t",data.filename); print = true;}
                                        else print = false;
                                    }
                                }
                            }
                        if(l) {
                            if(print) {
                                char create[20], access[20], modify[20];
                                strftime(create, 20, "%F %H:%M:%S", localtime(&fileMDS.creation_time));
                                strftime(access, 20, "%F %H:%M:%S", localtime(&fileMDS.access_time));
                                strftime(modify, 20, "%F %H:%M:%S", localtime(&fileMDS.modification_time));
                                char perm_str[10];
                                sprintf(perm_str, "%c%c%c%c%c%c%c%c%c",
                                    (fileMDS.permissions & 0400) ? 'r' : '-',
                                    (fileMDS.permissions & 0200) ? 'w' : '-',
                                    (fileMDS.permissions & 0100) ? 'x' : '-',
                                    (fileMDS.permissions & 0040) ? 'r' : '-',
                                    (fileMDS.permissions & 0020) ? 'w' : '-',
                                    (fileMDS.permissions & 0010) ? 'x' : '-',
                                    (fileMDS.permissions & 0004) ? 'r' : '-',
                                    (fileMDS.permissions & 0002) ? 'w' : '-',
                                    (fileMDS.permissions & 0001) ? 'x' : '-');

                                if(fileMDS.type == 2) {
                                    printf("d%s \t %d \t  (TC)%s  (TA)%s  (TM)%s",
                                        perm_str, data.offset, create, access, modify);
                                }
                                else if(fileMDS.type == 1) {
                                    printf("-%s \t %d \t  (TC)%s  (TA)%s  (TM)%s",
                                        perm_str, data.offset, create, access, modify);
                                }
                                else {
                                    printf("l%s \t %d \t  (TC)%s  (TA)%s  (TM)%s",
                                        perm_str, data.offset, create, access, modify);
                                }
                            }
                        }
                        }
                        if(print) printf("\n");
                    }
                    lseek(fs_file, current_pointer, SEEK_SET);
                }
            }
            must_print = false;
        }
        free(superblock); free(check_path);
    }
    else printf("fs_ls: execute first fs_workwith\n");
}

void fs_modify(int fs_file, list_node **current, char *path, unsigned int permissions) {
    if(fs_file > 0) {
        int target_offset = find_path(fs_file, current, path, false);
        if(target_offset < 0) {
            printf("fs_modify: failed to access %s: no such file or directory\n", path);
            return;
        }

        Superblock *superblock = malloc(sizeof(Superblock));
        MDS mds;

        lseek(fs_file, 0, SEEK_SET);
        read(fs_file, superblock, sizeof(Superblock));

        lseek(fs_file, target_offset, SEEK_SET);
        read(fs_file, &mds, superblock->metadata_size);

        mds.permissions = permissions & 0777;
        mds.modification_time = time(0);

        lseek(fs_file, target_offset, SEEK_SET);
        write(fs_file, &mds, superblock->metadata_size);

        printf("Permissions of %s changed to %o\n", path, mds.permissions);
        free(superblock);
    } else {
        printf("fs_modify: execute first fs_workwith\n");
    }
}

void fs_mv(int fs_file,  list_node **current, char *path_source, char *destination, bool i){
    bool exists = false, option_i = true;
    char all_sources[FILENAME_SIZE];
    strcpy(all_sources, path_source);

    if(fs_file>0){

        char *source = strtok(all_sources," ");
        if ((file_offset = find_path(fs_file, current, source, false))<0){
            printf("fs_mv: failed to access %s: No such file or directory\n", source);
            source = NULL;
        }else{
            data_type data;
            MDS currentMDS, fileMDS;

            lseek(fs_file, 0, SEEK_SET);
            Superblock *superblock = malloc(sizeof(Superblock));
            read(fs_file, superblock, sizeof(Superblock));

            lseek(fs_file, file_offset, SEEK_SET);
            read(fs_file, &fileMDS, superblock->metadata_size);

            lseek(fs_file, fileMDS.parent_offset, SEEK_SET);
            read(fs_file, &currentMDS, superblock->metadata_size);

            for(int i = 0; i < DATABLOCK_NUM; i++){
                lseek(fs_file, currentMDS.data.datablocks[i], SEEK_SET);
                for (int j = 0; j < superblock->datablocks_size/(sizeof(data_type)); j++){
                    read(fs_file, &data, sizeof(data_type));
                    if(data.active == true){
                        if(strcmp(data.filename, fileMDS.filename) == 0){
                            strcpy(data.filename, destination);
                            lseek(fs_file, -sizeof(data_type), SEEK_CUR);
                            write(fs_file, &data, sizeof(data_type));
                            i = DATABLOCK_NUM;
                            break;
                        }
                    }
                }
            }
            strcpy(fileMDS.filename, destination);
            lseek(fs_file, file_offset, SEEK_SET);
            write(fs_file, &fileMDS, superblock->metadata_size);
        }
    }
    else printf("fs_mv: execute first fs_workwith\n");
}

void fs_rm(int fs_file,  list_node **current, char *path, bool r, bool command_call){
    bool deleted = false;
    if(fs_file>0){
        char full_path[FILENAME_SIZE];
        int dest_offset;
        int index=0;
        int size = strlen(path)+1;
        char *check_path = malloc(size); memset(check_path, 0, size); strcpy(check_path, path);
        while(check_path[index]!='\0'){
            int ptr=0; int index_begin=index;
            while((check_path[index]!=' ') && (check_path[index] != '\0')){
                index++;
                ptr++;
            }
            memset(full_path, 0, FILENAME_SIZE);
            strncpy(full_path, check_path+index_begin, ptr);
            if(check_path[index] != '\0') index++;

            dest_offset = find_path(fs_file, current, full_path, false);
            data_type data;
            MDS currentMDS, tempMDS, parentMDS;
            int current_offset,current_offset_rec;

            lseek(fs_file, 0, SEEK_SET);
            Superblock *superblock = malloc(sizeof(Superblock));
            read(fs_file, superblock, sizeof(Superblock));

            lseek(fs_file, dest_offset, SEEK_SET);
            read(fs_file, &currentMDS, superblock->metadata_size);
            if(currentMDS.type == 2){
                for(int i = 0; i < DATABLOCK_NUM; i++){
                    lseek(fs_file, currentMDS.data.datablocks[i], SEEK_SET);
                    for (int j = 0; j < superblock->datablocks_size/(sizeof(data_type)); j++){
                        read(fs_file, &data, sizeof(data_type));
                        current_offset = lseek(fs_file, 0, SEEK_CUR);
                        if(data.active == true){
                            lseek(fs_file, data.offset, SEEK_SET);
                            read(fs_file, &tempMDS, superblock->metadata_size);
                            lseek(fs_file, current_offset, SEEK_SET);
                            if(tempMDS.type != 2){
                                printf("fs_rm: file %s just deleted\n",data.filename);
                                deleted = true;
                                data.active = false;
                                lseek(fs_file, -sizeof(data_type), SEEK_CUR);
                                write(fs_file, &data, sizeof(data_type));
                                current_offset_rec = lseek(fs_file, 0, SEEK_CUR);
                                delete_from_bitmap(data.offset, fs_file);
                                lseek(fs_file, current_offset_rec, SEEK_SET);
                            }else if(r){
                                char *full_filename=malloc(strlen(full_path)+FILENAME_SIZE);
                                memset(full_filename,0, strlen(full_path)+FILENAME_SIZE);
                                sprintf(full_filename, "%s/%s", full_path, data.filename);
                                current_offset_rec = lseek(fs_file, 0, SEEK_CUR);

                                fs_rm(fs_file, current, full_filename, true, false);
                                lseek(fs_file, current_offset_rec, SEEK_SET);
                                free(full_filename);

                                printf("fs_rm: folder %s just deleted\n",data.filename);
                                deleted = true;
                                data.active = false;
                                lseek(fs_file, -sizeof(data_type), SEEK_CUR);
                                write(fs_file, &data, sizeof(data_type));
                                current_offset_rec = lseek(fs_file, 0, SEEK_CUR);
                                delete_from_bitmap(data.offset, fs_file);
                                lseek(fs_file, current_offset_rec, SEEK_SET);
                            }

                        }
                    }
                }
                if(r && command_call){
                    lseek(fs_file, currentMDS.parent_offset, SEEK_SET);
                    read(fs_file, &parentMDS, superblock->metadata_size);
                    for(int i = 0; i < DATABLOCK_NUM; i++){
                        lseek(fs_file, parentMDS.data.datablocks[i], SEEK_SET);
                        for (int j = 0; j < superblock->datablocks_size/(sizeof(data_type)); j++){
                            read(fs_file, &data, sizeof(data_type));
                            if(data.active == true){
                                if(strcmp(data.filename, currentMDS.filename) == 0){
                                    printf("fs_rm: folder %s just deleted\n",data.filename);
                                    deleted = true;
                                    data.active = false;
                                    lseek(fs_file, -sizeof(data_type), SEEK_CUR);
                                    write(fs_file, &data, sizeof(data_type));
                                    current_offset_rec = lseek(fs_file, 0, SEEK_CUR);
                                    delete_from_bitmap(data.offset, fs_file);
                                    lseek(fs_file, current_offset_rec, SEEK_SET);
                                }
                            }
                        }
                    }
                }
            }else{
                printf("fs_rm: %s: not a directory \n",full_path);
            }
            free(superblock);
        }
        free(check_path);
    }
    else printf("fs_rm: execute first fs_workwith\n");
}

void fs_ln(int fs_file,  list_node **current, char *source, char *output){
    if(fs_file>0){
        Superblock *superblock = malloc(sizeof(Superblock));
        int free_offset, source_offset, output_offset;

        char *tmp = strrchr(output, '/'), *filename;
        char *output_path = malloc(strlen(output)+1);
        memset(output_path, 0, strlen(output)+1);
        if(tmp == NULL){
            strcpy(output_path, "./");
            filename = output;
        }
        else{
            strncpy(output_path, output, (int)(tmp-output));
            filename=tmp+1;
        }

        if ((source_offset = find_path(fs_file, current, source, false))<0)
            printf("fs_ln: failed to access %s: No such file or directory\n", source);
        if ((output_offset = find_path(fs_file, current, output_path, false))<0)
            printf("fs_ln: failed to access %s: No such file or directory\n", output_path);

        if(source>=0 && output>=0){
            data_type data;
            MDS mds, currentMDS, sourceMDS;
            lseek(fs_file, 0, SEEK_SET);
            read(fs_file, superblock, sizeof(Superblock));
            bool exists_already = false;
            bool empty_space = false;

            if(find_file(fs_file, filename)>=0) printf("fs_ln: '%s' name already exists\n", filename);
            else{
                lseek(fs_file, source_offset, SEEK_SET);
                read(fs_file, &sourceMDS, superblock->metadata_size);
                if (sourceMDS.type != 1) printf("fs_ln: '%s' hard link not allowed for directory\n", filename);
                else{
                    int free_offset = get_space(fs_file);

                    superblock->latest_nodeid++;
                    mds.nodeid = superblock->latest_nodeid;
                    mds.offset = free_offset;
                    mds.size = 0;
                    mds.type = 3;
                    mds.parent_nodeid = (*current)->nodeid;
                    mds.parent_offset = (*current)->offset;
                    strcpy(mds.filename, filename);
                    mds.creation_time = time(0); mds.access_time = time(0); mds.modification_time = time(0);
                    mds.data.datablocks[0] = mds.offset + superblock->metadata_size;
                    for(int i = 1; i < DATABLOCK_NUM; i++){
                        mds.data.datablocks[i] = mds.data.datablocks[i-1] + superblock->datablocks_size;
                    }
                    lseek(fs_file, mds.offset, SEEK_SET);
                    write(fs_file, &mds, superblock->metadata_size);
                    add_to_bitmap(mds.offset, fs_file);

                    data.nodeid = 0;
                    data.offset = 0;
                    memset(data.filename, 0, FILENAME_SIZE);
                    data.active = false;
                    for(int i = 0; i < DATABLOCK_NUM; i++){
                        lseek(fs_file, mds.data.datablocks[i], SEEK_SET);
                        for (int j = 0; j < superblock->datablocks_size/(sizeof(data_type)); j++) {
                            write(fs_file, &data, sizeof(data_type));
                        }
                    }

                    lseek(fs_file, source_offset, SEEK_SET);
                    read(fs_file, &sourceMDS, superblock->metadata_size);
                    data.nodeid = sourceMDS.nodeid;
                    data.offset = sourceMDS.offset;
                    strcpy(data.filename, sourceMDS.filename);
                    data.active = true;
                    lseek(fs_file, mds.data.datablocks[0], SEEK_SET);
                    write(fs_file, &data, sizeof(data_type));

                    lseek(fs_file, (*current)->offset, SEEK_SET);
                    read(fs_file, &currentMDS, superblock->metadata_size);
                    for(int i = 0; i < DATABLOCK_NUM; i++) {
                        lseek(fs_file, currentMDS.data.datablocks[i], SEEK_SET);
                        for (int j = 0; j < superblock->datablocks_size/(sizeof(data_type)); j++) {
                            read(fs_file, &data, sizeof(data_type));
                            if(data.active == false){
                                data.nodeid = mds.nodeid;
                                data.offset = mds.offset;
                                strcpy(data.filename, mds.filename);
                                data.active = true;
                                lseek(fs_file, -sizeof(data_type), SEEK_CUR);
                                write(fs_file, &data, sizeof(data_type));
                                i = DATABLOCK_NUM;
                                break;
                            }
                        }
                    }
                }
            }
        }
        free(superblock); free(output_path);
    }
    else printf("fs_ln: execute first fs_workwith\n");
}

void fs_import(int fs_file,  list_node **current, char *sources, char *directory){
    if(fs_file>0){
        data_type data;
        MDS allMDS, fileMDS;
        int index=0, size = strlen(sources)+1, file_offset;
        char str_source[FILENAME_SIZE];
        char *check_path = malloc(size); memset(check_path, 0, size); strcpy(check_path, sources);
        while(check_path[index] != '\0'){
            lseek(fs_file, 0, SEEK_SET);
            Superblock *superblock = malloc(sizeof(Superblock));
            read(fs_file, superblock, sizeof(Superblock));

            int ptr=0; int index_begin=index;
            while((check_path[index]!=' ') && (check_path[index] != '\0')){
                index++;
                ptr++;
            }
            memset(str_source, 0, FILENAME_SIZE);
            strncpy(str_source, check_path+index_begin, ptr);
            if(check_path[index] != '\0') index++;

            if(strcmp(str_source, directory)!=0){
                DIR *dir_import; struct dirent *dir;
                int file_import, dir_offset=-1;
                if ((dir_import = opendir(str_source)) != NULL){
                    fs_cd(fs_file, current, directory);

                    if((find_file(fs_file, directory))>0){
                        char *name = strrchr(str_source, '/');
                        if(name == NULL) name = str_source;
                        else name++;
                        if(*name!='\0'){

                            dir_offset = fs_mkdir(fs_file, name, current);
                            if(dir_offset>0){
                                while((dir = readdir(dir_import)) != NULL) {
                                    if(strcmp(dir->d_name, ".")!=0 && strcmp(dir->d_name, "..")!=0) {

                                        char *source_filename=malloc(strlen(str_source)+FILENAME_SIZE);
                                        memset(source_filename,0, strlen(str_source)+FILENAME_SIZE);
                                        sprintf(source_filename, "%s/%s", str_source, dir->d_name);

                                        fs_import(fs_file,  current, source_filename, name);
                                        free(source_filename);
                                    }
                                }
                            }

                        }
                    }
                    fs_cd(fs_file, current, "..");
                    closedir(dir_import);
                }
                else if((file_import = open(str_source, O_RDONLY))>0){
                    int size_file = lseek(file_import, 0, SEEK_END);
                    lseek(file_import, 0, SEEK_SET);
                    if(size_file<=BLOCK_SIZE*DATABLOCK_NUM){
                        if((find_file(fs_file, directory))>0){
                            fs_cd(fs_file, current, directory);
                            char *name = strrchr(str_source, '/');
                            if(name == NULL) name = str_source;
                            else name++;
                            if(*name!='\0'){
                                if((file_offset = fs_touch(fs_file, true, true, name, current))>0){
                                    lseek(fs_file, file_offset, SEEK_SET);
                                    read(fs_file, &fileMDS, superblock->metadata_size);
                                    fileMDS.size = size_file;
                                    lseek(fs_file, file_offset, SEEK_SET);
                                    write(fs_file, &fileMDS, superblock->metadata_size);

                                    int ii=0, current_size = BLOCK_SIZE;
                                    while((ii<DATABLOCK_NUM) && (size_file>0)){
                                        if(size_file>0){
                                            lseek(fs_file, fileMDS.data.datablocks[ii], SEEK_SET);
                                            if(size_file<BLOCK_SIZE) current_size = size_file;
                                            char *buffer=malloc(current_size+1); memset(buffer, 0, current_size+1);
                                            read(file_import, buffer, current_size);
                                            write(fs_file, buffer, current_size);
                                            free(buffer);
                                            if(size_file>BLOCK_SIZE) size_file -=BLOCK_SIZE;
                                            else size_file=0;
                                        }
                                        ii++;
                                    }

                                }
                            }
                            fs_cd(fs_file, current, "..");
                        }
                        else printf("fs_import: %s: No such file or directory\n", directory);
                    }
                    else printf("fs_import: no enough space for file\n");
                    close(file_import);
                }
                else printf("fs_import: failed to access %s: No such file or directory\n", str_source);
            }
            free(superblock);
        }
        free(check_path);
    }
    else printf("fs_import: execute first fs_workwith\n");
}

void fs_export(int fs_file,  list_node **current, char *sources, char *directory){
    if(fs_file>0){
        int source_offset;
        data_type data;
        MDS allMDS, fileMDS;
        int size = strlen(sources)+1;
        char *check_path = malloc(size); memset(check_path, 0, size); strcpy(check_path, sources);
        char str_source[FILENAME_SIZE];
        char *pathname = malloc(strlen(directory)+FILENAME_SIZE+5);
        DIR *dir_export, *open_dir;
        FILE *write_fp; int fd;
        struct dirent *dir;
        int index=0;

        if ((dir_export = opendir(directory)) != NULL){
            while(check_path[index] != '\0'){
                int ptr=0; int index_begin=index;
                while((check_path[index]!=' ') && (check_path[index] != '\0')){
                    index++;
                    ptr++;
                }
                memset(str_source, 0, FILENAME_SIZE);
                strncpy(str_source, check_path+index_begin, ptr);
                if(check_path[index] != '\0') index++;

                if(strcmp(str_source, directory)!=0){
                    if((source_offset = find_path(fs_file, current, str_source, false))<0)
                        printf("fs_export: failed to access %s: no such file or directory\n", str_source);
                    else{
                        lseek(fs_file, 0, SEEK_SET);
                        Superblock *superblock = malloc(sizeof(Superblock));
                        read(fs_file, superblock, sizeof(Superblock));
                        lseek(fs_file, source_offset, SEEK_SET);
                        read(fs_file, &fileMDS, superblock->metadata_size);

                        if(fileMDS.type == 1){
                            memset(pathname, 0, strlen(directory)+FILENAME_SIZE+5);
                            sprintf(pathname, "./%s/%s", directory, fileMDS.filename);
                            fd = open(pathname, O_WRONLY | O_CREAT, 0644);
                            if(fd>0){
                                int max_size = fileMDS.size, ii=0, current_size = BLOCK_SIZE;
                                while((ii<DATABLOCK_NUM) && (max_size>0)){
                                    if(max_size>0){
                                        lseek(fs_file, fileMDS.data.datablocks[ii], SEEK_SET);
                                        if(max_size<BLOCK_SIZE) current_size = max_size;
                                        char *buffer=malloc(current_size+1); memset(buffer, 0, current_size+1);
                                        read(fs_file, buffer, current_size);
                                        write(fd, buffer, current_size);
                                        free(buffer);
                                        if(max_size>BLOCK_SIZE) max_size -=BLOCK_SIZE;
                                        else max_size=0;
                                    }
                                    ii++;
                                }
                                close(fd);
                            }
                            else printf("fs_export: open: failed to access %s: no such file or directory\n", pathname);
                        }
                        else if(fileMDS.type == 2){
                            memset(pathname, 0, strlen(directory)+FILENAME_SIZE+5);
                            sprintf(pathname, "%s/%s", directory, fileMDS.filename);
                            if(mkdir(pathname,S_IRWXG)<0) printf("not create dir:[%s]\n", pathname);

                            for(int i = 0; i < DATABLOCK_NUM; i++){
                                lseek(fs_file, fileMDS.data.datablocks[i], SEEK_SET);
                                for (int j = 0; j < superblock->datablocks_size/(sizeof(data_type)); j++){
                                    read(fs_file, &data, sizeof(data_type));
                                    int current_pointer = lseek(fs_file, 0, SEEK_CUR);
                                    if(data.active == true){
                                        lseek(fs_file, data.offset, SEEK_SET);
                                        read(fs_file, &allMDS, superblock->metadata_size);

                                        char *source_filename=malloc(strlen(str_source)+strlen(allMDS.filename)+5);
                                        memset(source_filename, 0, strlen(str_source)+strlen(allMDS.filename)+5);
                                        sprintf(source_filename, "%s/%s", str_source, allMDS.filename);

                                        fs_export(fs_file, current, source_filename, pathname);

                                        free(source_filename);
                                        lseek(fs_file, current_pointer, SEEK_SET);
                                    }
                                }
                            }

                        }
                        free(superblock);
                    }
                }

            }
            closedir(dir_export);
        }
        else printf("fs_export: opendir: failed to access %s: no such file or directory\n", directory);
        free(pathname); free(check_path);
    }
    else printf("fs_export: execute first fs_workwith\n");
}

void fs_cat(int fs_file,  list_node **current, char *sources, char *output){
    if(fs_file>0){
        data_type data;
        MDS filesMDS, outMDS;
        int index=0, size = strlen(sources)+1, source_offset, output_offset;
        char str_source[FILENAME_SIZE];
        char *check_path = malloc(size); memset(check_path, 0, size); strcpy(check_path, sources);
        lseek(fs_file, 0, SEEK_SET);
        Superblock *superblock = malloc(sizeof(Superblock));
        read(fs_file, superblock, sizeof(Superblock));

        if ((output_offset = find_path(fs_file, current, output, false))<0)
            output_offset = fs_touch(fs_file, true, true, output, current);
        lseek(fs_file, output_offset, SEEK_SET);
        read(fs_file, &outMDS, superblock->metadata_size);

        while(check_path[index] != '\0'){
            int ptr=0; int index_begin=index;
            while((check_path[index]!=' ') && (check_path[index] != '\0')){
                index++;
                ptr++;
            }
            memset(str_source, 0, FILENAME_SIZE);
            strncpy(str_source, check_path+index_begin, ptr);
            if(check_path[index] != '\0') index++;

            if ((source_offset = find_path(fs_file, current, str_source, false))<0)
                printf("fs_cat: failed to access [%s]: No such file or directory\n", str_source);
            else{
                lseek(fs_file, source_offset, SEEK_SET);
                read(fs_file, &filesMDS, superblock->metadata_size);

                int max_size = filesMDS.size, ii=0, current_size = BLOCK_SIZE;
                int current_out = outMDS.data.datablocks[0]+outMDS.size;

                if((max_size+outMDS.size)<(DATABLOCK_NUM*BLOCK_SIZE)){
                    while((ii<DATABLOCK_NUM) && (max_size>0)){
                        if(max_size>0){
                            lseek(fs_file, filesMDS.data.datablocks[ii], SEEK_SET);
                            if(max_size<BLOCK_SIZE) current_size = max_size;
                            char *buffer=malloc(current_size+1); memset(buffer, 0, current_size+1);
                            read(fs_file, buffer, current_size);

                            lseek(fs_file, current_out, SEEK_SET);
                            write(fs_file, buffer, current_size);
                            current_out+=current_size;

                            free(buffer);
                            if(max_size>BLOCK_SIZE) max_size -=BLOCK_SIZE;
                            else max_size=0;
                        }
                        ii++;
                    }
                    outMDS.size+=filesMDS.size;
                    lseek(fs_file, output_offset, current_size);
                    write(fs_file, &outMDS, superblock->metadata_size);

                    max_size = outMDS.size; ii=0; current_size = BLOCK_SIZE;
                    printf("%s\n", outMDS.filename);
                    while((ii<DATABLOCK_NUM) && (max_size>0)){
                        if(max_size>0){
                            lseek(fs_file, outMDS.data.datablocks[ii], SEEK_SET);
                            if(max_size<BLOCK_SIZE) current_size = max_size;
                            char *buffer=malloc(current_size+1); memset(buffer, 0, current_size+1);
                            read(fs_file, buffer, current_size);
                            printf("%s", buffer);
                            free(buffer);
                            if(max_size>BLOCK_SIZE) max_size -=BLOCK_SIZE;
                            else max_size=0;
                        }
                        ii++;
                    }
                    printf("\n");
                }
                else printf("fs_cat: not enough space\n");
            }
        }
        free(check_path); free(superblock);
    }
    else printf("fs_export: execute first fs_workwith\n");
}

void fs_edit(int fs_file, list_node **current, char *filename) {
    if (fs_file <= 0) {
        printf("fs_edit: execute first fs_workwith\n");
        return;
    }

    int file_offset = find_path(fs_file, current, filename, false);
    if (file_offset < 0) {
        printf("File not found, creating new file...\n");
        file_offset = fs_touch(fs_file, true, true, filename, current);
        if (file_offset < 0) {
            printf("Failed to create file\n");
            return;
        }
    }

    Superblock superblock;
    MDS fileMDS;
    
    lseek(fs_file, 0, SEEK_SET);
    if (read(fs_file, &superblock, sizeof(Superblock)) != sizeof(Superblock)) {
        perror("Error reading superblock");
        return;
    }

    lseek(fs_file, file_offset, SEEK_SET);
    if (read(fs_file, &fileMDS, superblock.metadata_size) != superblock.metadata_size) {
        perror("Error reading file metadata");
        return;
    }

    struct termios old_term, new_term;
    tcgetattr(STDIN_FILENO, &old_term);
    new_term = old_term;
    new_term.c_iflag &= ~(IXON);
    new_term.c_lflag &= ~(ICANON | ECHO | ISIG);
    new_term.c_cc[VMIN] = 1;
    new_term.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &new_term) != 0) {
        perror("Error setting terminal mode");
        return;
    }

    char *content = calloc(1, BLOCK_SIZE * DATABLOCK_NUM + 1);
    size_t content_size = fileMDS.size;
    if (content_size > 0) {
        size_t remaining = content_size;
        size_t pos = 0;
        for (int i = 0; i < DATABLOCK_NUM && remaining > 0; i++) {
            size_t to_read = (remaining > BLOCK_SIZE) ? BLOCK_SIZE : remaining;
            lseek(fs_file, fileMDS.data.datablocks[i], SEEK_SET);
            if (read(fs_file, content + pos, to_read) != to_read) {
                printf("Error reading block %d\n", i);
                free(content);
                tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
                return;
            }
            pos += to_read;
            remaining -= to_read;
        }
    }

    printf("\n=== Editing %s ===\n", filename);
    printf("CTRL+S: Save | CTRL+X: Cancel | Max: %d bytes\n", BLOCK_SIZE * DATABLOCK_NUM);
    printf("----------------------------------------\n");
    if (content_size > 0) {
        printf("%.*s", (int)content_size, content);
    }

    int c;
    while (1) {
        if (read(STDIN_FILENO, &c, 1) != 1) continue;

        if (c == 19) {
            data_type empty = {0};
            for (int i = 0; i < DATABLOCK_NUM; i++) {
                lseek(fs_file, fileMDS.data.datablocks[i], SEEK_SET);
                for (int j = 0; j < superblock.datablocks_size / sizeof(data_type); j++) {
                    if (write(fs_file, &empty, sizeof(data_type)) != sizeof(data_type)) {
                        printf("Error clearing block %d\n", i);
                        break;
                    }
                }
            }

            size_t remaining = content_size;
            size_t pos = 0;
            for (int i = 0; i < DATABLOCK_NUM && remaining > 0; i++) {
                size_t to_write = (remaining > BLOCK_SIZE) ? BLOCK_SIZE : remaining;
                lseek(fs_file, fileMDS.data.datablocks[i], SEEK_SET);
                if (write(fs_file, content + pos, to_write) != to_write) {
                    printf("Error writing block %d\n", i);
                    break;
                }
                pos += to_write;
                remaining -= to_write;
            }

            fileMDS.size = content_size;
            fileMDS.modification_time = time(NULL);
            lseek(fs_file, file_offset, SEEK_SET);
            if (write(fs_file, &fileMDS, superblock.metadata_size) != superblock.metadata_size) {
                printf("Error updating metadata\n");
            } else {
                printf("\nFile saved successfully (%zu bytes)\n", content_size);
            }
            break;
        }
        else if (c == 24) {
            printf("\nEdit canceled\n");
            break;
        }
        else if (c == 127 || c == 8) {
            if (content_size > 0) {
                content_size--;
                printf("\b \b");
                fflush(stdout);
            }
        }
        else if (isprint(c)) {
            if (content_size < BLOCK_SIZE * DATABLOCK_NUM) {
                content[content_size++] = c;
                putchar(c);
                fflush(stdout);
            } else {
                printf("\nMaximum file size reached!\n");
            }
        }
    }

    free(content);
    tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
}

void fs_read_file(int fs_file, char *filename, list_node **current) {
    if (fs_file <= 0) {
        printf("fs_read_file: execute first fs_workwith\n");
        return;
    }

    int file_offset = find_path(fs_file, current, filename, false);
    if (file_offset < 0) {
        printf("fs_read_file: File [%s] not found.\n", filename);
        return;
    }

    lseek(fs_file, file_offset, SEEK_SET);
    MDS fileMDS;
    read(fs_file, &fileMDS, sizeof(MDS));

    printf("Reading file: %s (Size: %d bytes)\n", fileMDS.filename, fileMDS.size);

    int remaining_size = fileMDS.size;
    int block_index = 0;
    char buffer[BLOCK_SIZE + 1];

    while (remaining_size > 0 && block_index < DATABLOCK_NUM) {
        int to_read = (remaining_size > BLOCK_SIZE) ? BLOCK_SIZE : remaining_size;
        lseek(fs_file, fileMDS.data.datablocks[block_index], SEEK_SET);
        read(fs_file, buffer, to_read);
        buffer[to_read] = '\0';
        printf("%s", buffer);

        remaining_size -= to_read;
        block_index++;
    }

    printf("\n");
}
