/*
 * list.h
 * 
 * Doubly linked list.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

struct list_head {
    struct list_head *prev, *next;
};

static inline void list_init(struct list_head *head)
{
    head->prev = head;
    head->next = head;
}

static inline void list_insert_head(
    struct list_head *head, struct list_head *ent)
{
    ent->next = head->next;
    ent->prev = head;
    ent->next->prev = head->next = ent;
}

static inline void list_insert_tail(
    struct list_head *head, struct list_head *ent)
{
    ent->prev = head->prev;
    ent->next = head;
    ent->prev->next = head->prev = ent;
}

static inline void list_remove(struct list_head *ent)
{
    ent->next->prev = ent->prev;
    ent->prev->next = ent->next;
}

static inline bool_t list_is_empty(struct list_head *head)
{
    return head->next == head;
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
