/*
 * Main program for Unix psocks.
 */

#include <string.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>

#include "putty.h"
#include "ssh.h"
#include "psocks.h"

typedef struct PsocksDataSinkPopen {
    stdio_sink sink[2];
    PsocksDataSink pds;
} PsocksDataSinkPopen;

static void popen_free(PsocksDataSink *pds)
{
    PsocksDataSinkPopen *pdsp = container_of(pds, PsocksDataSinkPopen, pds);
    for (size_t i = 0; i < 2; i++)
        pclose(pdsp->sink[i].fp);
    sfree(pdsp);
}

static PsocksDataSink *open_pipes(
    const char *cmd, const char *const *direction_args,
    const char *index_arg, char **err)
{
    FILE *fp[2];
    char *errmsg = NULL;

    for (size_t i = 0; i < 2; i++) {
        /* No escaping needed: the provided command is already
         * shell-quoted, and our extra arguments are simple */
        char *command = dupprintf("%s %s %s", cmd,
                                  direction_args[i], index_arg);

        fp[i] = popen(command, "w");
        sfree(command);

        if (!fp[i]) {
            if (!errmsg)
                errmsg = dupprintf("%s", strerror(errno));
        }
    }

    if (errmsg) {
        for (size_t i = 0; i < 2; i++)
            if (fp[i])
                pclose(fp[i]);
        *err = errmsg;
        return NULL;
    }

    PsocksDataSinkPopen *pdsp = snew(PsocksDataSinkPopen);

    for (size_t i = 0; i < 2; i++) {
        setvbuf(fp[i], NULL, _IONBF, 0);
        stdio_sink_init(&pdsp->sink[i], fp[i]);
        pdsp->pds.s[i] = BinarySink_UPCAST(&pdsp->sink[i]);
    }

    pdsp->pds.free = popen_free;

    return &pdsp->pds;
}

static int signalpipe[2] = { -1, -1 };
static void sigchld(int signum)
{
    if (write(signalpipe[1], "x", 1) <= 0)
        /* not much we can do about it */;
}

static pid_t subcommand_pid = -1;

static bool still_running = true;

static char **exec_args = NULL;

static void found_subcommand(CmdlineArg *arg)
{
    exec_args = cmdline_arg_remainder(arg);
}

static void start_subcommand(void)
{
    pid_t pid;

    /*
     * Set up the pipe we'll use to tell us about SIGCHLD.
     */
    if (pipe(signalpipe) < 0) {
        perror("pipe");
        exit(1);
    }
    putty_signal(SIGCHLD, sigchld);

    pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(1);
    } else if (pid == 0) {
        execvp(exec_args[0], exec_args);
        perror("exec");
        _exit(127);
    } else {
        subcommand_pid = pid;
    }
}

static const PsocksPlatform platform = {
    open_pipes,
    found_subcommand,
    start_subcommand,
};

static bool psocks_pw_setup(void *ctx, pollwrapper *pw)
{
    if (signalpipe[0] >= 0)
        pollwrap_add_fd_rwx(pw, signalpipe[0], SELECT_R);
    return true;
}

static void psocks_pw_check(void *ctx, pollwrapper *pw)
{
    if (signalpipe[0] >= 0 &&
        pollwrap_check_fd_rwx(pw, signalpipe[0], SELECT_R)) {
        while (true) {
            int status;
            pid_t pid = waitpid(-1, &status, WNOHANG);
            if (pid <= 0)
                break;
            if (pid == subcommand_pid)
                still_running = false;
        }
    }
}

static bool psocks_continue(void *ctx, bool found_any_fd,
                            bool ran_any_callback)
{
    return still_running;
}

int main(int argc, char **argv)
{
    psocks_state *ps = psocks_new(&platform);
    CmdlineArgList *arglist = cmdline_arg_list_from_argv(argc, argv);
    psocks_cmdline(ps, arglist);

    sk_init();
    uxsel_init();
    psocks_start(ps);
    cmdline_arg_list_free(arglist);

    cli_main_loop(psocks_pw_setup, psocks_pw_check, psocks_continue, NULL);
}
