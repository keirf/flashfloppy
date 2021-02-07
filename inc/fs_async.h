/*
 * fs_async.h
 * 
 * Non-blocking error-handling wrappers around FatFS.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com> and Eric Anderson
 * <ejona86@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

typedef int FOP;

FOP F_lseek_async(FIL *fp, FSIZE_t ofs);
FOP F_read_async(FIL *fp, void *buff, UINT btr, UINT *br);
FOP F_write_async(FIL *fp, const void *buff, UINT btw, UINT *bw);
FOP F_sync_async(FIL *fp);

/* Returns TRUE if oper has completed or is cancelled. */
bool_t F_async_isdone(FOP oper);

/* Blocks until oper completes or is cancelled. */
void F_async_wait(FOP oper);

/* Requests oper be cancelled. A cancellation may not take effect immediately.
 * Has no effect when called on a completed oper.
 */
void F_async_cancel(FOP oper);

/* Requests all operations to be cancelled. A cancellation may not take effect
 * immediately. */
void F_async_cancel_all(void);

/* Return a completed or cancelled op. Can be used as a "fake" op that is safe
 * to wait or be cancelled. */
FOP F_async_get_completed_op(void);

/* Executes async operations until none remain.  */
void F_async_drain(void);
