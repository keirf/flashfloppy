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

static struct cancellation fs_cancellation;
static FRESULT fs_fresult;

FRESULT F_call_cancellable(int (*fn)(void))
{
    FRESULT res;
    ASSERT(!cancellation_is_active(&fs_cancellation));
    (void)call_cancellable_fn(&fs_cancellation, fn);
    res = fs_fresult;
    fs_fresult = FR_OK;
    return res;
}

FRESULT F_fresult(void)
{
    return fs_fresult;
}

static void handle_fr(FRESULT fr)
{
    ASSERT(!fs_fresult && cancellation_is_active(&fs_cancellation));
    if (fr == FR_OK)
        return;
    fs_fresult = fr;
    cancel_call(&fs_cancellation);
}

void F_open(FIL *fp, const TCHAR *path, BYTE mode)
{
    FRESULT fr = f_open(fp, path, mode);
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

void F_write(FIL *fp, const void *buff, UINT btw, UINT *bw)
{
    UINT _bw;
    FRESULT fr = f_write(fp, buff, btw, &_bw);
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

void F_lseek(FIL *fp, DWORD ofs)
{
    FRESULT fr = f_lseek(fp, ofs);
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

void F_unlink(const TCHAR *path)
{
    FRESULT fr = f_unlink(path);
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
