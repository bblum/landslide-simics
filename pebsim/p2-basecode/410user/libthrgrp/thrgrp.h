/* @file thrgrp.h
 * @brief This file defines the interface for the thread library wrapper. 
 * This wrapper allows explicit control over what exiting threads get joined 
 * on by what joining threads.
 * @author Matthew Brewer (mbrewer)
 */

#ifndef THRGRP_H
#define THRGRP_H
#include <cond.h>

/**
 * @brief This is an element of a thread group queue, used for storing
 * exited processes
 */
typedef struct thrgrp_queue_el{
  /** @brief the next element in the queue */
  struct thrgrp_queue_el *next;
  /** @brief the tid of the thread to exit */
  int tid;
} thrgrp_queue_el_t;

/**
 * @brief This is a structure holding information for joining and exiting
 * threads. The idea is that threads exiting on this exitgroup can be joined
 * on by threads joining on this exitgroup.
 */
typedef struct{
  /* @brief a condition variable holding joiners waiting on exiters*/
  cond_t cv;
  /* @brief a list of zombie threads waiting to be reaped */
  thrgrp_queue_el_t *zombie_in;
  thrgrp_queue_el_t *zombie_out;
  /* @brief a mutex for protecting the rest of this data*/
  mutex_t lock;
} thrgrp_group_t;

/**
 * @brief This is a temporary structure used for passing data into a spawned
 * thread
 */
typedef struct thrgrp_tmp_data{
  /** @brief function to call in newly spaned thread */
  void *(*func)(void *);
  /** @brief argument to func */
  void *arg;
  /** @brief thread group to spawn the new thread in */
  thrgrp_group_t *tg; 
} thrgrp_tmp_data_t;

/**
 * @brief This is a union for malloc purposes, so it can be used for
 * either a queue element, or temporary data, this way we only have to
 * malloc once per thread spawn 
 */
typedef union thrgrp_data{
  /** @brief a queue element */
  thrgrp_queue_el_t qel;
  /** @brief a temporary data structure */
  thrgrp_tmp_data_t tmp;
} thrgrp_data_t;


int thrgrp_init_group(thrgrp_group_t *eg);

int thrgrp_destroy_group(thrgrp_group_t* eg);

int thrgrp_create(thrgrp_group_t *tg, void *(*func)(void *), void *args);

int thrgrp_join(thrgrp_group_t *tg, void **status);





#endif /*WRAPTH_H*/
