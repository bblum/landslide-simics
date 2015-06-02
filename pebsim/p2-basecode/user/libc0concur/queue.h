#ifndef __QUEUE_H_
#define __QUEUE_H_

typedef struct queue queue_t;

/* Input: None
   Output: A pointer to a queue 
 */
queue_t *new_queue();

/* Input: A valid queue created by a previous call to new_queue
   Output: None
 */
void free_queue(queue_t *queue);

/* Input: An element to insert into the queue
          A queue to insert element into
   Output: 0 if successful and negative error code otherwise
 */
int queue_enqueue(void *elem, queue_t *queue);

/* Input: A queue to dequeue from 
          Location to write dequeue element to element to 
   Output: 0 if successful and negative error code otherwise
   Behavior:
 */
int queue_dequeue(queue_t *queue, void **elem);

/* Input: A queue to dequeue from 
   Output: size of queue if successful and negative error code otherwise
 */
int queue_size(queue_t *queue);

/* Input: Two queues to join together in the order of q1 then q2
   Output: Pointer to joined queue
*/
queue_t *queue_join(queue_t *q1, queue_t *q2);

#endif /* __QUEUE_H_ */
