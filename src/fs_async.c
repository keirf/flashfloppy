/*
 * fs_async.c
 * 
 * Non-blocking error-handling wrappers around FatFS.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com> and Eric Anderson
 * <ejona86@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

union op_args {
    struct {
        FSIZE_t ofs;
    } lseek;
    struct {
        void *buff;
        UINT btr;
        UINT *br;
    } read;
    struct {
        const void *buff;
        UINT btw;
        UINT *bw;
    } write;
    struct {
        BYTE *buff;
        LBA_t sector;
        UINT count;
    } disk_read;
    struct {
        const BYTE *buff;
        LBA_t sector;
        UINT count;
    } disk_write;
    struct {
        BYTE cmd;
        void* buff;
        DRESULT *res;
    } disk_ioctl;
};

struct op;

typedef void (*f_op_func)(struct op *op);

struct op {
    f_op_func func;
    FIL *fp;
    union op_args args;
    bool_t cancelled;
};

#define OPS_LEN 4 /* Power of 2. */
#define OPS_MASK(x) ((x)&(OPS_LEN-1))
static struct {
    struct op ops[OPS_LEN];
    int prod, cons;
} f_async_queue;

bool_t F_async_isdone(FOP oper) {
    ASSERT(oper - f_async_queue.prod < 0);
    return oper - f_async_queue.cons < 0;
}

void F_async_wait(FOP oper) {
    while (!F_async_isdone(oper))
        thread_yield();
}

void F_async_cancel(FOP oper) {
    if (F_async_isdone(oper))
        return;
    f_async_queue.ops[OPS_MASK(oper)].cancelled = TRUE;
}

void F_async_cancel_all(void) {
    for (int i = 0; i < OPS_LEN; i++)
        f_async_queue.ops[i].cancelled = TRUE;
}

FOP F_async_get_completed_op(void) {
    return f_async_queue.cons - 4; /* "Random" op. Chosen by fair dice roll. */
}

static FOP enqueue(f_op_func func, FIL *fp, union op_args *args) {
    struct op *op;
    bool_t printed = FALSE;
    while (f_async_queue.prod - f_async_queue.cons >= OPS_LEN) {
        if (!printed) {
            printk("async queue full; blocking on I/O\n");
            printk("0: %x 1: %x\n",
                    f_async_queue.ops[OPS_MASK(f_async_queue.prod-1)].func,
                    f_async_queue.ops[OPS_MASK(f_async_queue.prod-2)].func);
            printed = TRUE;
        }
        thread_yield();
    }
    op = &f_async_queue.ops[OPS_MASK(f_async_queue.prod)];
    op->func = func;
    op->fp = fp;
    op->args = *args;
    op->cancelled = FALSE;
    return f_async_queue.prod++;
}

void F_async_drain(void) {
    while (f_async_queue.prod != f_async_queue.cons) {
        struct op *op = &f_async_queue.ops[OPS_MASK(f_async_queue.cons)];
        if (!op->cancelled) {
            op->func(op);
        }
        f_async_queue.cons++;
    }
}

static void do_lseek(struct op *op) {
    /* Short-circuit if already appropriately positioned. The caller of
     * F_lseek_async can't check fptr themselves like they could using the
     * blocking API. */
    if (op->args.lseek.ofs == op->fp->fptr)
        return;
    F_lseek(op->fp, op->args.lseek.ofs);
}

FOP F_lseek_async(FIL *fp, FSIZE_t ofs) {
    union op_args args = { .lseek = {ofs} };
    return enqueue(do_lseek, fp, &args);
}

static void do_read(struct op *op) {
    time_t start = time_now(), duration;
    F_read(op->fp, op->args.read.buff, op->args.read.btr, op->args.read.br);
    duration = time_since(start);
    if (duration > time_us(9000))
        printk("Lengthy read. %d bytes took %d us\n", op->args.read.btr,
                duration / TIME_MHZ);
}

FOP F_read_async(FIL *fp, void *buff, UINT btr, UINT *br) {
    union op_args args = { .read = {buff, btr, br} };
    return enqueue(do_read, fp, &args);
}

static void do_write(struct op *op) {
    time_t start = time_now(), duration;
    F_write(op->fp, op->args.write.buff, op->args.write.btw, op->args.write.bw);
    duration = time_since(start);
    if (duration > time_us(9000))
        printk("Lengthy write. %d bytes took %d us\n", op->args.write.btw,
                duration / TIME_MHZ);
}

FOP F_write_async(FIL *fp, const void *buff, UINT btw, UINT *bw) {
    union op_args args = { .write = {buff, btw, bw} };
    return enqueue(do_write, fp, &args);
}

static void do_sync(struct op *op) {
    F_sync(op->fp);
}

FOP F_sync_async(FIL *fp) {
    union op_args args = { 0 };
    return enqueue(do_sync, fp, &args);
}

static void do_disk_read(struct op *op) {
    if (disk_read((uintptr_t) op->fp, op->args.disk_read.buff,
            op->args.disk_read.sector, op->args.disk_read.count) != RES_OK)
        F_die(FR_DISK_ERR);
}

FOP disk_read_async(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
    union op_args args = { .disk_read = {buff, sector, count} };
    return enqueue(do_disk_read, (void*)(uintptr_t) pdrv, &args);
}

static void do_disk_write(struct op *op) {
    time_t start = time_now();
    if (disk_write((uintptr_t) op->fp, op->args.disk_write.buff,
            op->args.disk_write.sector, op->args.disk_write.count) != RES_OK)
        F_die(FR_DISK_ERR);
    printk("Disk write %08x (size 0x%x): %u us\n",
            op->args.disk_write.sector,
            op->args.disk_write.count,
            time_since(start) / TIME_MHZ);
}

FOP disk_write_async(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
    union op_args args = { .disk_write = {buff, sector, count} };
    return enqueue(do_disk_write, (void*)(uintptr_t) pdrv, &args);
}

static void do_disk_ioctl(struct op *op) {
    DRESULT res = disk_ioctl((uintptr_t) op->fp, op->args.disk_ioctl.cmd,
            op->args.disk_ioctl.buff);
    if (op->args.disk_ioctl.res != NULL)
        *op->args.disk_ioctl.res = res;
    else if (res != RES_OK)
        F_die(FR_DISK_ERR);
}

FOP disk_ioctl_async(BYTE pdrv, BYTE cmd, void* buff, DRESULT *res) {
    union op_args args = { .disk_ioctl = {cmd, buff, res} };
    return enqueue(do_disk_ioctl, (void*)(uintptr_t) pdrv, &args);
}
