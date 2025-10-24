#include <stdlib.h>
#include <ttypt/idm.h>

void ids_push(ids_t *list, uint32_t id) {
    idsi_t *item = malloc(sizeof(idsi_t));
    item->value = id;
    SLIST_INSERT_HEAD(list, item, entry);
}

uint32_t ids_pop(ids_t *list) {
    idsi_t *popped = SLIST_FIRST(list);
    if (!popped)
        return IDM_MISS;
    uint32_t ret = popped->value;
    SLIST_REMOVE_HEAD(list, entry);
    free(popped);
    return ret;
}

void ids_drop(ids_t *list) {
    while (ids_pop(list) != IDM_MISS);
}

void ids_remove(ids_t *list, uint32_t id) {
    idsi_t *item;
    SLIST_FOREACH(item, list, entry) {
        if (item->value == id) {
            SLIST_REMOVE(list, item, ids_item, entry);
            free(item);
            return;
        }
    }
}

void ids_free(ids_t *list, uint32_t id) {
    ids_remove(list, id);
}

/* idm_* */

void idm_drop(idm_t *idm) {
    ids_drop(&idm->free);
}

uint32_t idm_push(idm_t *idm, uint32_t n) {
    uint32_t i;
    if (idm->last > n) {
        ids_free(&idm->free, n);
        return IDM_MISS;
    }
    for (i = idm->last; i < n; i++)
        ids_push(&idm->free, i);
    idm->last = n + 1;
    return n;
}

int idm_del(idm_t *idm, uint32_t id) {
    if (idm->last <= id)
        return 1;
    else if (id + 1 == idm->last) {
        idm->last--;
        return 1;
    } else {
        ids_push(&idm->free, id);
        return 0;
    }
}

uint32_t idm_new(idm_t *idm) {
    uint32_t ret = ids_pop(&idm->free);
    return (ret == IDM_MISS) ? idm->last++ : ret;
}
