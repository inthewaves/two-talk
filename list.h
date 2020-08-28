#ifndef _LIST_H_
#define _LIST_H_
#include <stdbool.h>
#include <stddef.h>

typedef struct Node_s Node;
struct Node_s {
    size_t indexOfAFreeNode;  // If in the right partition of pool, holds index of a free node.
    Node* pFront;
    Node* pBack;
    void* pItem;
};

enum CurrentPointerStatus {EMPTY_LIST, BEFORE_START, WITHIN_LIST, BEYOND_END};

typedef struct {
    size_t indexOfAFreeHead;  // If in the right partition of pool, holds index of a free head.
    int count; 
    enum CurrentPointerStatus currentPointerStatus;
    Node* pHead;
    Node* pTail;
    Node* pCurrent;
} List;

#define LIST_FAIL -1

#define LIST_MAX_NUM_HEADS 10

#define LIST_MAX_NUM_NODES 1000

List* List_create();

int List_count(List* pList);

void* List_first(List* pList);

void* List_last(List* pList); 

void* List_next(List* pList);

void* List_prev(List* pList);

// Returns a pointer to the current item in pList.
void* List_curr(List* pList);

// Adds after current item,
int List_add(List* pList, void* pItem);

// Inserts before current item
int List_insert(List* pList, void* pItem);

int List_append(List* pList, void* pItem);

int List_prepend(List* pList, void* pItem);

void* List_remove(List* pList);

void List_concat(List* pList1, List* pList2);

typedef void (*FREE_FN)(void* pItem);
void List_free(List* pList, FREE_FN pItemFreeFn);

void* List_trim(List* pList);

typedef bool (*COMPARATOR_FN)(void* pItem, void* pComparisonArg);
void* List_search(List* pList, COMPARATOR_FN pComparator, void* pComparisonArg);

#endif
