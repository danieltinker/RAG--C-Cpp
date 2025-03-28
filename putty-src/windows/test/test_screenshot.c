#include "putty.h"

static NORETURN PRINTF_LIKE(1, 2) void fatal_error(const char *p, ...)
{
    va_list ap;
    fprintf(stderr, "screenshot: ");
    va_start(ap, p);
    vfprintf(stderr, p, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

void out_of_memory(void) { fatal_error("out of memory"); }

int main(int argc, char **argv)
{
    Filename *outfile = NULL;

    AuxMatchOpt amo = aux_match_opt_init(fatal_error);
    while (!aux_match_done(&amo)) {
        CmdlineArg *val;
        #define match_opt(...) aux_match_opt( \
            &amo, NULL, __VA_ARGS__, (const char *)NULL)
        #define match_optval(...) aux_match_opt( \
            &amo, &val, __VA_ARGS__, (const char *)NULL)

        if (aux_match_arg(&amo, &val)) {
            fatal_error("unexpected argument '%s'", cmdline_arg_to_str(val));
        } else if (match_optval("-o", "--output")) {
            outfile = cmdline_arg_to_filename(val);
        } else {
            fatal_error("unrecognised option '%s'\n",
                        cmdline_arg_to_str(amo.arglist->args[amo.index]));
        }
    }

    if (!outfile)
        fatal_error("expected an output file name");

    char *err = save_screenshot(NULL, outfile);
    if (err)
        fatal_error("%s", err);
    filename_free(outfile);

    return 0;
}
