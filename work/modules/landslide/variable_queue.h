/**
 * @file variable_queue.h
 *
 * @brief Generalized queue module for data collection
 *
 * @author Mike Sullivan (mjsulliv)
 **/

#ifndef __LS_VQ_H
#define __LS_VQ_H

#include <stddef.h>
#include <stdlib.h>

#include "common.h"
#include "compiler.h"

/**
 * @def Q_NEW_HEAD(Q_HEAD_TYPE, Q_ELEM_TYPE) 
 *
 *  @brief Generates a new structure of type Q_HEAD_TYPE representing the head 
 *  of a queue of elements of type Q_ELEM_TYPE. 
 *  
 *  Usage: Q_NEW_HEAD(Q_HEAD_TYPE, Q_ELEM_TYPE); //create the type <br>
 *         Q_HEAD_TYPE headName; //instantiate a head of the given type
 *
 *  @param Q_HEAD_TYPE the type you wish the newly-generated structure to have.
 *         
 *  @param Q_ELEM_TYPE the type of elements stored in the queue.
 *         Q_ELEM_TYPE must be a structure.
 *  
 **/

#define Q_NEW_HEAD(Q_HEAD_TYPE, Q_ELEM_TYPE) \
	Q_HEAD_TYPE {             \
		Q_ELEM_TYPE *head;            \
		Q_ELEM_TYPE *tail;            \
		unsigned int count;                        \
	}

/**
 * @def Q_HEAD_INITIALIZER
 * @brief A static initializer for a queue's head
 */
#define Q_HEAD_INITIALIZER {NULL, NULL, 0}
#define Q_HEAD_INIT Q_HEAD_INITIALIZER

/**
 * @def Q_NEW_LINK(Q_ELEM_TYPE)
 *
 *  @brief Instantiates a link within a structure, allowing that structure to be 
 *         collected into a queue created with Q_NEW_HEAD. 
 *
 *  Usage: <br>
 *  typedef struct Q_ELEM_TYPE {<br>
 *  Q_NEW_LINK(Q_ELEM_TYPE) LINK_NAME; //instantiate the link <br>
 *  } Q_ELEM_TYPE; <br>
 *
 *  A structure can have more than one link defined within it, as long as they
 *  have different names. This allows the structure to be placed in more than
 *  one queue simultanteously.
 *
 *  @param Q_ELEM_TYPE the type of the structure containing the link
 **/
#define Q_NEW_LINK(Q_ELEM_TYPE)          \
	struct {                             \
		Q_ELEM_TYPE *next;        \
		Q_ELEM_TYPE *prev;        \
	}

/**
 * @def Q_INIT_HEAD(Q_HEAD)
 *
 *  @brief Initializes the head of a queue so that the queue head can be used
 *         properly.
 *  @param Q_HEAD Pointer to queue head to initialize
 **/
#define Q_INIT_HEAD(Q_HEAD) do {                \
		(Q_HEAD)->head = (Q_HEAD)->tail = NULL; \
		(Q_HEAD)->count = 0;                    \
	} while (0)

/**
 * @def Q_LINK_INITIALIZER
 * @brief A static initializer for a link
 */
#define Q_LINK_INITIALIZER {NULL, NULL}
#define Q_LINK_INIT Q_LINK_INITIALIZER

/**
 * @def Q_INIT_ELEM(Q_ELEM, LINK_NAME)
 *
 *  @brief Initializes the link named LINK_NAME in an instance of the structure  
 *         Q_ELEM. 
 *  
 *  Once initialized, the link can be used to organized elements in a queue.
 *  
 *  @param Q_ELEM Pointer to the structure instance containing the link
 *  @param LINK_NAME The name of the link to initialize
 **/
#define Q_INIT_ELEM(Q_ELEM, LINK_NAME) do { \
		(Q_ELEM)->LINK_NAME.next = NULL;    \
		(Q_ELEM)->LINK_NAME.prev = NULL;    \
	} while (0)

#define Q_IN_LIST(Q_HEAD, Q_ELEM, LINK_NAME) (                          \
		(((Q_ELEM)->LINK_NAME.prev &&                                   \
		  (Q_ELEM)->LINK_NAME.prev->LINK_NAME.next == (Q_ELEM)) ||      \
		 (!(Q_ELEM)->LINK_NAME.prev &&                                  \
		  (Q_HEAD)->head == (Q_ELEM))) &&                               \
		(((Q_ELEM)->LINK_NAME.next &&                                   \
		  (Q_ELEM)->LINK_NAME.next->LINK_NAME.prev == (Q_ELEM)) ||      \
		 (!(Q_ELEM)->LINK_NAME.next &&                                  \
		  (Q_HEAD)->tail == (Q_ELEM)))                                  \
		)

#define Q_ASSERT_CONSISTENT(Q_HEAD, Q_ELEM, LINK_NAME) \
	assert(Q_IN_LIST((Q_HEAD), (Q_ELEM), LINK_NAME))

/**
 * @def Q_INSERT_FRONT(Q_HEAD, Q_ELEM, LINK_NAME)
 *
 *  @brief Inserts the queue element pointed to by Q_ELEM at the front of the 
 *         queue headed by the structure Q_HEAD. 
 *  
 *  The link identified by LINK_NAME will be used to organize the element and
 *  record its location in the queue.
 *
 *  @param Q_HEAD Pointer to the head of the queue into which Q_ELEM will be 
 *         inserted
 *  @param Q_ELEM Pointer to the element to insert into the queue
 *  @param LINK_NAME Name of the link used to organize the queue
 *
 *  @return Void
 **/
#define Q_INSERT_FRONT(Q_HEAD, Q_ELEM, LINK_NAME) do {  \
		(Q_ELEM)->LINK_NAME.prev = NULL;                \
		(Q_ELEM)->LINK_NAME.next = (Q_HEAD)->head;      \
		if ((Q_HEAD)->head)                             \
			(Q_HEAD)->head->LINK_NAME.prev = (Q_ELEM);  \
		(Q_HEAD)->head = (Q_ELEM);                      \
		if (!(Q_HEAD)->tail)                            \
			(Q_HEAD)->tail = (Q_ELEM);                  \
		(Q_HEAD)->count++;                              \
		                                                \
		Q_ASSERT_CONSISTENT(Q_HEAD, Q_ELEM, LINK_NAME); \
	} while (0)
#define Q_INSERT_HEAD Q_INSERT_FRONT	

/**
 *  @def Q_INSERT_TAIL(Q_HEAD, Q_ELEM, LINK_NAME) 
 *  @brief Inserts the queue element pointed to by Q_ELEM at the end of the 
 *         queue headed by the structure pointed to by Q_HEAD. 
 *  
 *  The link identified by LINK_NAME will be used to organize the element and
 *  record its location in the queue.
 *
 *  @param Q_HEAD Pointer to the head of the queue into which Q_ELEM will be 
 *         inserted
 *  @param Q_ELEM Pointer to the element to insert into the queue
 *  @param LINK_NAME Name of the link used to organize the queue
 *
 *  @return Void
 **/
#define Q_INSERT_TAIL(Q_HEAD, Q_ELEM, LINK_NAME) do {       \
		(Q_ELEM)->LINK_NAME.prev = (Q_HEAD)->tail;      	\
		(Q_ELEM)->LINK_NAME.next = NULL;					\
		if (!(Q_HEAD)->head) {                              \
			(Q_HEAD)->head = (Q_HEAD)->tail = (Q_ELEM);     \
		} else {                                            \
			(Q_HEAD)->tail->LINK_NAME.next = (Q_ELEM);      \
			(Q_HEAD)->tail = (Q_ELEM);                      \
		}                                                   \
		(Q_HEAD)->count++;                                  \
		                                                    \
		Q_ASSERT_CONSISTENT(Q_HEAD, Q_ELEM, LINK_NAME);     \
	} while (0)

/**
 * @def Q_GET_SIZE(Q_HEAD)
 *
 * @brief Returns the number of elements in the queue.
 *
 * @param Q_HEAD Pointer to the head of the queue
 * @return The number of elements in the queue
 */
#define Q_GET_SIZE(Q_HEAD) (Q_HEAD)->count

/**
 * @def Q_GET_FRONT(Q_HEAD)
 *  
 *  @brief Returns a pointer to the first element in the queue, or NULL 
 *  (memory address 0) if the queue is empty.
 *
 *  @param Q_HEAD Pointer to the head of the queue
 *  @return Pointer to the first element in the queue, or NULL if the queue
 *          is empty
 **/
#define Q_GET_FRONT(Q_HEAD) (Q_HEAD)->head
#define Q_GET_HEAD Q_GET_FRONT

/**
 * @def Q_GET_TAIL(Q_HEAD)
 *
 *  @brief Returns a pointer to the last element in the queue, or NULL 
 *  (memory address 0) if the queue is empty.
 *
 *  @param Q_HEAD Pointer to the head of the queue
 *  @return Pointer to the last element in the queue, or NULL if the queue
 *          is empty
 **/
#define Q_GET_TAIL(Q_HEAD) (Q_HEAD)->tail

/**
 * @def Q_GET_NEXT(Q_ELEM, LINK_NAME)
 * 
 *  @brief Returns a pointer to the next element in the queue, as linked to by 
 *         the link specified with LINK_NAME. 
 *
 *  If Q_ELEM is not in a queue or is the last element in the queue, 
 *  Q_GET_NEXT should return NULL.
 *
 *  @param Q_ELEM Pointer to the queue element before the desired element
 *  @param LINK_NAME Name of the link organizing the queue
 *
 *  @return The element after Q_ELEM, or NULL if there is no next element
 **/
#define Q_GET_NEXT(Q_ELEM, LINK_NAME) (Q_ELEM)->LINK_NAME.next
 
/**
 * @def Q_GET_PREV(Q_ELEM, LINK_NAME)
 * 
 *  @brief Returns a pointer to the previous element in the queue, as linked to 
 *         by the link specified with LINK_NAME. 
 *
 *  If Q_ELEM is not in a queue or is the first element in the queue, 
 *  Q_GET_NEXT should return NULL.
 *
 *  @param Q_ELEM Pointer to the queue element after the desired element
 *  @param LINK_NAME Name of the link organizing the queue
 *
 *  @return The element before Q_ELEM, or NULL if there is no next element
 **/
#define Q_GET_PREV(Q_ELEM, LINK_NAME) (Q_ELEM)->LINK_NAME.prev

/**
 * @def Q_INSERT_AFTER(Q_HEAD, Q_INQ, Q_TOINSERT, LINK_NAME)
 *
 *  @brief Inserts the queue element Q_TOINSERT after the element Q_INQ
 *         in the queue.
 *
 *  Inserts an element into a queue after a given element. If the given
 *  element is the last element, Q_HEAD should be updated appropriately
 *  (so that Q_TOINSERT becomes the tail element)
 *
 *  @param Q_HEAD head of the queue into which Q_TOINSERT will be inserted
 *  @param Q_INQ  Element already in the queue
 *  @param Q_TOINSERT Element to insert into queue
 *  @param LINK_NAME  Name of link field used to organize the queue
 **/
#define Q_INSERT_AFTER(Q_HEAD,Q_INQ,Q_TOINSERT,LINK_NAME) do {    \
		Q_ASSERT_CONSISTENT(Q_HEAD, Q_INQ, LINK_NAME);            \
		                                                          \
		(Q_TOINSERT)->LINK_NAME.prev = (Q_INQ);                   \
		(Q_TOINSERT)->LINK_NAME.next = (Q_INQ)->LINK_NAME.next;   \
		if ((Q_INQ)->LINK_NAME.next)                              \
			(Q_INQ)->LINK_NAME.next->LINK_NAME.prev = Q_TOINSERT; \
		(Q_INQ)->LINK_NAME.next = (Q_TOINSERT);                   \
		if ((Q_INQ) == (Q_HEAD)->tail)                            \
			(Q_HEAD)->tail = (Q_TOINSERT);                        \
		(Q_HEAD)->count++;                                        \
		                                                          \
		Q_ASSERT_CONSISTENT(Q_HEAD, Q_TOINSERT, LINK_NAME);       \
	} while (0)

/**
 * @def Q_INSERT_BEFORE(Q_HEAD, Q_INQ, Q_TOINSERT, LINK_NAME)
 *
 *  @brief Inserts the queue element Q_TOINSERT before the element Q_INQ
 *         in the queue.
 *
 *  Inserts an element into a queue before a given element. If the given
 *  element is the first element, Q_HEAD should be updated appropriately
 *  (so that Q_TOINSERT becomes the front element)
 *
 *  @param Q_HEAD head of the queue into which Q_TOINSERT will be inserted
 *  @param Q_INQ  Element already in the queue
 *  @param Q_TOINSERT Element to insert into queue
 *  @param LINK_NAME  Name of link field used to organize the queue
 **/
#define Q_INSERT_BEFORE(Q_HEAD,Q_INQ,Q_TOINSERT,LINK_NAME) do {   \
		Q_ASSERT_CONSISTENT(Q_HEAD, Q_INQ, LINK_NAME);            \
		                                                          \
		(Q_TOINSERT)->LINK_NAME.next = (Q_INQ);                   \
		(Q_TOINSERT)->LINK_NAME.prev = (Q_INQ)->LINK_NAME.prev;   \
		if ((Q_INQ)->LINK_NAME.prev)                              \
			(Q_INQ)->LINK_NAME.prev->LINK_NAME.next = Q_TOINSERT; \
		(Q_INQ)->LINK_NAME.prev = (Q_TOINSERT);                   \
		if ((Q_INQ) == (Q_HEAD)->head)                            \
			(Q_HEAD)->head = (Q_TOINSERT);                        \
		(Q_HEAD)->count++;                                        \
		                                                          \
		Q_ASSERT_CONSISTENT(Q_HEAD, Q_TOINSERT, LINK_NAME);       \
	} while (0)

/**
 * @def Q_REMOVE(Q_HEAD,Q_ELEM,LINK_NAME)
 * 
 *  @brief Detaches the element Q_ELEM from the queue organized by LINK_NAME, 
 *         and returns a pointer to the element. 
 *
 *  If Q_HEAD does not use the link named LINK_NAME to organize its elements
 *  the behavior of this macro is undefined.
 *
 *  @param Q_HEAD Pointer to the head of the queue containing Q_ELEM. If 
 *         Q_REMOVE removes the first, last, or only element in the queue, 
 *         Q_HEAD should be updated appropriately.
 *  @param Q_ELEM Pointer to the element to remove from the queue headed by 
 *         Q_HEAD.
 *  @param LINK_NAME The name of the link used to organize Q_HEAD's queue
 * 
 *  @return Void
 **/
#define Q_REMOVE(Q_HEAD,Q_ELEM,LINK_NAME) do {                          \
		Q_ASSERT_CONSISTENT(Q_HEAD, Q_ELEM, LINK_NAME); \
		if ((Q_ELEM)->LINK_NAME.prev)                                   \
			(Q_ELEM)->LINK_NAME.prev->LINK_NAME.next=(Q_ELEM)->LINK_NAME.next; \
		if ((Q_ELEM)->LINK_NAME.next)                                   \
			(Q_ELEM)->LINK_NAME.next->LINK_NAME.prev=(Q_ELEM)->LINK_NAME.prev; \
		if ((Q_ELEM) == (Q_HEAD)->head)                                 \
			(Q_HEAD)->head = (Q_ELEM)->LINK_NAME.next;                  \
		if ((Q_ELEM) == (Q_HEAD)->tail)                                 \
			(Q_HEAD)->tail = (Q_ELEM)->LINK_NAME.prev;                  \
		(Q_HEAD)->count--;                                              \
		(Q_ELEM)->LINK_NAME.next = NULL;                                \
		(Q_ELEM)->LINK_NAME.prev = NULL;                                \
	} while (0)

/**
 * @def Q_CONCAT(Q_HEAD1,Q_HEAD2,LINK_NAME)
 * 
 * @brief Concatenates two lists together
 *  @param Q_HEAD1 The first list
 *  @param Q_HEAD2 The second list
 *  @param LINK_NAME The name of the link used to organize Q_HEAD's queue
 * 
 *  @return Void
 **/
#define Q_CONCAT(Q_HEAD1,Q_HEAD2,LINK_NAME) do {                        \
		if (!(Q_HEAD1)->head) {                                         \
			*(Q_HEAD1) = *(Q_HEAD2);									\
		} else if ((Q_HEAD2)->head) {									\
			(Q_HEAD1)->tail->LINK_NAME.next = (Q_HEAD2)->head;			\
			(Q_HEAD2)->head->LINK_NAME.prev = (Q_HEAD1)->tail;			\
			(Q_HEAD1)->tail = (Q_HEAD2)->tail;							\
			(Q_HEAD1)->count += (Q_HEAD2)->count;						\
		}																\
		(Q_HEAD2)->head = (Q_HEAD2)->tail = NULL;						\
		(Q_HEAD2)->count = 0;											\
	} while (0)


/**
 * @def Q_FOREACH(CURRENT_ELEM,Q_HEAD,LINK_NAME) 
 *
 *  @brief Constructs an iterator block (like a for block) that operates
 *         on each element in Q_HEAD, in order.
 *
 *  Q_FOREACH constructs the head of a block of code that will iterate through
 *  each element in the queue headed by Q_HEAD. Each time through the loop, 
 *  the variable named by CURRENT_ELEM will be set to point to a subsequent
 *  element in the queue.
 *
 *  Usage:<br>
 *  Q_FOREACH(CURRENT_ELEM,Q_HEAD,LINK_NAME)<br>
 *  {<br>
 *  ... operate on the variable CURRENT_ELEM ... <br>
 *  }
 *
 *  If LINK_NAME is not used to organize the queue headed by Q_HEAD, then
 *  the behavior of this macro is undefined.
 *
 *  @param CURRENT_ELEM name of the variable to use for iteration. On each
 *         loop through the Q_FOREACH block, CURRENT_ELEM will point to the
 *         current element in the queue. CURRENT_ELEM should be an already-
 *         defined variable name, and its type should be a pointer to 
 *         the type of data organized by Q_HEAD
 *  @param Q_HEAD Pointer to the head of the queue to iterate through
 *  @param LINK_NAME The name of the link used to organize the queue headed
 *         by Q_HEAD.
 **/
#define Q_FOREACH(CURRENT_ELEM,Q_HEAD,LINK_NAME)            \
	for ((CURRENT_ELEM) = (Q_HEAD)->head; (CURRENT_ELEM);   \
		 (CURRENT_ELEM) = (CURRENT_ELEM)->LINK_NAME.next)

/**
 * @def Q_SEARCH(OUTPUT,Q_HEAD,LINK_NAME,EXPR)
 *
 * @brief Searches each element in Q_HEAD for a node that satisfies
 * EXPR.
 *
 * @param OUTPUT name of the variable to use in the iteration. If a
 *        node that satisfies EXPR is found, iteration is stopped and OUTPUT
 *        is set as that value. If no node that satisfies EXPR is found,
 *        OUTPUT will be NULL. In EXPR, OUTPUT will point the the current
 *        node.
 *  @param Q_HEAD Pointer to the head of the queue to iterate through
 *  @param LINK_NAME The name of the link used to organize the queue headed
 *         by Q_HEAD.
 *  @param EXPR An expression that evaluates true when the wanted
 *         element is seen 
 **/
#define Q_SEARCH(OUTPUT,Q_HEAD,LINK_NAME,EXPR)      \
	do {                                            \
		Q_FOREACH((OUTPUT),(Q_HEAD),LINK_NAME) {    \
			if ((EXPR)) {                           \
				break;                              \
			}                                       \
		}                                           \
	} while (0)

#endif
