#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "double_list.h"

typedef struct node {
    void* data;
    struct node *next;
    struct node *last;
} node_t;

struct list {
    struct node *head;
    int data_size;
    int length;
    void* (*custom_alloc)(size_t, size_t);
    void (*custom_free)(void*);
};

static node_t* position_of_list(list_t list, int index);
static node_t* new_node(list_t list);
static void link_node_to_list(list_t list, node_t *node, node_t *next_node);
static int remove_node(list_t list, node_t *node);


static void* alloc_mem(list_t list, size_t num, size_t size) {
    if (list && list->custom_alloc) 
        return list->custom_alloc(num, size);
    else 
        return calloc(num, size);
}


static void free_mem(list_t list, void *ptr) {
    if (list && list->custom_free) 
        list->custom_free(ptr);
    else 
        free(ptr);
}


int init_list(list_t *list_ptr, int data_size, 
             void* (*alloc_ptr)(size_t, size_t), 
             void (*free_ptr)(void*)) {
    list_t new_list = (list_t)(alloc_ptr ? alloc_ptr(1, sizeof(struct list)) : 
                     calloc(1, sizeof(struct list)));
    *list_ptr = new_list;
    if (!new_list) return -1;
    
    node_t *node = (node_t*)(alloc_ptr ? alloc_ptr(1, sizeof(node_t)) : 
                     calloc(1, sizeof(node_t)));
    if (!node) {
        if (free_ptr) free_ptr(new_list);
        else free(new_list);
        *list_ptr = NULL;
        return -1;
    }
    
    node->data = NULL;
    node->next = node;
    node->last = node;
    
    new_list->head = node;
    new_list->data_size = data_size;
    new_list->length = 0;
    new_list->custom_alloc = alloc_ptr;
    new_list->custom_free = free_ptr;
    return 0;
}


iterator_t append_list(list_t list, void *data) {
    return insert_list(list, data, end_of_list(list));
}


static void link_node_to_list(list_t list, node_t *node, node_t *next_node) {
    node_t *last_node = next_node->last;
    last_node->next = node;
    node->next = next_node;
    next_node->last = node;
    node->last = last_node;
    list->length++;
}


static int remove_node(list_t list, node_t *node) {
    if (node == list->head) return -1;
    
    node_t *next_node = node->next;
    node_t *last_node = node->last;
    next_node->last = last_node;
    last_node->next = next_node;
    list->length--;
    return 0;
}


static node_t* position_of_list(list_t list, int index) {
    node_t *node = NULL;
    int i = 0;
    
    if (index <= list->length >> 1) {
        node = list->head->next;
        for (i = 0; i < index; i++) {
            node = node->next;
        }
    }
    else {
        node = list->head;
        for (i = list->length; i > index; i--) {
            node = node->last;
        }
    }
    return node;
}


static node_t* new_node(list_t list) {
    node_t *node = (node_t*)alloc_mem(list, 1, sizeof(node_t));
    if (!node) return NULL;
        
    void *data = alloc_mem(list, 1, list->data_size);
    if (!data) {
        free_mem(list, node);
        return NULL;
    }
    
    node->data = data;
    node->next = NULL;
    node->last = NULL;
    return node;
}


iterator_t insert_list(list_t list, void *data, iterator_t it_before) {
    node_t *node = new_node(list);
    if (!node) return list->head;
    
    memcpy(node->data, data, list->data_size);
    link_node_to_list(list, node, it_before);
    return node;
}


iterator_t move_from_A_to_B(list_t A, iterator_t it_a, list_t B, iterator_t it_b_before) {
    remove_node(A, it_a);
    link_node_to_list(B, it_a, it_b_before);
    return it_a;
}


int remove_iterator(list_t list, iterator_t it) {
    int n = remove_node(list, it);
    if (n != -1) {
        free_mem(list, it->data);
        it->data = NULL;
        free_mem(list, it);
    }
    return n;
}


int remove_list_first(list_t list) {
    return remove_iterator(list, begin_of_list(list));
}


int remove_list_last(list_t list) {
    return remove_iterator(list, end_of_list(list));
}


void* at_list(list_t list, int index) {
    node_t *node = position_of_list(list, index);
    return (node != list->head) ? node->data : NULL;
}


iterator_t find_list_first(iterator_t begin, iterator_t end, void *data, 
                         int (*condition)(const void*, const void*)) {
    while (begin != end) {
        if (condition(get_list_data(begin), data)) break;
        next_of_list(&begin);
    }
    return begin;
}


int index_of_list(list_t list, void *data, int (*equal)(const void*, const void*)) {
    node_t *node = list->head->next;
    int i = 0;
    
    while (node != list->head) {
        if (equal(data, node->data)) return i;
        i++;
        node = node->next;
    }
    return -1;
}


iterator_t get_list_min(iterator_t begin, iterator_t end, 
                      int (*less)(const void*, const void*)) {
    iterator_t min = begin;
    next_of_list(&begin);
    
    while (begin != end) {
        if (less(get_list_data(begin), get_list_data(min))) min = begin;
        next_of_list(&begin);
    }
    return min;
}


iterator_t get_list_max(iterator_t begin, iterator_t end, 
                      int (*large)(const void*, const void*)) {
    iterator_t max = begin;
    next_of_list(&begin);
    
    while (begin != end) {
        if (large(get_list_data(begin), get_list_data(max))) max = begin;
        next_of_list(&begin);
    }
    return max;
}


int get_list_data_size(list_t list) {
    return list->data_size;
}


int get_list_length(list_t list) {
    return list->length;
}


int is_list_empty(list_t list) {
    return !list->length;
}


void destroy_list(list_t *list_ptr) {
    if (!*list_ptr) return;
    
    list_t list = *list_ptr;
    node_t *node = list->head->next;
    node_t *end = list->head;
    node_t *tmp = NULL;
    
    while (node != end) {
        tmp = node->next;
        free_mem(list, node->data);
        free_mem(list, node);
        node = tmp;
    }
    
    free_mem(list, end);
    free_mem(list, list);
    *list_ptr = NULL;
}


iterator_t begin_of_list(list_t list) {
    return list->head->next;
}


iterator_t end_of_list(list_t list) {
    return list->head;
}


iterator_t next_of_list(iterator_t *it) {
    *it = (*it)->next;
    return *it;
}


iterator_t last_of_list(iterator_t *it) {
    *it = (*it)->last;
    return *it;
}


void* get_list_data(iterator_t it) {
    return it->data;
}


iterator_t get_list_next(iterator_t it) {
    return it->next;
}


iterator_t get_list_last(iterator_t it) {
    return it->last;
}

