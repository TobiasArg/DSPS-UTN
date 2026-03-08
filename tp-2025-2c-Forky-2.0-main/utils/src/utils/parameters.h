#ifndef ARGS_UTILS_H
#define ARGS_UTILS_H

#include <stdbool.h>

int get_params(int argc);

bool valid_min_params(int argc, int min_required);
bool valid_max_params(int argc, int max_required);
bool valid_range_params(int argc, int min_required, int max_required);
bool valid_even_params(int argc);
bool valid_odd_params(int argc);

void slice_params(char *argv[], int start, int count, char *slice[]);
int find_flag(char *argv[], int argc, const char *flag);
bool has_flag(char *argv[], int argc, const char *flag);

char* read_query_file_content(const char* path);

#endif /* ARGS_UTILS_H */
