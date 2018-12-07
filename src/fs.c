/*
 * fs.c
 * 
 * Error-handling wrappers around FatFS.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

static struct cancellation fs_cancellation[2];
static struct cancellation *next_cancellation = &fs_cancellation[0];
static FRESULT fs_fresult;

FRESULT F_call_cancellable(int (*fn)(void *), void *arg)
{
    FRESULT res;
    struct cancellation *c = next_cancellation++;
    ASSERT((c - fs_cancellation) < ARRAY_SIZE(fs_cancellation));
    ASSERT(!cancellation_is_active(c));
    (void)call_cancellable_fn(c, fn, arg);
    next_cancellation--;
    res = fs_fresult;
    fs_fresult = FR_OK;
    return res;
}

static void handle_fr(FRESULT fr)
{
    struct cancellation *c = next_cancellation - 1;
    ASSERT(!fs_fresult && (c >= fs_cancellation));
    if (fr == FR_OK)
        return;
    fs_fresult = fr;
    cancel_call(c);
}

void F_die(FRESULT fr)
{
    handle_fr(fr);
}

static BYTE mask_mode(BYTE mode)
{
    if (volume_readonly())
        mode &= FA_READ;
    return mode;
}

FRESULT F_try_open(FIL *fp, const TCHAR *path, BYTE mode)
{
    FRESULT fr = f_open(fp, path, mask_mode(mode));
    switch (fr) {
    case FR_NO_FILE:
    case FR_NO_PATH:
        break;
    default:
        handle_fr(fr);
        break;
    }
    return fr;
}

void F_open(FIL *fp, const TCHAR *path, BYTE mode)
{
    FRESULT fr = f_open(fp, path, mask_mode(mode));
    handle_fr(fr);
}

void F_close(FIL *fp)
{
    FRESULT fr = f_close(fp);
    handle_fr(fr);
}

void F_read(FIL *fp, void *buff, UINT btr, UINT *br)
{
    UINT _br;
    FRESULT fr = f_read(fp, buff, btr, &_br);
    if (br != NULL) {
        *br = _br;
    } else if (_br < btr) {
        memset((char *)buff + _br, 0, btr - _br);
    }
    handle_fr(fr);
}

#if !FF_FS_READONLY

void F_write(FIL *fp, const void *buff, UINT btw, UINT *bw)
{
    UINT _bw;
    FRESULT fr;
    if (volume_readonly()) {
        /* Read-only: silently drop. */
        if (bw) *bw = btw;
        return;
    }
    if (!fp->dir_ptr) {
        /* File cannot be resized. Clip the write size. */
        btw = min_t(UINT, btw, f_size(fp) - f_tell(fp));
    }
    fr = f_write(fp, buff, btw, &_bw);
    if (bw != NULL) {
        *bw = _bw;
    } else if ((fr == FR_OK) && (_bw < btw)) {
        fr = FR_DISK_FULL;
    }
    handle_fr(fr);
}

void F_sync(FIL *fp)
{
    FRESULT fr = f_sync(fp);
    handle_fr(fr);
}

void F_truncate(FIL *fp)
{
    FRESULT fr = volume_readonly() ? FR_OK : f_truncate(fp);
    handle_fr(fr);
}

#endif /* !FF_FS_READONLY */

void F_lseek(FIL *fp, DWORD ofs)
{
    FRESULT fr;
#if !FF_FS_READONLY
    if (!fp->dir_ptr) {
        /* File cannot be resized. Clip the seek offset. */
        ofs = min(ofs, f_size(fp));
    }
#endif
    fr = f_lseek(fp, ofs);
    handle_fr(fr);
}

void F_opendir(DIR *dp, const TCHAR *path)
{
    FRESULT fr = f_opendir(dp, path);
    handle_fr(fr);
}

void F_closedir(DIR *dp)
{
    FRESULT fr = f_closedir(dp);
    handle_fr(fr);
}

void F_readdir(DIR *dp, FILINFO *fno)
{
    FRESULT fr = f_readdir(dp, fno);
    handle_fr(fr);
}

void F_findfirst(DIR *dp, FILINFO *fno, const TCHAR *path,
                 const TCHAR *pattern)
{
    FRESULT fr = f_findfirst(dp, fno, path, pattern);
    handle_fr(fr);
}

void F_findnext(DIR *dp, FILINFO *fno)
{
    FRESULT fr = f_findnext(dp, fno);
    handle_fr(fr);
}

void F_chdir(const TCHAR *path)
{
    FRESULT fr = f_chdir(path);
    handle_fr(fr);
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
