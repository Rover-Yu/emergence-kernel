/* JAKernel - Simple doubly-linked list implementation (Linux-style) */

#ifndef _KERNEL_LIST_H
#define _KERNEL_LIST_H

#include <stdint.h>

/* List head structure */
struct list_head {
    struct list_head *next;
    struct list_head *prev;
};

/* Initialize a list head */
static inline void list_init(struct list_head *head) {
    head->next = head;
    head->prev = head;
}

/* Check if list is empty */
static inline int list_empty(struct list_head *head) {
    return head->next == head;
}

/* Add node to front of list */
static inline void list_push_front(struct list_head *head, struct list_head *node) {
    node->next = head->next;
    node->prev = head;
    head->next->prev = node;
    head->next = node;
}

/* Add node to back of list */
static inline void list_push_back(struct list_head *head, struct list_head *node) {
    node->next = head;
    node->prev = head->prev;
    head->prev->next = node;
    head->prev = node;
}

/* Remove node from list */
static inline void list_remove(struct list_head *node) {
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = node;
    node->prev = node;
}

/* Pop node from front of list (returns NULL if empty) */
static inline struct list_head *list_pop_front(struct list_head *head) {
    struct list_head *node;

    if (list_empty(head)) {
        return 0;
    }

    node = head->next;
    list_remove(node);
    return node;
}

/* Get container structure from list member pointer */
#define list_entry(ptr, type, member) \
    ((type *)((char *)(ptr) - (uint64_t)&((type *)0)->member))

/* Iterate over list */
#define list_for_each(pos, head) \
    for (pos = (head)->next; pos != (head); pos = pos->next)

/* Iterate over list safely (allows deletion) */
#define list_for_each_safe(pos, n, head) \
    for (pos = (head)->next, n = pos->next; pos != (head); pos = n, n = pos->next)

#endif /* _KERNEL_LIST_H */
