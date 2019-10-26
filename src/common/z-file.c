/**
 * \file z-file.c
 * \brief Low-level file (and directory) handling
 *
 * Copyright (c) 1997-2007 Ben Harrison, pelpel, Andi Sidwell, Matthew Jones
 *
 * This work is free software; you can redistribute it and/or modify it
 * under the terms of:
 *
 * b) the "Angband licence":
 *    This software may be copied and distributed for educational, research,
 *    and not for profit purposes provided that this copyright and statement
 *    are included in all such copies.  Other copyrights may also apply.
 */
/* Forward-compatibility with V4XX */
#undef true
#undef false
#define true TRUE
#define false FALSE
#define PATH_SEPC PATH_SEP[0]
#define mem_zalloc ralloc
#define assert(X) false
#define mem_free rnfree
#ifdef SET_UID
#define UNUX
#define SETGID
# if defined(SAFE_SETUID)
#   if defined(SAFE_SETUID_POSIX)
#      define HAVE_SETEGID
#   else
#      define HAVE_SETRESGID
#   endif
# endif
#endif /* SET_UID */
/* XXX */
#include "h-basic.h"
#include "z-file.h"
#include "z-form.h"
#include "z-util.h"
#include "z-virt.h"


#include <sys/types.h>

#ifdef WINDOWS
# include <windows.h>
# include <io.h>
# ifndef CYGWIN
#  include <direct.h>
# endif
#endif

#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#endif

#if defined (HAVE_DIRENT_H) || defined (CYGWIN)
# include <sys/types.h>
# include <dirent.h>
#endif

#ifdef HAVE_STAT
# include <sys/stat.h>
#endif

#if defined (WINDOWS) && !defined (CYGWIN)
# define my_mkdir(path, perms) mkdir(path)
#elif defined(HAVE_MKDIR) || defined(MACH_O_CARBON) || defined (CYGWIN)
# define my_mkdir(path, perms) mkdir(path, perms)
#else
# define my_mkdir(path, perms) false
#endif

#if defined (WINDOWS) && !defined (CYGWIN)
#ifdef WIN32
#define INVALID_FILE_NAME (DWORD)0xFFFFFFFF
#else /* WIN32 -> WIN16/DOS */
#define FA_LABEL    0x08        /* Volume label */
#define FA_DIREC    0x10        /* Directory */
unsigned _cdecl _dos_getfileattr(const char *, unsigned *);
#endif /* WIN32/WIN16 */
#endif

/**
 * Player info
 */
int player_uid;
int player_egid;




/**
 * Drop permissions
 */
void safe_setuid_drop(void)
{
#ifdef SETGID
# if defined(HAVE_SETRESGID)

	if (setresgid(-1, getgid(), -1) != 0)
		quit("setegid(): cannot drop permissions correctly!");

# else

	if (setegid(getgid()) != 0)
		quit("setegid(): cannot drop permissions correctly!");

# endif
#endif /* SETGID */
}


/**
 * Grab permissions
 */
void safe_setuid_grab(void)
{
#ifdef SETGID
# if defined(HAVE_SETRESGID)

	if (setresgid(-1, player_egid, -1) != 0)
		quit("setegid(): cannot grab permissions correctly!");

# elif defined(HAVE_SETEGID)

	if (setegid(player_egid) != 0)
		quit("setegid(): cannot grab permissions correctly!");

# endif
#endif /* SETGID */
}




/**
 * Apply special system-specific processing before dealing with a filename.
 */
static void path_parse(char *buf, size_t max, const char *file)
{
	/* Accept the filename */
	my_strcpy(buf, file, max);
}


static void path_process(char *buf, size_t len, size_t *cur_len,
						 const char *path)
{
#if defined(UNIX)

	/* Home directory on Unixes */
	if (path[0] == '~') {
		const char *s;
		const char *username = path + 1;

		struct passwd *pw;
		char user[128];

		/* Look for non-user portion of the file */
		s = strstr(username, PATH_SEP);
		if (s) {
			int i;

			/* Keep username a decent length */
			if (s >= username + sizeof(user)) return;

			for (i = 0; username < s; ++i) user[i] = *username++;
			user[i] = '\0';
			username = user;
		}

		/* Look up a user (or "current" user) */
		pw = username[0] ? getpwnam(username) : getpwuid(getuid());
		if (!pw) return;

		/* Copy across */
		strnfcat(buf, len, cur_len, "%s%s", pw->pw_dir, PATH_SEP);
		if (s) strnfcat(buf, len, cur_len, "%s", s);
	} else

#endif /* defined(UNIX) */

		strnfcat(buf, len, cur_len, "%s", path);
}



/**
 * Create a new path string by appending a 'leaf' to 'base'.
 *
 * On Unixes, we convert a tidle at the beginning of a basename to mean the
 * directory, complicating things a little, but better now than later.
 *
 * Remember to free the return value.
 */
size_t path_build(char *buf, size_t len, const char *base, const char *leaf)
{
	size_t cur_len = 0;
	int starts_with_separator;

	buf[0] = '\0';

	if (!leaf || !leaf[0]) {
		if (base && base[0])
			path_process(buf, len, &cur_len, base);

		return cur_len;
	}


	/*
	 * If the leafname starts with the seperator,
	 *   or with the tilde (on Unix),
	 *   or there's no base path,
	 * We use the leafname only.
	 */
	starts_with_separator = (!base || !base[0]) || prefix(leaf, PATH_SEP);
#if defined(UNIX)
	starts_with_separator = starts_with_separator || leaf[0] == '~';
#endif
	if (starts_with_separator) {
		path_process(buf, len, &cur_len, leaf);
		return cur_len;
	}


	/* There is both a relative leafname and a base path from which it is
	 * relative */
	path_process(buf, len, &cur_len, base);

	if (!suffix(base, PATH_SEP)) {
		/* Append separator if it isn't already in the string. */
		strnfcat(buf, len, &cur_len, "%s", PATH_SEP);
	}

	path_process(buf, len, &cur_len, leaf);

	return cur_len;
}

/**
 * Return the index of the filename in a path, using PATH_SEPC. If no path
 * separator is found, return 0.
 */
size_t path_filename_index(const char *path)
{
	size_t i;

	if (strlen(path) == 0)
		return 0;

	for (i = strlen(path) - 1; i >= 0; i--) {
		if (path[i] == PATH_SEPC)
			return i + 1;
	}

	return 0;
}

/**
 * ------------------------------------------------------------------------
 * File-handling API
 * ------------------------------------------------------------------------ */


/* Some defines for compatibility between various build platforms */
#ifndef S_IRUSR
#define S_IRUSR S_IREAD
#endif

#ifndef S_IWUSR
#define S_IWUSR S_IWRITE
#endif

/* if the flag O_BINARY is not defined, it is not needed , but we still
 * need it defined so it will compile */
#ifndef O_BINARY
#define O_BINARY 0
#endif

/* Avoid a compiler warning when cross compiling for windows */
#ifdef __STRICT_ANSI__
FILE *fdopen(int handle, const char *mode);
#endif

#ifdef USE_SDL_RWOPS
#include <SDL.h>
#endif

/* Private structure to hold file pointers and useful info. */
struct ang_file
{
#ifdef USE_SDL_RWOPS
	SDL_RWops *fh;
	int error;
#else
	FILE *fh;
#endif
	char *fname;
	file_mode mode;
};



/** Utility functions **/

/**
 * Delete file 'fname'.
 */
bool file_delete(const char *fname)
{
	char buf[1024];

	/* Get the system-specific paths */
	path_parse(buf, sizeof(buf), fname);

	return (remove(buf) == 0);
}

/**
 * Move file 'fname' to 'newname'.
 */
bool file_move(const char *fname, const char *newname)
{
	char buf[1024];
	char aux[1024];

	/* Get the system-specific paths */
	path_parse(buf, sizeof(buf), fname);
	path_parse(aux, sizeof(aux), newname);

	return (rename(buf, aux) == 0);
}


/**
 * Decide whether a file exists or not.
 */

#if defined(HAVE_STAT)

bool file_exists(const char *fname)
{
	struct stat st;
	return (stat(fname, &st) == 0);
}

#elif defined(WINDOWS)

bool file_exists(const char *fname)
{
	char path[MAX_PATH];
	DWORD attrib;

	/* API says we mustn't pass anything larger than MAX_PATH */
	my_strcpy(path, fname, sizeof(path));

	attrib = GetFileAttributes(path);
	if (attrib == INVALID_FILE_NAME) return false;
	if (attrib & FILE_ATTRIBUTE_DIRECTORY) return false;

	return true;
}

#else

bool file_exists(const char *fname)
{
	ang_file *f = file_open(fname, MODE_READ, 0);

	if (f) file_close(f);
	return (f ? true : false);
}

#endif

/**
 * Return true if first is newer than second, false otherwise.
 */
bool file_newer(const char *first, const char *second)
{
#ifdef HAVE_STAT
	struct stat stat1, stat2;

	/* If the first doesn't exist, the first is not newer. */
	if (stat(first, &stat1) != 0) return false;

	/* If the second doesn't exist, the first is always newer. */
	if (stat(second, &stat2) != 0) return true;

	/* Compare modification times. */
	return stat1.st_mtime > stat2.st_mtime ? true : false;
#else /* HAVE_STAT */
	return false;
#endif /* !HAVE_STAT */
}




/** File-handle functions **/

void (*file_open_hook)(const char *path, file_type ftype);

/**
 * Open file 'fname', in mode 'mode', with filetype 'ftype'.
 * Returns file handle or NULL.
 */
ang_file *file_open(const char *fname, file_mode mode, file_type ftype)
{
	ang_file *f = mem_zalloc(sizeof(ang_file));
	char buf[1024];

	(void)ftype;

	/* Get the system-specific path */
	path_parse(buf, sizeof(buf), fname);

#ifdef USE_SDL_RWOPS
{
	char *rwm;
	switch (mode) {
		case MODE_WRITE: rwm = "wb"; break;
		case MODE_READ: rwm = "rb"; break;
		case MODE_APPEND: rwm = "a+"; break;
		case MODE_READWRITE: rwm = "r+b"; break;
		default: assert(0); break;
	}
	f->error = 0;
	f->fh = SDL_RWFromFile(buf, rwm);
}
#else
	switch (mode) {
		case MODE_WRITE: {
			if (ftype == FTYPE_SAVE) {
				/* open only if the file does not exist */
				int fd;
				fd = open(buf, O_CREAT | O_EXCL | O_WRONLY | O_BINARY, S_IRUSR | S_IWUSR);
				if (fd < 0) {
					/* there was some error */
					f->fh = NULL;
				} else {
					f->fh = fdopen(fd, "wb");
				}
			} else {
				f->fh = fopen(buf, "wb");
			}
			break;
		}
		case MODE_READ:
			f->fh = fopen(buf, "rb");
			break;
		case MODE_APPEND:
			f->fh = fopen(buf, "a+");
			break;
		case MODE_READWRITE:
			f->fh = fopen(buf, "r+b");
			break;
		default:
			assert(0);
	}
#endif
	if (f->fh == NULL) {
		mem_free(f);
		return NULL;
	}

	f->fname = (char*)string_make(buf);
	f->mode = mode;

	if (mode != MODE_READ && file_open_hook)
		file_open_hook(buf, ftype);

	return f;
}


/**
 * Close file handle 'f'.
 */
bool file_close(ang_file *f)
{
#ifdef USE_SDL_RWOPS
	if (SDL_RWclose(f->fh) != 0)
		return false;
#else
	if (fclose(f->fh) != 0)
		return false;
#endif

	mem_free(f->fname);
	mem_free(f);

	return true;
}


/**
 * Check errors in file handle 'f'.
 */
bool file_error(ang_file *f)
{
#ifdef USE_SDL_RWOPS
	if (f->error) return true;
	return false;
#else
	if (ferror(f->fh) != 0)
		return true;

	if (f->mode == MODE_WRITE && fflush(f->fh) == EOF)
		return true;

	return false;
#endif
}


/** Locking functions **/

/**
 * Lock a file using POSIX locks, on platforms where this is supported.
 */
bool file_lock(ang_file *f)
{
#if defined(HAVE_FCNTL_H) && defined(UNIX)
	struct flock lock;
	lock.l_type = (f->mode == MODE_READ ? F_RDLCK : F_WRLCK);
	lock.l_whence = SEEK_SET;
	lock.l_start = 0;
	lock.l_len = 0;
	lock.l_pid = 0;
	fcntl(fileno(f->fh), F_SETLKW, &lock);
#endif /* HAVE_FCNTL_H && UNIX */
	return true;
}

/**
 * Unlock a file locked using file_lock().
 */
bool file_unlock(ang_file *f)
{
#if defined(HAVE_FCNTL_H) && defined(UNIX)
	struct flock lock;
	lock.l_type = F_UNLCK;
	lock.l_whence = SEEK_SET;
	lock.l_start = 0;
	lock.l_len = 0;
	lock.l_pid = 0;
	fcntl(fileno(f->fh), F_SETLK, &lock);
#endif /* HAVE_FCNTL_H && UNIX */
	return true;
}


/** Byte-based IO and functions **/

/**
 * Seek to location 'pos' in file 'f', from the current position.
 */
bool file_skip(ang_file *f, int bytes)
{
#ifdef USE_SDL_RWOPS
	return (SDL_RWseek(f->fh, bytes, RW_SEEK_CUR) > -1);
#else
	return (fseek(f->fh, bytes, SEEK_CUR) == 0);
#endif
}

/**
 * Seek to location 'pos' in file 'f', from the start of file.
 */
bool file_seek(ang_file *f, int bytes)
{
#ifdef USE_SDL_RWOPS
	return (SDL_RWseek(f->fh, bytes, RW_SEEK_SET) > -1);
#else
	return (fseek(f->fh, bytes, SEEK_SET) == 0);
#endif
}

/**
 * Return current location in file 'f'.
 */
size_t file_tell(ang_file *f)
{
#ifdef USE_SDL_RWOPS
	Sint64 pos = SDL_RWtell(f->fh);
	if (pos < 0)
	{
		f->error = TRUE;
		return 0;
	}
	return (size_t)pos;
#else
	return ftell(f->fh);
#endif
}

/**
 * Read a single, 8-bit character from file 'f'.
 */
bool file_readc(ang_file *f, byte *b)
{
#ifdef USE_SDL_RWOPS
	size_t i = SDL_RWread(f->fh, b, 1, 1);
	if (i == 0) return false;
	return true;
#else
	int i = fgetc(f->fh);

	if (i == EOF)
		return false;

	*b = (byte)i;
	return true;
#endif
}

/**
 * Write a single, 8-bit character 'b' to file 'f'.
 */
bool file_writec(ang_file *f, byte b)
{
#ifdef USE_SDL_RWOPS
	size_t i = SDL_RWwrite(f->fh, (void*)&b, 1, 1);
	if (i == 0) return false;
	return true;
#else
	return file_write(f, (const char *)&b, 1);
#endif
}

/**
 * Read 'n' bytes from file 'f' into array 'buf'.
 */
size_t file_read(ang_file *f, char *buf, size_t n)
{
#ifdef USE_SDL_RWOPS
	size_t read;

	SDL_ClearError();
	read = SDL_RWread(f->fh, (void*)buf, 1, n);

	if (read == 0 && SDL_GetError()[0] == '\0')
		return -1;
	else
		return read;
#else
	size_t read = fread(buf, 1, n, f->fh);

	if (read == 0 && ferror(f->fh))
		return -1;
	else
		return read;
#endif
}

/**
 * Append 'n' bytes of array 'buf' to file 'f'.
 */
bool file_write(ang_file *f, const char *buf, size_t n)
{
#ifdef USE_SDL_RWOPS
	return SDL_RWwrite(f->fh, (void*)buf, 1, n) == n;
#else
	return fwrite(buf, 1, n, f->fh) == n;
#endif
}

/** Line-based IO **/

/**
 * Read a line of text from file 'f' into buffer 'buf' of size 'n' bytes.
 *
 * Support both \r\n and \n as line endings, but not the outdated \r that used
 * to be used on Macs.  Replace non-printables with '?', and \ts with ' '.
 */
#define TAB_COLUMNS 4

bool file_getl(ang_file *f, char *buf, size_t len)
{
	bool seen_cr = false;
	byte b;
	size_t i = 0;

	/* Leave a byte for the terminating 0 */
	size_t max_len = len - 1;

	while (i < max_len) {
		char c;

		if (!file_readc(f, &b)) {
			buf[i] = '\0';
			return (i == 0) ? false : true;
		}

		c = (char) b;

		if (c == '\r') {
			seen_cr = true;
			continue;
		}

		if (seen_cr && c != '\n') {
			file_skip(f, -1);//fseek(f->fh, -1, SEEK_CUR);
			buf[i] = '\0';
			return true;
		}

		if (c == '\n') {
			buf[i] = '\0';
			return true;
		}

		/* Expand tabs */
		if (c == '\t') {
			/* Next tab stop */
			size_t tabstop = ((i + TAB_COLUMNS) / TAB_COLUMNS) * TAB_COLUMNS;
			if (tabstop >= len) break;

			/* Convert to spaces */
			while (i < tabstop)
				buf[i++] = ' ';

			continue;
		}

		buf[i++] = c;
	}

	buf[i] = '\0';
	return true;
}

/**
 * Append a line of text 'buf' to the end of file 'f', using system-dependent
 * line ending.
 */
bool file_put(ang_file *f, const char *buf)
{
	return file_write(f, buf, strlen(buf));
}

/*
 * The comp.lang.c FAQ recommends this pairing for varargs functions.
 * See <http://c-faq.com/varargs/handoff.html>
 */

/**
 * Append a formatted line of text to the end of file 'f'.
 *
 * file_putf() is the ellipsis version. Most file output will call this
 * version. It calls file_vputf() to do the real work. It returns true
 * if the write was successful and false otherwise.
 */
bool file_putf(ang_file *f, const char *fmt, ...)
{
	va_list vp;
	bool status;

	if (!f) return false;

	va_start(vp, fmt);
	status = file_vputf(f, fmt, vp);
	va_end(vp);

	return status;
}

/**
 * Append a formatted line of text to the end of file 'f'.
 *
 * file_vputf() is the va_list version. It returns true if the write was
 * successful and false otherwise.
 */
bool file_vputf(ang_file *f, const char *fmt, va_list vp)
{
	char buf[1024];
	int n;

	if (!f) return false;

	n = vstrnfmt(buf, sizeof(buf), fmt, vp);

	/* The semantics of vstrnfmt are weird and its return
	 * value is ill-defined. However, return value of zero
	 * almost definitely means there was an error (unless you pass it
	 * an empty format string with zero varargs, I guess). */
	if (n == 0) return false;

	return file_put(f, buf);
}

/**
 * Copy a file.
 */
bool file_copy(const char *src, const char *dst, file_type ftype)
{
	ang_file *sfile;
	ang_file *dfile;
	char buf[1024];
	size_t n;

	sfile = file_open(src, MODE_READ, ftype);
	if (sfile == NULL)
	{
		return false;
	}

	dfile = file_open(dst, MODE_WRITE, ftype);
	if (dfile == NULL)
	{
		file_close(sfile);
		return false;
	}

	while ((n = file_read(sfile, buf, 1024)))
	{
		if (!file_write(dfile, buf, n)) break;
    }

	file_close(sfile);
	file_close(dfile);
	return true;
}

#ifdef WINDOWS
#ifndef INVALID_FILE_NAME
#define INVALID_FILE_NAME (DWORD)0xFFFFFFFF
#endif
#endif

bool dir_exists(const char *path)
{
#ifdef WINDOWS
# ifdef WIN32
	DWORD attrib;

	/* Examine */
	attrib = GetFileAttributes(path);

	/* Require valid filename */
	if (attrib == INVALID_FILE_NAME) return (FALSE);

	/* Require directory */
	if (!(attrib & FILE_ATTRIBUTE_DIRECTORY)) return (FALSE);
# else /* WIN16 */
	unsigned int attrib;
	/* Examine and verify */
	if (_dos_getfileattr(path, &attrib)) return (FALSE);

	/* Prohibit something */
	if (attrib & FA_LABEL) return (FALSE);

	/* Require directory */
	if (!(attrib & FA_DIREC)) return (FALSE);

# endif /* WIN16/WIN32 */
#else /* Not on WINDOWS */
#ifdef HAVE_STAT
	struct stat buf;
	if (stat(path, &buf) != 0)
	{
		return false;
	}
	else if (buf.st_mode & S_IFDIR)
	{
		return true;
	}
	else
		return false;
#endif
#endif
	/* If we got here, we can't reliably tell
	 * if a directory exists.. */
	return true;
}

#ifdef HAVE_STAT
bool dir_create(const char *path)
{
	const char *ptr;
	char buf[512];

	/* If the directory already exists then we're done */
	if (dir_exists(path)) return true;

	#ifdef WINDOWS
	/* If we're on windows, we need to skip past the "C:" part. */
	if (isalpha(path[0]) && path[1] == ':') path += 2;
	#endif

	/* Iterate through the path looking for path segements. At each step,
	 * create the path segment if it doesn't already exist. */
	for (ptr = path; *ptr; ptr++) {
		if (*ptr == PATH_SEPC) {
			/* Find the length of the parent path string */
			size_t len = (size_t)(ptr - path);

			/* Skip the initial slash */
			if (len == 0) continue;

			/* If this is a duplicate path separator, continue */
			if (*(ptr - 1) == PATH_SEPC) continue;

			/* We can't handle really big filenames */
			if (len - 1 > 512) return false;

			/* Create the parent path string, plus null-padding */
			my_strcpy(buf, path, len + 1);

			/* Skip if the parent exists */
			if (dir_exists(buf)) continue;

			/* The parent doesn't exist, so create it or fail */
			if (my_mkdir(buf, 0755) != 0) return false;
		}
	}
	return my_mkdir(path, 0755) == 0 ? true : false;
}

#else /* HAVE_STAT */
bool dir_create(const char *path) { return false; }
#endif /* !HAVE_STAT */

/**
 * ------------------------------------------------------------------------
 * Directory scanning API
 * ------------------------------------------------------------------------ */


/*
 * For information on what these are meant to do, please read the header file.
 */

#ifdef WINDOWS


/* System-specific struct */
struct ang_dir
{
	HANDLE h;
	char *first_file;
};

ang_dir *my_dopen(const char *dirname)
{
	WIN32_FIND_DATA fd;
	HANDLE h;
	ang_dir *dir;

	/* Try to open it */
	h = FindFirstFile(format("%s\\*", dirname), &fd);

	/* Abort */
	if (h == INVALID_HANDLE_VALUE)
		return NULL;

	/* Set up the handle */
	dir = mem_zalloc(sizeof(ang_dir));
	dir->h = h;
	dir->first_file = string_make(fd.cFileName);

	/* Success */
	return dir;
}

bool my_dread(ang_dir *dir, char *fname, size_t len)
{
	WIN32_FIND_DATA fd;
	bool ok;

	/* Try the first file */
	if (dir->first_file) {
		/* Copy the string across, then free it */
		my_strcpy(fname, dir->first_file, len);
		mem_free(dir->first_file);

		/* Wild success */
		return true;
	}

	/* Try the next file */
	while (1) {
		ok = FindNextFile(dir->h, &fd);
		if (!ok) return false;

		/* Skip directories */
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ||
		    strcmp(fd.cFileName, ".") == 0 ||
		    strcmp(fd.cFileName, "..") == 0)
			continue;

		/* Take this one */
		break;
	}

	/* Copy name */
	my_strcpy(fname, fd.cFileName, len);

	return true;
}

void my_dclose(ang_dir *dir)
{
	/* Close directory */
	if (dir->h)
		FindClose(dir->h);

	/* Free memory */
	mem_free(dir->first_file);
	mem_free(dir);
}

#else /* WINDOWS */

#ifdef HAVE_DIRENT_H

/* Define our ang_dir type */
struct ang_dir
{
	DIR *d;
	char *dirname;
};

ang_dir *my_dopen(const char *dirname)
{
	ang_dir *dir;
	DIR *d;

	/* Try to open the directory */
	d = opendir(dirname);
	if (!d) return NULL;

	/* Allocate memory for the handle */
	dir = mem_zalloc(sizeof(ang_dir));
	if (!dir) {
		closedir(d);
		return NULL;
	}

	/* Set up the handle */
	dir->d = d;
	dir->dirname = (char*)string_make(dirname);

	/* Success */
	return dir;
}

bool my_dread(ang_dir *dir, char *fname, size_t len)
{
	struct dirent *entry;
	struct stat filedata;
	char path[1024];

	assert(dir != NULL);

	/* Try reading another entry */
	while (1) {
		entry = readdir(dir->d);
		if (!entry) return false;

		path_build(path, sizeof path, dir->dirname, entry->d_name);
            
		/* Check to see if it exists */
		if (stat(path, &filedata) != 0)
			continue;

		/* Check to see if it's a directory */
		if (S_ISDIR(filedata.st_mode))
			continue;

		/* We've found something worth returning */
		break;
	}

	/* Copy the filename */
	my_strcpy(fname, entry->d_name, len);

	return true;
}

void my_dclose(ang_dir *dir)
{
	/* Close directory */
	if (dir->d)
		closedir(dir->d);

	/* Free memory */
	mem_free(dir->dirname);
	mem_free(dir);
}

#endif /* HAVE_DIRENT_H */
#endif /* WINDOWS */
