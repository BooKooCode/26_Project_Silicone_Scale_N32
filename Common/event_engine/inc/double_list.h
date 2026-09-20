#ifndef DOUBLE_LIST_H
#define DOUBLE_LIST_H

#include <stddef.h>

/** 
 * Generic circular doubly linked list implementation with a dummy head node. 
 * Node indices start from 0 (head node doesn't count as an element).
 * 
 * Definition of Iterator type (pointer to Node) and List type
 */
typedef struct node* iterator_t;
typedef struct list* list_t;

/**
 * Initialize a new linked list
 * 
 * @param list_ptr Pointer to receive the new List instance
 * @param data_size Size of data elements in bytes
 * @param alloc_ptr Custom memory allocation function (NULL for default calloc)
 * @param free_ptr Custom memory deallocation function (NULL for default free)
 * @return 0 on success, -1 on failure
 */
int init_list(list_t *list_ptr, int data_size, 
             void* (*alloc_ptr)(size_t, size_t), 
             void (*free_ptr)(void*));

/**
 * Append data to the end of the list
 * 
 * @param list Target list instance
 * @param data Pointer to data to append
 * @return Iterator to the newly inserted element
 */
iterator_t append_list(list_t list, void *data);

/**
 * Insert data before the specified iterator position
 * 
 * @param list Target list instance
 * @param data Pointer to data to insert
 * @param it_before Iterator before which to insert
 * @return Iterator to the newly inserted element
 */
iterator_t insert_list(list_t list, void *data, iterator_t it_before);

/**
 * Move a node from one list to another
 * 
 * @param A Source list
 * @param it_a Iterator to the node being moved
 * @param B Destination list
 * @param it_b_before Position before which to insert in destination list
 * @return Iterator to the moved node
 */
iterator_t move_from_A_to_B(list_t A, iterator_t it_a, list_t B, iterator_t it_b_before);

/**
 * Remove node at the specified iterator position
 * 
 * @param list Target list instance
 * @param it Iterator to the node to remove
 * @return 0 on success, -1 on failure
 */
int remove_iterator(list_t list, iterator_t it);

/**
 * Remove first element in the list (index 0)
 * 
 * @param list Target list instance
 * @return 0 on success, -1 on failure
 */
int remove_list_first(list_t list);

/**
 * Remove last element in the list
 * 
 * @param list Target list instance
 * @return 0 on success, -1 on failure
 */
int remove_list_last(list_t list);

/**
 * Access element at specified index
 * 
 * @param list Target list instance
 * @param index Element position (0-based)
 * @return Pointer to element data, or NULL if index is invalid
 */
void* at_list(list_t list, int index);

/**
 * Find first element meeting condition in a range
 * 
 * @param begin Start of search range (inclusive)
 * @param end End of search range (exclusive)
 * @param data Comparison data value
 * @param condition Comparison function (returns 1 for true, 0 otherwise)
 * @return Iterator to first matching element, or end if not found
 */
iterator_t find_list_first(iterator_t begin, iterator_t end, void *data, 
                         int (*condition)(const void*, const void*));

/**
 * Find index of first element matching data using equality function
 * 
 * @param list Target list instance
 * @param data Data to search for
 * @param equal Equality function (returns 1 if equal, 0 otherwise)
 * @return Index of matching element, or -1 if not found
 */
int index_of_list(list_t list, void *data, int (*equal)(const void*, const void*));

/**
 * Find minimum value in a range using comparison function
 * 
 * @param begin Start of search range (inclusive)
 * @param end End of search range (exclusive)
 * @param less Comparison function (returns 1 if first < second)
 * @return Iterator to the minimum element
 */
iterator_t get_list_min(iterator_t begin, iterator_t end, 
                      int (*less)(const void*, const void*));

/**
 * Find maximum value in a range using comparison function
 * 
 * @param begin Start of search range (inclusive)
 * @param end End of search range (exclusive)
 * @param large Comparison function (returns 1 if first > second)
 * @return Iterator to the maximum element
 */
iterator_t get_list_max(iterator_t begin, iterator_t end, 
                      int (*large)(const void*, const void*));

/**
 * Get size of data elements in the list
 * 
 * @param list Target list instance
 * @return Size of data elements in bytes
 */
int get_list_data_size(list_t list);

/**
 * Get current list length
 * 
 * @param list Target list instance
 * @return Number of elements in the list
 */
int get_list_length(list_t list);

/**
 * Check if list is empty
 * 
 * @param list Target list instance
 * @return 1 if empty, 0 otherwise
 */
int is_list_empty(list_t list);

/**
 * Destroy list and free all resources
 * 
 * @param list_ptr Pointer to List instance to destroy
 */
void destroy_list(list_t *list_ptr);

/**
 * Get iterator to first element
 * 
 * @param list Target list instance
 * @return Iterator to first element
 */
iterator_t begin_of_list(list_t list);

/**
 * Get iterator to end position (past last element)
 * 
 * @param list Target list instance
 * @return Iterator to end marker
 */
iterator_t end_of_list(list_t list);

/**
 * Advance iterator to next position
 * 
 * @param it Pointer to iterator to advance
 * @return Advanced iterator
 */
iterator_t next_of_list(iterator_t *it);

/**
 * Move iterator to previous position
 * 
 * @param it Pointer to iterator to move back
 * @return Previous iterator
 */
iterator_t last_of_list(iterator_t *it);

/**
 * Get data pointer from iterator
 * 
 * @param it Iterator pointing to a node
 * @return Pointer to element data
 */
void* get_list_data(iterator_t it);

/**
 * Get next iterator (does not modify input iterator)
 * 
 * @param it Current iterator
 * @return Iterator to next element
 */
iterator_t get_list_next(iterator_t it);

/**
 * Get previous iterator (does not modify input iterator)
 * 
 * @param it Current iterator
 * @return Iterator to previous element
 */
iterator_t get_list_last(iterator_t it);

#endif /* DOUBLE_LIST_H */