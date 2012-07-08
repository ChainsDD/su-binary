/*
** Copyright 2010, Adam Shanks (@ChainsDD)
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

#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <cutils/log.h>

#include "su.h"

int database_check(const struct su_context *ctx)
{
    FILE *fp;
    char allow = '-';
    char *filename = malloc(snprintf(NULL, 0, "%s/%u-%u", REQUESTOR_STORED_PATH, ctx->from.uid, ctx->to.uid) + 1);
    sprintf(filename, "%s/%u-%u", REQUESTOR_STORED_PATH, ctx->from.uid, ctx->to.uid);
    if ((fp = fopen(filename, "r"))) {
    LOGD("Found file");
        char cmd[PATH_MAX];
        fgets(cmd, sizeof(cmd), fp);
        int last = strlen(cmd) - 1;
        LOGD("this is the last character %u of the string", cmd[5]);
        if (cmd[last] == '\n') {
            cmd[last] = '\0';
        }
        LOGD("Comparing %c %s, %u to %s", cmd[last - 2], cmd, last, get_command(&ctx->to));
        if (strcmp(cmd, get_command(&ctx->to)) == 0) {
            allow = fgetc(fp);
        }
        fclose(fp);
    } else if ((fp = fopen(REQUESTOR_STORED_DEFAULT, "r"))) {
    LOGD("Using default");
        allow = fgetc(fp);
        fclose(fp);
    }
    free(filename);

    if (allow == '1') {
        return DB_ALLOW;
    } else if (allow == '0') {
        return DB_DENY;
    } else {
        return DB_INTERACTIVE;
    }
}
