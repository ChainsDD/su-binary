/*
** Copyright 2012, The CyanogenMod Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#include <unistd.h>
#include <limits.h>
#include <fcntl.h>
#include <errno.h>
#include <endian.h>
#include <ctype.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cutils/properties.h>

/* reads a file, making sure it is terminated with \n \0 */
char* read_file(const char *fn, unsigned *_sz)
{
    char *data;
    int sz;
    int fd;

    data = 0;
    fd = open(fn, O_RDONLY);
    if(fd < 0) return 0;

    sz = lseek(fd, 0, SEEK_END);
    if(sz < 0) goto oops;

    if(lseek(fd, 0, SEEK_SET) != 0) goto oops;

    data = (char*) malloc(sz + 2);
    if(data == 0) goto oops;

    if(read(fd, data, sz) != sz) goto oops;
    close(fd);
    data[sz] = '\n';
    data[sz+1] = 0;
    if(_sz) *_sz = sz;
    return data;

oops:
    close(fd);
    if(data != 0) free(data);
    return 0;
}

int get_property(const char *data, char *found, const char *searchkey, const char *not_found)
{
    char *key, *value, *eol, *sol, *tmp;
    if (data == NULL) goto defval;
    int matched = 0;
    sol = strdup(data);
    while((eol = strchr(sol, '\n'))) {
        key = sol;
        *eol++ = 0;
        sol = eol;

        value = strchr(key, '=');
        if(value == 0) continue;
        *value++ = 0;

        while(isspace(*key)) key++;
        if(*key == '#') continue;
        tmp = value - 2;
        while((tmp > key) && isspace(*tmp)) *tmp-- = 0;

        while(isspace(*value)) value++;
        tmp = eol - 2;
        while((tmp > value) && isspace(*tmp)) *tmp-- = 0;

        if (strncmp(searchkey, key, strlen(searchkey)) == 0) {
            matched = 1;
            break;
        }
    }
    int len;
    if (matched) {
        len = strlen(value);
        if (len >= PROPERTY_VALUE_MAX)
            return -1;
        memcpy(found, value, len + 1);
    } else goto defval;
    return len;

defval:
    len = strlen(not_found);
    memcpy(found, not_found, len + 1);
    return len;
}