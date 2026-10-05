#ifndef LIST_H
#define LIST_H

#include <stddef.h>
#include <stdlib.h>

typedef struct SpgListNode {
    void* data;
    struct SpgListNode* next;
    struct SpgListNode* prev;
} SpgListNode;

typedef struct SpgList {
    SpgListNode* head;
    SpgListNode* tail;
    size_t size;
} SpgList;

SpgList new_list() {
    SpgList list;
    list.head = NULL;
    list.tail = NULL;
    list.size = 0;
    return list;
}

void list_free(SpgList* list) {
    SpgListNode* current = list->head;
    while(current) {
        SpgListNode* next = current->next;
        free(current);
        current = next;
    }
    list->head = NULL;
    list->tail = NULL;
    list->size = 0;
}

void list_remove(SpgList* list, SpgListNode* node) {
    if(!node || list->size == 0) return;

    if(node->prev) {
        node->prev->next = node->next;
    } else {
        list->head = node->next; // Node is head
    }

    if(node->next) {
        node->next->prev = node->prev;
    } else {
        list->tail = node->prev; // Node is tail
    }

    free(node);
    list->size--;
}

void list_push(SpgList* list, void* data) {
    SpgListNode* node = (SpgListNode*)malloc(sizeof(SpgListNode));
    if(!node) return; // Handle memory allocation failure
    node->data = data;
    node->next = NULL;
    node->prev = list->tail;

    if(list->tail) {
        list->tail->next = node;
    } else {
        list->head = node; // First element
    }
    list->tail = node;
    list->size++;
}

void* list_pop_head(SpgList* list) {
    if(list->head == NULL) return NULL; // SpgList is empty

    SpgListNode* node = list->head;
    void* data = node->data;

    list->head = node->next;
    if(list->head) {
        list->head->prev = NULL;
    } else {
        list->tail = NULL; // SpgList is now empty
    }
    
    free(node);
    list->size--;
    return data;
}

#endif
