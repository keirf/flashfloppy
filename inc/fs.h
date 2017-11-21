/*
 * fs.h
 * 
 * Error-handling wrappers around FatFS.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

FRESULT F_call_cancellable(int (*fn)(void *), void *arg);

void F_die(FRESULT fr);

FRESULT F_try_open(FIL *fp, const TCHAR *path, BYTE mode);
void F_open(FIL *fp, const TCHAR *path, BYTE mode);
void F_close(FIL *fp);
void F_read(FIL *fp, void *buff, UINT btr, UINT *br);
void F_write(FIL *fp, const void *buff, UINT btw, UINT *bw);
void F_sync(FIL *fp);
void F_lseek(FIL *fp, DWORD ofs);
void F_truncate(FIL *fp);
void F_opendir(DIR *dp, const TCHAR *path);
void F_closedir(DIR *dp);
void F_readdir(DIR *dp, FILINFO *fno);
void F_findfirst(DIR *dp, FILINFO *fno, const TCHAR *path,
                 const TCHAR *pattern);
void F_findnext(DIR *dp, FILINFO *fno);
void F_chdir(const TCHAR *path);

#if 0
FRESULT f_mkdir(const TCHAR *path);
FRESULT f_rename(const TCHAR *path_old, const TCHAR *path_new);
FRESULT f_stat(const TCHAR *path, FILINFO *fno);
FRESULT f_chmod(const TCHAR *path, BYTE attr, BYTE mask);
FRESULT f_utime(const TCHAR *path, const FILINFO *fno);
FRESULT f_getcwd(TCHAR *buff, UINT len);
FRESULT f_getfree(const TCHAR *path, DWORD *nclst, FATFS* *fatfs);
FRESULT f_mount(FATFS *fs, const TCHAR *path, BYTE opt);
FRESULT f_unlink(const TCHAR *path);
#endif

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
