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
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>
#include <limits.h>
#include <fcntl.h>
#include <errno.h>
#include <endian.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <stdint.h>
#include <pwd.h>

#include <private/android_filesystem_config.h>
#include <cutils/properties.h>
#include <cutils/log.h>

#include "su.h"

/* Still lazt, will fix this */
static char socket_path[PATH_MAX];


static inline int get_sdk_version(void)
{
    char sdk_version_prop[PROPERTY_VALUE_MAX];

    property_get("ro.build.version.sdk", sdk_version_prop, "0");
    return atoi(sdk_version_prop); 
}

static int from_init(struct su_initiator *from)
{
    char path[PATH_MAX], exe[PATH_MAX];
    char args[4096], *argv0, *argv_rest;
    int fd;
    ssize_t len;
    int i;
    int err;

    from->uid = getuid();
    from->pid = getppid();

    /* Get the command line */
    snprintf(path, sizeof(path), "/proc/%u/cmdline", from->pid);
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        PLOGE("Opening command line");
        return -1;
    }
    len = read(fd, args, sizeof(args));
    err = errno;
    close(fd);
    if (len < 0 || len == sizeof(args)) {
        PLOGEV("Reading command line", err);
        return -1;
    }

    argv0 = args;
    argv_rest = NULL;
    for (i = 0; i < len; i++) {
        if (args[i] == '\0') {
            if (!argv_rest) {
                argv_rest = &args[i+1];
            } else {
                args[i] = ' ';
            }
        }
    }
    args[len] = '\0';

    if (argv_rest) {
        strncpy(from->args, argv_rest, sizeof(from->args));
        from->args[sizeof(from->args)-1] = '\0';
    } else {
        from->args[0] = '\0';
    }

    /* If this isn't app_process, use the real path instead of argv[0] */
    snprintf(path, sizeof(path), "/proc/%u/exe", from->pid);
    len = readlink(path, exe, sizeof(exe));
    if (len < 0) {
        PLOGE("Getting exe path");
        return -1;
    }
    exe[len] = '\0';
    if (strcmp(exe, "/system/bin/app_process")) {
        argv0 = exe;
    }

    strncpy(from->bin, argv0, sizeof(from->bin));
    from->bin[sizeof(from->bin)-1] = '\0';

    return 0;
}

static void socket_cleanup(void)
{
    unlink(socket_path);
}

static void cleanup(void)
{
    socket_cleanup();
}

static void cleanup_signal(int sig)
{
    socket_cleanup();
    exit(128 + sig);
}

static int socket_create_temp(char *path, size_t len)
{
    int fd;
    struct sockaddr_un sun;

    fd = socket(AF_LOCAL, SOCK_STREAM, 0);
    if (fd < 0) {
        PLOGE("socket");
        return -1;
    }

    memset(&sun, 0, sizeof(sun));
    sun.sun_family = AF_LOCAL;
    snprintf(path, len, "%s/.socket%d", REQUESTOR_CACHE_PATH, getpid());
    snprintf(sun.sun_path, sizeof(sun.sun_path), "%s", path);

    /*
     * Delete the socket to protect from situations when
     * something bad occured previously and the kernel reused pid from that process.
     * Small probability, isn't it.
     */
    unlink(sun.sun_path);

    if (bind(fd, (struct sockaddr*)&sun, sizeof(sun)) < 0) {
        PLOGE("bind");
        goto err;
    }

    if (listen(fd, 1) < 0) {
        PLOGE("listen");
        goto err;
    }

    return fd;
err:
    close(fd);
    return -1;
}

static int socket_accept(int serv_fd)
{
    struct timeval tv;
    fd_set fds;
    int fd;

    /* Wait 20 seconds for a connection, then give up. */
    tv.tv_sec = 20;
    tv.tv_usec = 0;
    FD_ZERO(&fds);
    FD_SET(serv_fd, &fds);
    if (select(serv_fd + 1, &fds, NULL, NULL, &tv) < 1) {
        PLOGE("select");
        return -1;
    }

    fd = accept(serv_fd, NULL, NULL);
    if (fd < 0) {
        PLOGE("accept");
        return -1;
    }

    return fd;
}

static int socket_send_request(int fd, struct su_context *ctx)
{
    size_t len;
    size_t bin_size, cmd_size;
    char *cmd;

#define write_token(fd, data)				\
do {							\
	uint32_t __data = htonl(data);			\
	size_t __count = sizeof(__data);		\
	size_t __len = write((fd), &__data, __count);	\
	if (__len != __count) {				\
		PLOGE("write(" #data ")");		\
		return -1;				\
	}						\
} while (0)

    write_token(fd, PROTO_VERSION);
    write_token(fd, PATH_MAX);
    write_token(fd, ARG_MAX);
    write_token(fd, ctx->from.uid);
    write_token(fd, ctx->to.uid);
    bin_size = strlen(ctx->from.bin) + 1;
    write_token(fd, bin_size);
    len = write(fd, ctx->from.bin, bin_size);
    if (len != bin_size) {
        PLOGE("write(bin)");
        return -1;
    }
    cmd = get_command(&ctx->to);
    cmd_size = strlen(cmd) + 1;
    write_token(fd, cmd_size);
    len = write(fd, cmd, cmd_size);
    if (len != cmd_size) {
        PLOGE("write(cmd)");
        return -1;
    }
    return 0;
}

static int socket_receive_result(int fd, char *result, ssize_t result_len)
{
    ssize_t len;
    
    len = read(fd, result, result_len-1);
    if (len < 0) {
        PLOGE("read(result)");
        return -1;
    }
    result[len] = '\0';

    return 0;
}

static void usage(int status)
{
    FILE *stream = (status == EXIT_SUCCESS) ? stdout : stderr;

    fprintf(stream,
    "Usage: su [options] [--] [-] [LOGIN] [--] [args...]\n\n"
    "Options:\n"
    "  -c, --command COMMAND         pass COMMAND to the invoked shell\n"
    "  -h, --help                    display this help message and exit\n"
    "  -, -l, --login, -m, -p,\n"
    "  --preserve-environment        do nothing, kept for compatibility\n"
    "  -s, --shell SHELL             use SHELL instead of the default " DEFAULT_SHELL "\n"
    "  -v, --version                 display version number and exit\n"
    "  -V                            display version code and exit,\n"
    "                                this is used almost exclusively by Superuser.apk\n");
    exit(status);
}

static void deny(struct su_context *ctx)
{
    char *cmd = get_command(&ctx->to);

    send_intent(ctx, "", 0, ACTION_RESULT);
    LOGW("request rejected (%u->%u %s)", ctx->from.uid, ctx->to.uid, cmd);
    fprintf(stderr, "%s\n", strerror(EACCES));
    exit(EXIT_FAILURE);
}

static void allow(struct su_context *ctx)
{
    char *arg0;
    int argc, err;

    umask(ctx->umask);
    send_intent(ctx, "", 1, ACTION_RESULT);

    arg0 = strrchr (ctx->to.shell, '/');
    arg0 = (arg0) ? arg0 + 1 : ctx->to.shell;
    if (ctx->to.login) {
        int s = strlen(arg0) + 2;
        char *p = malloc(s);

        if (!p)
            exit(EXIT_FAILURE);

        *p = '-';
        strcpy(p + 1, arg0);
        arg0 = p;
    }
    if (setresgid(ctx->to.uid, ctx->to.uid, ctx->to.uid)) {
        PLOGE("setresgid (%u)", ctx->to.uid);
        exit(EXIT_FAILURE);
    }
    if (setresuid(ctx->to.uid, ctx->to.uid, ctx->to.uid)) {
        PLOGE("setresuid (%u)", ctx->to.uid);
        exit(EXIT_FAILURE);
    }

#define PARG(arg)									\
    (ctx->to.optind + (arg) < ctx->to.argc) ? " " : "",					\
    (ctx->to.optind + (arg) < ctx->to.argc) ? ctx->to.argv[ctx->to.optind + (arg)] : ""

    LOGD("%u %s executing %u %s using shell %s : %s%s%s%s%s%s%s%s%s%s%s%s%s%s",
            ctx->from.uid, ctx->from.bin,
            ctx->to.uid, get_command(&ctx->to), ctx->to.shell,
            arg0, PARG(0), PARG(1), PARG(2), PARG(3), PARG(4), PARG(5),
            (ctx->to.optind + 6 < ctx->to.argc) ? " ..." : "");

    argc = ctx->to.optind;
    if (ctx->to.command) {
        ctx->to.argv[--argc] = ctx->to.command;
        ctx->to.argv[--argc] = "-c";
    }
    ctx->to.argv[--argc] = arg0;
    execv(ctx->to.shell, ctx->to.argv + argc);
    err = errno;
    PLOGE("exec");
    fprintf(stderr, "Cannot execute %s: %s\n", ctx->to.shell, strerror(err));
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
    struct su_context ctx = {
        .from = {
            .pid = -1,
            .uid = 0,
            .bin = "",
            .args = "",
        },
        .to = {
            .uid = AID_ROOT,
            .login = 0,
            .shell = DEFAULT_SHELL,
            .command = NULL,
            .argv = argv,
            .argc = argc,
            .optind = 0,
        },
    };
    struct stat st;
    int socket_serv_fd, fd;
    char buf[64], *result;
    int c, dballow;
    struct option long_opts[] = {
        { "command",			required_argument,	NULL, 'c' },
        { "help",			no_argument,		NULL, 'h' },
        { "login",			no_argument,		NULL, 'l' },
        { "preserve-environment",	no_argument,		NULL, 'p' },
        { "shell",			required_argument,	NULL, 's' },
        { "version",			no_argument,		NULL, 'v' },
        { NULL, 0, NULL, 0 },
    };

    while ((c = getopt_long(argc, argv, "+c:hlmps:Vv", long_opts, NULL)) != -1) {
        switch(c) {
        case 'c':
            ctx.to.command = optarg;
            break;
        case 'h':
            usage(EXIT_SUCCESS);
            break;
        case 'l':
            ctx.to.login = 1;
            break;
        case 'm':    /* for compatibility */
        case 'p':
            break;
        case 's':
            ctx.to.shell = optarg;
            break;
        case 'V':
            printf("%d\n", VERSION_CODE);
            exit(EXIT_SUCCESS);
        case 'v':
            printf("%s\n", VERSION);
            exit(EXIT_SUCCESS);
        default:
            /* Bionic getopt_long doesn't terminate its error output by newline */
            fprintf(stderr, "\n");
            usage(2);
        }
    }
    if (optind < argc && !strcmp(argv[optind], "-")) {
        ctx.to.login = 1;
        optind++;
    }
    /* username or uid */
    if (optind < argc && strcmp(argv[optind], "--")) {
        struct passwd *pw;
        pw = getpwnam(argv[optind]);
        if (!pw) {
            char *endptr;

            /* It seems we shouldn't do this at all */
            errno = 0;
            ctx.to.uid = strtoul(argv[optind], &endptr, 10);
            if (errno || *endptr) {
                LOGE("Unknown id: %s\n", argv[optind]);
                fprintf(stderr, "Unknown id: %s\n", argv[optind]);
                exit(EXIT_FAILURE);
            }
        } else {
            ctx.to.uid = pw->pw_uid;
        }
        optind++;
    }
    if (optind < argc && !strcmp(argv[optind], "--")) {
        optind++;
    }
    ctx.to.optind = optind;

    ctx.sdk_version = get_sdk_version();

    if (from_init(&ctx.from) < 0) {
        deny(&ctx);
    }

    ctx.umask = umask(027);

    if (ctx.from.uid == AID_ROOT || ctx.from.uid == AID_SHELL)
        allow(&ctx);

    if (stat(REQUESTOR_DATA_PATH, &st) < 0) {
        PLOGE("stat");
        deny(&ctx);
    }

    if (st.st_gid != st.st_uid)
    {
        LOGE("Bad uid/gid %d/%d for Superuser Requestor application",
                (int)st.st_uid, (int)st.st_gid);
        deny(&ctx);
    }

    mkdir(REQUESTOR_CACHE_PATH, 0770);
    if (chown(REQUESTOR_CACHE_PATH, st.st_uid, st.st_gid)) {
        PLOGE("chown (%s, %ld, %ld)", REQUESTOR_CACHE_PATH, st.st_uid, st.st_gid);
        deny(&ctx);
    }

    if (setgroups(0, NULL)) {
        PLOGE("setgroups");
        deny(&ctx);
    }
    if (setegid(st.st_gid)) {
        PLOGE("setegid (%lu)", st.st_gid);
        deny(&ctx);
    }
    if (seteuid(st.st_uid)) {
        PLOGE("seteuid (%lu)", st.st_uid);
        deny(&ctx);
    }

    dballow = database_check(&ctx);
    switch (dballow) {
        case DB_DENY: deny(&ctx);
        case DB_ALLOW: allow(&ctx);
        case DB_INTERACTIVE: break;
        default: deny(&ctx);
    }
    
    socket_serv_fd = socket_create_temp(socket_path, sizeof(socket_path));
    if (socket_serv_fd < 0) {
        deny(&ctx);
    }

    signal(SIGHUP, cleanup_signal);
    signal(SIGPIPE, cleanup_signal);
    signal(SIGTERM, cleanup_signal);
    signal(SIGQUIT, cleanup_signal);
    signal(SIGINT, cleanup_signal);
    signal(SIGABRT, cleanup_signal);
    atexit(cleanup);

    if (send_intent(&ctx, socket_path, -1, ACTION_REQUEST) < 0) {
        deny(&ctx);
    }

    fd = socket_accept(socket_serv_fd);
    if (fd < 0) {
        deny(&ctx);
    }
    if (socket_send_request(fd, &ctx)) {
        deny(&ctx);
    }
    if (socket_receive_result(fd, buf, sizeof(buf))) {
        deny(&ctx);
    }

    close(fd);
    close(socket_serv_fd);
    socket_cleanup();

    result = buf;

#define SOCKET_RESPONSE	"socket:"
    if (strncmp(result, SOCKET_RESPONSE, sizeof(SOCKET_RESPONSE) - 1))
        LOGW("SECURITY RISK: Requestor still receives credentials in intent");
    else
        result += sizeof(SOCKET_RESPONSE) - 1;

    if (!strcmp(result, "DENY")) {
        deny(&ctx);
    } else if (!strcmp(result, "ALLOW")) {
        allow(&ctx);
    } else {
        LOGE("unknown response from Superuser Requestor: %s", result);
        deny(&ctx);
    }

    deny(&ctx);
    return -1;
}
