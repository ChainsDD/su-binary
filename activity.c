/*
** Copyright 2010, Adam Shanks (@ChainsDD)
** Copyright 2008, Zinx Verituse (@zinxv)
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

#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <paths.h>
#include <sys/wait.h>

#include "su.h"

int send_intent(const struct su_context *ctx,
                const char *socket_path, int allow, const char *action)
{
    int rc;

    pid_t pid = fork();
    /* Child */
    if (!pid) {
        char command[ARG_MAX];

        snprintf(command, sizeof(command),
            "exec /system/bin/am broadcast -a %s --es socket '%s' "
            "--ei caller_uid %d --ei allow %d "
            "--ei version_code %d",
            action, socket_path, ctx->from.uid, allow, VERSION_CODE);
        char *args[] = { "sh", "-c", command, NULL, };

        /*
         * before sending the intent, make sure the effective uid/gid match
         * the real uid/gid, otherwise LD_LIBRARY_PATH is wiped
         * in Android 4.0+.
         */
        set_identity(ctx->from.uid);
        int zero = open("/dev/zero", O_RDONLY | O_CLOEXEC);
        dup2(zero, 0);
        int null = open("/dev/null", O_WRONLY | O_CLOEXEC);
        dup2(null, 1);
        dup2(null, 2);
        LOGD("Executing %s\n", command);
        execv(_PATH_BSHELL, args);
        PLOGE("exec am");
        _exit(EXIT_FAILURE);
    }
    /* Parent */
    if (pid < 0) {
        PLOGE("fork");
        return -1;
    }
    pid = waitpid(pid, &rc, 0);
    if (pid < 0) {
        PLOGE("waitpid");
        return -1;
    }
    if (!WIFEXITED(rc) || WEXITSTATUS(rc)) {
        LOGE("am returned error %d\n", rc);
        return -1;
    }
    return 0;
}
