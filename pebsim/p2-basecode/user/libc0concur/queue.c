#include <queue.h>
#include <error_codes.h>
#include <assert.h>
#include <util.h>
#include <stdlib.h>

/* #include <errno.h> */
/* #include <string.h> */

//FIXME evntually the compiler can just know this
#define QUEUE_CAPACITY 1023

struct queue {
    int front;
    int back;
    int capacity;
    /* void* buf[]; */
    void** buf;
};

queue_t* new_queue()
{
    /* allocating enough memory for the array and the struct */

    /* queue_t* q = malloc(sizeof(queue_t) + (QUEUE_CAPACITY + 1) * sizeof(void*)); */
    queue_t* q = malloc(sizeof(queue_t));
    q->buf = malloc((QUEUE_CAPACITY + 1) * sizeof(void*));

    if (q == NULL) {
        /* int err = errno; */
        /* dbg(strerror(err)); */
        panic("Allocation failed in new_queue");
    }

    q->front = 0;
    q->back = 0;
    q->capacity = QUEUE_CAPACITY;

    return q;
}

void free_queue(queue_t *q)
{
    if (q == NULL) return;

    free(q->buf);
    free(q);
}

int queue_enqueue(void* e, queue_t* q)
{
	if (q == NULL) {
		panic("Enqueue called on NULL queue");
    }
	if (queue_size(q) == q->capacity)
		return -1;

	q->buf[q->back] = e;
	q->back = (q->back + 1) % (q->capacity + 1);

	return 0;
}

int queue_dequeue(queue_t* q, void** elem_loc)
{
	if (q == NULL) {
		panic("Dequeue called on NULL queue");
    }
	if (queue_size(q) == 0) {
		panic("Dequeue called on empty queue");
    }

    if (elem_loc != NULL)
        *elem_loc = q->buf[q->front];
    q->front = (q->front + 1) % (q->capacity + 1);

    return 0;
}

queue_t* queue_join(queue_t *q1, queue_t *q2)
{
    while (queue_size(q2) > 0) {
        void *elem;
        if (queue_dequeue(q2, &elem)) {
            panic("Could not dequeue from queue");
        }
        if (queue_enqueue(elem, q1)) {
            panic("Could not enqueue into queue");
        }
    }
    
    free_queue(q2); 
    return q1; 
}

int queue_size(queue_t* q)
{
	if (q == NULL) {
		panic("Size called on NULL queue");
    }

    int size = (q->back - q->front);
    if (size < 0)
    	return q->capacity + 1 + size;
    else
    	return size;
}

