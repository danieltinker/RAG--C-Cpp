/*
 * psftp.h: interface between psftp.c / pscp.c, psftpcommon.c, and
 * each platform-specific SFTP module.
 */

#ifndef PUTTY_PSFTP_H
#define PUTTY_PSFTP_H

/*
 * psftp_getcwd returns the local current directory. The returned
 * string must be freed by the caller.
 */
char *psftp_getcwd(void);

/*
 * psftp_lcd changes the local current directory. The return value
 * is NULL on success, or else an error message which must be freed
 * by the caller.
 */
char *psftp_lcd(char *newdir);

/*
 * Retrieve file times on a local file. Must return two unsigned
 * longs in POSIX time_t format.
 */
void get_file_times(char *filename, unsigned long *mtime,
                    unsigned long *atime);

/*
 * One iteration of the PSFTP event loop: wait for network data and
 * process it, once.
 */
int ssh_sftp_loop_iteration(void);

/*
 * Read a command line for PSFTP from standard input. Caller must
 * free.
 *
 * If `backend_required' is true, should also listen for activity
 * at the backend (rekeys, clientalives, unexpected closures etc)
 * and respond as necessary, and if the backend closes it should
 * treat this as a failure condition. If `backend_required' is
 * false, a back end is not (intentionally) active at all (e.g.
 * psftp before an `open' command).
 */
char *ssh_sftp_get_cmdline(const char *prompt, bool backend_required);

/*
 * Platform-specific function called when we're about to make a
 * network connection.
 */
void platform_psftp_pre_conn_setup(LogPolicy *lp);

/*
 * The main program in psftp.c. Called from main() in the platform-
 * specific code, after doing any platform-specific initialisation.
 */
int psftp_main(CmdlineArgList *arglist);

/*
 * These functions are used by PSCP to transmit progress updates
 * and error information to a GUI window managing it. This will
 * probably only ever be supported on Windows, so these functions
 * can safely be stubs on all other platforms.
 */
void gui_update_stats(const char *name, unsigned long size,
                      int percentage, unsigned long elapsed,
                      unsigned long done, unsigned long eta,
                      unsigned long ratebs);
void gui_send_errcount(int list, int errs);
void gui_send_char(int is_stderr, int c);
void gui_enable(const char *arg);

/*
 * It's likely that a given platform's implementation of file
 * transfer utilities is going to want to do things with them that
 * aren't present in stdio. Hence we supply an alternative
 * abstraction for file access functions.
 *
 * This abstraction tells you the size and access times when you
 * open an existing file (platforms may choose the meaning of the
 * file times if it's not clear; whatever they choose will be what
 * PSCP sends to the server as mtime and atime), and lets you set
 * the times when saving a new file.
 *
 * On the other hand, the abstraction is pretty simple: it supports
 * only opening a file and reading it, or creating a file and writing
 * it. None of this read-and-write, seeking-back-and-forth stuff.
 */
typedef struct RFile RFile;
typedef struct WFile WFile;
/* Output params size, perms, mtime and atime can all be NULL if
 * desired. perms will be -1 if the OS does not support POSIX permissions. */
RFile *open_existing_file(const char *name, uint64_t *size,
                          unsigned long *mtime, unsigned long *atime,
                          long *perms);
WFile *open_existing_wfile(const char *name, uint64_t *size);
/* Returns <0 on error, 0 on eof, or number of bytes read, as usual */
int read_from_file(RFile *f, void *buffer, int length);
/* Closes and frees the RFile */
void close_rfile(RFile *f);
WFile *open_new_file(const char *name, long perms);
/* Returns <0 on error, 0 on eof, or number of bytes written, as usual */
int write_to_file(WFile *f, void *buffer, int length);
void set_file_times(WFile *f, unsigned long mtime, unsigned long atime);
/* Closes and frees the WFile */
void close_wfile(WFile *f);
/* Seek offset bytes through file */
enum { FROM_START, FROM_CURRENT, FROM_END };
int seek_file(WFile *f, uint64_t offset, int whence);
/* Get file position */
uint64_t get_file_posn(WFile *f);
/*
 * Determine the type of a file: nonexistent, file, directory or
 * weird. `weird' covers anything else - named pipes, Unix sockets,
 * device files, fish, badgers, you name it. Things marked `weird'
 * will be skipped over in recursive file transfers, so the only
 * real reason for not lumping them in with `nonexistent' is that
 * it allows a slightly more sane error message.
 */
enum {
    FILE_TYPE_NONEXISTENT, FILE_TYPE_FILE, FILE_TYPE_DIRECTORY, FILE_TYPE_WEIRD
};
int file_type(const char *name);

/*
 * Read all the file names out of a directory.
 */
typedef struct DirHandle DirHandle;
DirHandle *open_directory(const char *name, const char **errmsg);
/* The string returned from this will need freeing if not NULL */
char *read_filename(DirHandle *dir);
void close_directory(DirHandle *dir);

/*
 * Test a filespec to see whether it's a local wildcard or not.
 * Return values:
 *
 *  - WCTYPE_WILDCARD (this is a wildcard).
 *  - WCTYPE_FILENAME (this is a single file name).
 *  - WCTYPE_NONEXISTENT (whichever it was, nothing of that name exists).
 *
 * Some platforms may choose not to support local wildcards when
 * they come from the command line; in this case they simply never
 * return WCTYPE_WILDCARD, but still test the file's existence.
 * (However, all platforms will probably want to support wildcards
 * inside the PSFTP CLI.)
 */
enum {
    WCTYPE_NONEXISTENT, WCTYPE_FILENAME, WCTYPE_WILDCARD
};
int test_wildcard(const char *name, bool cmdline);

/*
 * Actually return matching file names for a local wildcard.
 */
typedef struct WildcardMatcher WildcardMatcher;
WildcardMatcher *begin_wildcard_matching(const char *name);
/* The string returned from this will need freeing if not NULL */
char *wildcard_get_filename(WildcardMatcher *dir);
void finish_wildcard_matching(WildcardMatcher *dir);

/*
 * Vet a filename returned from the remote host, to ensure it isn't
 * in some way malicious. The idea is that this function is applied
 * to filenames returned from FXP_READDIR, which means we can panic
 * if we see _anything_ resembling a directory separator.
 *
 * Returns true if the filename is kosher, false if dangerous.
 */
bool vet_filename(const char *name);

/*
 * Create a directory. Returns true on success, false on error.
 */
bool create_directory(const char *name);

/*
 * Concatenate a directory name and a file name. The way this is
 * done will depend on the OS.
 */
char *dir_file_cat(const char *dir, const char *file);

/*
 * Return a pointer to the portion of str that comes after the last
 * path component separator.
 *
 * If 'local' is false, path component separators are taken to just be
 * '/', on the assumption that we're discussing the path syntax on the
 * server. But if 'local' is true, the separators are whatever the
 * local OS will treat that way - so that includes '\' and ':' on
 * Windows.
 *
 * This function has the annoying strstr() property of taking a const
 * char * and returning a char *. You should treat it as if it was a
 * pair of overloaded functions, one mapping mutable->mutable and the
 * other const->const :-(
 */
char *stripslashes(const char *str, bool local);

/* ----------------------------------------------------------------------
 * In psftpcommon.c
 */

/*
 * qsort comparison routine for fxp_name structures. Sorts by real
 * file name.
 */
int sftp_name_compare(const void *av, const void *bv);

/*
 * Shared code for outputting a directory listing in response to a
 * stream of name structures from FXP_READDIR operations. Used by
 * psftp's ls command and pscp -ls.
 */
struct list_directory_from_sftp_ctx;
struct fxp_name; /* in sftp.h */
struct list_directory_from_sftp_ctx *list_directory_from_sftp_new(void);
void list_directory_from_sftp_feed(struct list_directory_from_sftp_ctx *ctx,
                                   struct fxp_name *name);
void list_directory_from_sftp_finish(struct list_directory_from_sftp_ctx *ctx);
void list_directory_from_sftp_free(struct list_directory_from_sftp_ctx *ctx);
/* Callbacks provided by the tool front end */
void list_directory_from_sftp_warn_unsorted(void);
void list_directory_from_sftp_print(struct fxp_name *name);

#endif /* PUTTY_PSFTP_H */
