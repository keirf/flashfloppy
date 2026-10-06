/* Shared image allocation and write-flux decoding for FDD and QD. */
#include "floppy_internal.h"

/* Allocate and initialise a DMA ring. */
static struct dma_ring *dma_ring_alloc(void)
{
    struct dma_ring *dma = arena_alloc(sizeof(*dma));
    memset(dma, 0, offsetof(struct dma_ring, buf));
    return dma;
}

/* Allocate the shared buffers and mount an image. The caller publishes the
 * returned image and DMA rings to its own IRQ-visible state afterwards. */
struct image *floppy_image_mount(
    struct slot *slot, struct dma_ring **rd, struct dma_ring **wr)
{
    struct image *im;
    struct dma_ring *_dma_rd, *_dma_wr;
    FSIZE_t fastseek_sz;
    DWORD *cltbl;
    FRESULT fr;
    int max_ring_kb = (ram_kb >= 128) ? 64 : (ram_kb >= 64) ? 32 : 8;

    do {

        arena_init();

        _dma_rd = dma_ring_alloc();
        _dma_wr = dma_ring_alloc();

        im = arena_alloc(sizeof(*im));
        memset(im, 0, sizeof(*im));

        /* Create a fast-seek cluster table for the image. */
#define MAX_FILE_FRAGS 511 /* up to a 4kB cluster table */
        cltbl = arena_alloc(0);
        *cltbl = (MAX_FILE_FRAGS + 1) * 2;
        fatfs_from_slot(&im->fp, slot, FA_READ);
        fastseek_sz = f_size(&im->fp);
        if (fastseek_sz == 0) {
            /* Empty or dummy file. */
            cltbl = NULL;
        } else {
            im->fp.cltbl = cltbl;
            fr = f_lseek(&im->fp, CREATE_LINKMAP);
            printk("Fast Seek: %u frags\n", (*cltbl / 2) - 1);
            if (fr == FR_OK) {
                DWORD *_cltbl = arena_alloc(*cltbl * 4);
                ASSERT(_cltbl == cltbl);
            } else if (fr == FR_NOT_ENOUGH_CORE) {
                printk("Fast Seek: FAILED\n");
                cltbl = NULL;
            } else {
                F_die(fr);
            }
        }

        /* ~0 avoids sync match within fewer than 32 bits of scan start. */
        im->write_bc_window = ~0;

        /* Large buffer to absorb write latencies at mass-storage layer. */
        im->bufs.write_bc.len = max_ring_kb*1024; /* power of two */
        im->bufs.write_bc.p = arena_alloc(im->bufs.write_bc.len);

        /* Read BC buffer overlaps the second half of the write BC buffer. This 
         * is because:
         *  (a) The read BC buffer does not need to absorb such large latencies
         *      (reads are much more predictable than writes to mass storage).
         *  (b) By dedicating the first half of the write buffer to writes, we
         *      can safely start processing write flux while read-data is still
         *      processing (eg. in-flight mass storage io). At say 10kB of
         *      dedicated write buffer, this is good for >80ms before colliding
         *      with read buffers, even at HD data rate (1us/bitcell).
         *      This is more than enough time for read
         *      processing to complete. */
        im->bufs.read_bc.len = im->bufs.write_bc.len / 2;
        im->bufs.read_bc.p = (char *)im->bufs.write_bc.p
            + im->bufs.read_bc.len;

        /* Any remaining space is used for staging I/O to mass storage, shared
         * between read and write paths (Change of use of this memory space is
         * fully serialised). */
        im->bufs.write_data.len = arena_avail();
        im->bufs.write_data.p = arena_alloc(im->bufs.write_data.len);
        im->bufs.read_data = im->bufs.write_data;

        /* Minimum allowable buffer space. */
        ASSERT(im->bufs.read_data.len >= 10*1024);

        /* Mount the image file. */
        image_open(im, slot, cltbl);
        if (!im->disk_handler->write_track || volume_readonly())
            slot->attributes |= AM_RDO;
        if (slot->attributes & AM_RDO) {
            printk("Image is R/O\n");
        } else {
            image_extend(im);
        }

    } while (f_size(&im->fp) != fastseek_sz);

    /* After image is extended at mount time, we permit no further changes 
     * to the file metadata. Clear the dirent info to ensure this. */
    im->fp.dir_ptr = NULL;
    im->fp.dir_sect = 0;

    _dma_rd->state = DMA_stopping;

    *rd = _dma_rd;
    *wr = _dma_wr;
    return im;
}

void floppy_wdata_decode(struct dma_ring *dma_wr, struct image *image)
{
    const uint16_t buf_mask = ARRAY_SIZE(dma_wr->buf) - 1;
    uint16_t cons, prod, prev, next;
    uint32_t bc_dat = 0, bc_prod;
    uint32_t *bc_buf = image->bufs.write_bc.p;
    unsigned int sync = image->sync;
    unsigned int bc_bufmask = (image->bufs.write_bc.len / 4) - 1;
    int curr, cell = image->write_bc_ticks;
    struct write *write = NULL;

    /* Clear DMA peripheral interrupts. */
    dma1->ifcr = DMA_IFCR_CGIF(2);

    /* If we happen to be called in the wrong state, just bail. */
    if (dma_wr->state == DMA_inactive)
        return;

    /* Find out where the DMA engine's producer index has got to. */
    prod = ARRAY_SIZE(dma_wr->buf) - dma1->ch[2-1].cndtr;

    /* Check if we are processing the tail end of a write. */
    barrier(); /* interrogate peripheral /then/ check for write-end. */
    if (image->wr_bc != image->wr_prod) {
        write = get_write(image, image->wr_bc);
        prod = write->dma_end;
    }

    /* Process the flux timings into the raw bitcell buffer. */
    prev = dma_wr->prev_sample;
    bc_prod = image->bufs.write_bc.prod;
    bc_dat = image->write_bc_window;
    for (cons = dma_wr->cons; cons != prod; cons = (cons+1) & buf_mask) {
        next = dma_wr->buf[cons];
        curr = (int16_t)(next - prev) - (cell >> 1);
        if (unlikely(curr < 0)) {
            /* Runt flux, much shorter than bitcell clock. Merge it forward. */
            continue;
        }
        prev = next;
        while ((curr -= cell) > 0) {
            bc_dat <<= 1;
            bc_prod++;
            if (!(bc_prod&31))
                bc_buf[((bc_prod-1) / 32) & bc_bufmask] = htobe32(bc_dat);
        }
        curr += cell >> 1; /* remove the 1/2-cell bias */
        prev -= curr >> 2; /* de-jitter/precomp: carry 1/4 of phase error */
        bc_dat = (bc_dat << 1) | 1;
        bc_prod++;
        switch (sync) {
        case SYNC_fm:
            /* FM clock sync clock byte is 0xc7. Check for:
             * 1010 1010 1010 1010 1x1x 0x0x 0x1x 1x1x */
            if ((bc_dat & 0xffffd555) == 0x55555015)
                bc_prod = (bc_prod - 31) | 31;
            break;
        case SYNC_mfm:
            if (bc_dat == 0x44894489)
                bc_prod &= ~31;
            break;
        }
        if (!(bc_prod&31))
            bc_buf[((bc_prod-1) / 32) & bc_bufmask] = htobe32(bc_dat);
    }

    if (bc_prod & 31)
        bc_buf[(bc_prod / 32) & bc_bufmask] = htobe32(bc_dat << (-bc_prod&31));

    /* Processing the tail end of a write? */
    if (write != NULL) {
        /* Remember where this write's bitcell data ends. */
        write->bc_end = bc_prod;
        if (++image->wr_bc != image->wr_prod)
            IRQx_set_pending(DMA1_CH2_IRQ);
        /* Initialise decoder state for the start of the next write. */
        bc_prod = (bc_prod + 31) & ~31;
        bc_dat = ~0;
        prev = 0;
    }

    /* Save our progress for next time. */
    image->write_bc_window = bc_dat;
    image->bufs.write_bc.prod = bc_prod;
    dma_wr->cons = cons;
    dma_wr->prev_sample = prev;
}
