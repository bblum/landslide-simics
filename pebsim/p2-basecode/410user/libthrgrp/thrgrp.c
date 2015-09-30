/* @file thrgrp.c
 * @brief This file contains the implementation of the wrapper for the
 * thread library. This wrapper allows explicit control over what threads
 * get joined on by what other threads.
 * @author Matthew Brewer (mbrewer)
 *
 * The user allocates groups, and then initializes them.
 * They can then spawn threads into these groups with thrgrp_create()
 * When the threads exit (via returning.. NOT thr_exit()), they can be
 * joined on using thrgrp_join(). A call to thrgrp_join() with group tg
 * Will reap any one thread to exit from that group, and then return
 */

#include <cond.h>
#include <thrgrp.h>
#include <thread.h>
#include <stddef.h>
#include <syscall.h>
#include <stdlib.h>
#include <stdio.h>
#include <simics.h>
#include <assert.h>

/** 
 * @brief Initializes a thread group 
 * 
 * This function basically just initializes mutexes etc.
 *
 * @param eg An unitialized, but allocated, thread group to be initialized
 * @return 0 on success, nonzero otherwise
 *
 * @pre thr_init should have been called
 * @post eg is initialized
 */
int thrgrp_init_group(thrgrp_group_t *eg){
  int ret;
  eg->zombie_in=NULL;
  eg->zombie_out=NULL;
  if((ret=mutex_init(&(eg->lock))))
    return ret; 
  if((ret=cond_init(&(eg->cv)))) {
    mutex_destroy(&(eg->lock));
    return ret;
  }
  return 0;
}

/**
 * @brief Destroys an initialized the thrgroup 
 *
 * Destroys the mutex's and other related stuff
 *
 * @param eg An initialized thread group to be destroyed
 * @return 0 on success, nonzero otherwise
 *
 * @pre thrgrp_init_group(eg) has been called
 * @post eg is uninitialized (but still allocated)
 */
int thrgrp_destroy_group(thrgrp_group_t *eg){
  int ret=0;
  mutex_lock(&(eg->lock));
  /* make sure that the queues are empty */
  if(eg->zombie_in || eg->zombie_out)
    ret = 1;
  mutex_unlock(&(eg->lock));
  if(ret == 0) {
    mutex_destroy(&(eg->lock)); 
    cond_destroy(&(eg->cv)); 
  }
  return ret;
}

/**
 * @brief Function to sit on the bottom of the thread stack
 *
 * @param in_data is a thrgrp_data_t, with tmp loaded with the function
 * the user wants to run, the argument to call the function on, and
 * the thread group this new thread is to be spawned in
 * @return doesn't
 */
static void *thrgrp_bottom(void *in_data){
  thrgrp_data_t *data = (thrgrp_data_t *) in_data;
  void *(*func)(void *) = data->tmp.func;
  void *arg = data->tmp.arg;
  thrgrp_group_t *tg = data->tmp.tg;
  void * ret;

  /* now that we have all of the data out of data, 
    we'll use it as our queueing element */

  data->qel.next = NULL;
  data->qel.tid = thr_getid();

  /* runthe code we were asked to run */
  ret = func(arg);

  /* insert ourselves into the zombie queue */
  mutex_lock(&(tg->lock));
  if(tg->zombie_in)
    tg->zombie_in->next = &(data->qel);
  tg->zombie_in = &(data->qel);
  if(tg->zombie_out == NULL)
    tg->zombie_out = &(data->qel);

  /* signal a waiter to come clean up our zombie */
  cond_signal(&(tg->cv));
  mutex_unlock(&(tg->lock));

  /* if we return with ret, should exit with ret */
  return ret;
}

/** @brief spawns a new thread in the thread group
 *
 * @param tg, an initialized thread group to spawn the new thread in
 * @param func, function to run in the new thread
 * @param arg, argument to pass the function to func
 * @return 0 on success, nonzero otherwise
 */
int thrgrp_create(thrgrp_group_t *tg, void *(*func)(void *),void *arg){
  int tid;
  thrgrp_data_t * data;
  /* setup a hunk of data
    this is used both to get the arguments into the other thread,
    and then as the memory to go on the zombie queue, this way we only
    malloc once */
  data = malloc(sizeof(thrgrp_data_t));
  if(data == NULL)
    return -1;
  data->tmp.func = func;
  data->tmp.arg = arg;
  data->tmp.tg = tg;

  /* spawn the thread */
  tid = thr_create(thrgrp_bottom, data);

  /* tid<0 indicates error */
  if(tid < 0) {
    free(data);
    return tid;
  }
  
  /* we don't return the tid, because your not supposed to join on it 
    must be joined with thrgrp_join(), not thr_join() */
  return 0;
}


/** @brief joins on any thread which exits in the group
 * 
 * like thr_join(), but will join on any exited thread which was spawned into 
 * this thread group via thrgrp_create()
 * 
 * throughout this function thrgrp_queue_el_t is a LIE! in reality this is
 * thrgrp_data_t, a union including thrgrp_queue_el_t as a subtype. this is
 * only important because one should realize "free" is freeing thrgrp_data_t
 * of memory, not thrgrp_queue_el_t of memory
 *
 * @param eg, an initialized thread group to join on threads in
 * @param depared, a pointer to some memory, the TID of the exited
 * thread will be placed here before thrgrp_join returns
 * @param status, a pointer to a void *, where the return status of
 * the thread we join on (I.E. what's returned from that threads first function
 * @return returns the result of thr_join
 */
int thrgrp_join(thrgrp_group_t* eg, void **status){
  thrgrp_queue_el_t *thr_data;
  int tid;

  mutex_lock(&(eg->lock));
  /* check to see if there's someone to join on*/
  while(eg->zombie_out == NULL){
    /* wait until there is someone to join on */
	cond_wait(&(eg->cv), &(eg->lock));
    /* inside of cond_wait, someone can sneek in
     * I.E. even if condition variables are implemented in order 
     * we do not have guaranteed in order joins */
  }
  /* get our zombie from the queue */
  thr_data = eg->zombie_out;
  if(eg->zombie_out == eg->zombie_in){
    eg->zombie_in = NULL;
    eg->zombie_out = NULL;
  }else{
    eg->zombie_out = eg->zombie_out->next;
  }
  mutex_unlock(&(eg->lock));

  /* grab the tid out before we free it */
  tid = thr_data->tid;
  /* free the memory from the queue */
  free(thr_data);
  /* join on the tid, and return the result */
  return thr_join(tid, status);
}

