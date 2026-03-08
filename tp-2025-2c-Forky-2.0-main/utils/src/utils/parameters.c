#include "parameters.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int get_params(int argc) {
    return (argc > 0) ? argc - 1 : 0;
}

bool valid_min_params(int argc, int min_required) {
    int params = get_params(argc);
    return params >= min_required;
}

bool valid_max_params(int argc, int max_required) {
    int params = get_params(argc);
    return params <= max_required;
}

bool valid_range_params(int argc, int min_required, int max_required) {
    return valid_min_params(argc, min_required) &&
           valid_max_params(argc, max_required);
}

bool valid_even_params(int argc) {
    return (get_params(argc) % 2) == 0;
}

bool valid_odd_params(int argc) {
    return (get_params(argc) % 2) != 0;
}

void slice_params(char *argv[], int start, int count, char *slice[]) {
    for (int i = 0; i < count; ++i)
        slice[i] = argv[start + i];
}

int find_flag(char *argv[], int argc, const char *flag) {
    for (int i = 1; i < argc; ++i)
        if (argv[i] && strcmp(argv[i], flag) == 0)
            return i;
    return -1;
}

bool has_flag(char *argv[], int argc, const char *flag) {
    return find_flag(argv, argc, flag) != -1;
}

char* read_query_file_content(const char* path) {
    FILE* file = fopen(path, "r");
    if (!file) return NULL;

    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    rewind(file);

    char* buffer = malloc(length + 1);
    if (!buffer) {
        fclose(file);
        return NULL;
    }

    fread(buffer, 1, length, file);
    buffer[length] = '\0';

    fclose(file);
    return buffer;
}
