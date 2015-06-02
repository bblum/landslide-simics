#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <error_codes.h>
#include <channel.h>
#include <stdint.h>
#include <thread.h>
#include <util.h>
#include <queue.h>

//#include <sys/types.h>

/* turn on debugging */
/*
#undef  DEBUG_PRINT
#define DEBUG_PRINT 1
*/

typedef enum {
    TO_PROVIDER,
    TO_CLIENT
} channel_dir_e;

struct channel {
    channel_dir_e queue_dir;
    channel_dir_e provider_dir;
    channel_dir_e client_dir;

    queue_t* msgs;                  // TODO inline struct

    /* provider_handle_t* provider; */
    /* client_handle_t* client; */

    pthread_mutex_t m;
    pthread_cond_t c;
};

struct provider_handle {
    channel_t* chan;
};

struct client_handle {
    channel_t* chan;
};

typedef enum {
	LABEL, 
	INT, 
	PTR, 
	CHANNEL, 
	FORWARD,
        DONE, 
	SHIFT,
} msg_type_e;

typedef struct {   
    msg_type_e type;
    union {
        int label;
        int n;
        void* p;
        channel_t* forward;
        client_handle_t* handle;
    };
} channel_msg_t;


/* -------
 * helpers
 * -------
 */

#if DEBUG_PRINT

static void snprint_msg(char* buf, size_t size, channel_msg_t* msg) {
  switch (msg->type) {
  case LABEL:   snprintf(buf, size, "LABEL %d",   msg->label);   break;
  case INT:     snprintf(buf, size, "INT %d",     msg->n);       break;
  case PTR:     snprintf(buf, size, "PTR %p",     msg->p);       break;
  case CHANNEL: snprintf(buf, size, "HANDLE %p with CHANNEL %p",
  			 msg->handle, msg->handle->chan);  break;
  case FORWARD: snprintf(buf, size, "FORWARD %p", msg->forward); break;
  case SHIFT:   snprintf(buf, size, "SHIFT");                    break;
  case DONE:    snprintf(buf, size, "DONE");                     break;
  default: panic("printing invalid message type\n");        break;
  }
}

static inline const char* dir_string(channel_dir_e dir) {
    switch (dir) {
        case TO_CLIENT:   return "TO_CLIENT";
        case TO_PROVIDER: return "TO_PROVIDER";
        default:          panic("bad direction\n");
    }
}

#endif

/* ----------
 * new / free 
 * ----------
 */

static inline channel_t* new_channel(channel_dir_e dir)
{
    channel_t* chan = malloc(sizeof(channel_t));
    assert(chan);

    chan->msgs = new_queue();
    assert(chan->msgs);

    chan->queue_dir    = dir;
    chan->client_dir   = dir;
    chan->provider_dir = dir;

    pthread_mutex_init(&chan->m, NULL);
    pthread_cond_init(&chan->c, NULL);

    return chan;
}

channel_t* new_channel_positive() {
    return new_channel(TO_CLIENT);
}

channel_t* new_channel_negative() {
    return new_channel(TO_PROVIDER);
}

provider_handle_t* provider_handle(channel_t* channel) 
{
    provider_handle_t* provider = malloc(sizeof(provider_handle_t));
    assert(provider);
    provider->chan = channel;
    return provider;
}

client_handle_t* client_handle(channel_t* channel) 
{
    client_handle_t* client = malloc(sizeof(client_handle_t));
    assert(client);
    client->chan = channel;
    return client;
}

void free_channel(channel_t* chan)
{   
    if (!chan) return;

    pthread_mutex_destroy(&chan->m);
    pthread_cond_destroy(&chan->c);
    free_queue(chan->msgs);
    free(chan);
}

/* ---------------------
 * msg packing/unpacking
 * ---------------------
 */

channel_msg_t* pack_msg(msg_type_e type, void* msg_content)
{
    // TODO we can use the compiler type guarantees to avoid allocation and
    // have fixed message sizes (intptr_t)
    channel_msg_t* msg = malloc(sizeof(channel_msg_t));
    assert(msg != NULL);
    
    msg->type = type;
    switch (type) {
        case LABEL:   msg->label   = (int)(intptr_t)    msg_content; break;
        case INT:     msg->n       = (int)(intptr_t)    msg_content; break;
        case PTR:     msg->p       = (void*)            msg_content; break;
        case CHANNEL: msg->handle  = (client_handle_t*) msg_content; break;
        case FORWARD: msg->forward = (channel_t*)       msg_content; break;
        case SHIFT:
        case DONE:    break;
        default:
            panic("packing invalid message type\n");
    }
    return msg;
}

channel_t* unpack_msg(channel_msg_t* msg, msg_type_e expected_type,
		      void **ret_loc)
{
    assert(msg != NULL);

    /* forwards will not be expected, so return them */
    if (msg->type == FORWARD) {
        channel_t* chan = msg->forward;
        free(msg);
        return chan;
    } else {
        assert(expected_type == msg->type);
    }

    switch (expected_type) {
        case LABEL:   *(int*)              ret_loc = msg->label;  break;
        case INT:     *(int*)              ret_loc = msg->n;      break;
        case PTR:     *(void**)            ret_loc = msg->p;      break;
        case CHANNEL: *(client_handle_t**) ret_loc = msg->handle; break;
        case SHIFT:
        case DONE:    break;
        default:
            panic("packing invalid message type\n");
    }
    free(msg);      // Allocated by create_message
    return NULL;
}

/* ------------------------------------
 * Core functions (send, recv, forward)
 * ------------------------------------
 * the api could really be these 3 functions
 */

                                 /* sending */
static void _send_msg(channel_t* chan, msg_type_e type, void* msg_content)
{
    channel_msg_t* msg = pack_msg(type, msg_content);
    if (queue_enqueue((void*)msg, chan->msgs) != 0)
      panic("message queue capacity exceeded\n");

#if DEBUG_PRINT
    char buf[100];
    snprint_msg(buf, 100, msg);
    dbg("--> sending along %p: %s \n", chan, buf);
#endif
}


static int provider_send_msg(provider_handle_t* handle, msg_type_e type, 
                             void* msg_content) 
{
    channel_t* chan = handle->chan;
    pthread_mutex_lock(&chan->m);

    /* we are sending to the client */
    assert(chan->provider_dir == TO_CLIENT);
    _send_msg(chan, type, msg_content);

    if (type == SHIFT) 
        chan->provider_dir = TO_PROVIDER;

    chan->queue_dir = TO_CLIENT;
    pthread_cond_signal(&chan->c);

    pthread_mutex_unlock(&chan->m);
    return SUCCESS;
}

static int client_send_msg(client_handle_t* handle, msg_type_e type, 
                             void* msg_content) 
{
    channel_t* chan = handle->chan;
    pthread_mutex_lock(&chan->m);

    /* we are sending to the provider */
    assert(chan->client_dir == TO_PROVIDER);
    _send_msg(chan, type, msg_content);

    if (type == SHIFT) 
        chan->client_dir = TO_CLIENT;

    chan->queue_dir = TO_PROVIDER;
    pthread_cond_signal(&chan->c);

    pthread_mutex_unlock(&chan->m);
    return SUCCESS;
}

                                 /* recving */

static int provider_recv_msg(provider_handle_t* handle, 
                             msg_type_e expected_type, void** ret_loc) 
{
    channel_t* chan = handle->chan;
    pthread_mutex_lock(&chan->m);

    dbg("<-- recving along %p ....\n", chan);
    /* we are recving from the client, so assert our dir and wait on their's */
    assert(chan->provider_dir == TO_PROVIDER);
    while (chan->queue_dir != TO_PROVIDER || queue_size(chan->msgs) == 0) {
      pthread_cond_wait(&chan->c, &chan->m);
    }
    
    /* unpack the message, processing the forward if applicable */
    channel_msg_t* msg;
    assert(queue_dequeue(chan->msgs, (void**)&msg) == 0);

#if DEBUG_PRINT
    char buf[100];
    snprint_msg(buf, 100, msg);
    dbg("<-- recving along %p: %s \n", chan, buf);
#endif

    channel_t* forward = unpack_msg(msg, expected_type, ret_loc);

    if (forward) {
        assert(queue_size(chan->msgs) == 0);
        pthread_mutex_unlock(&chan->m);
        free_channel(chan);
        handle->chan = forward;
        return provider_recv_msg(handle, expected_type, ret_loc);
    }

    if (expected_type == SHIFT) {
        assert(queue_size(chan->msgs) == 0);
        chan->provider_dir = TO_CLIENT;
    }

    pthread_mutex_unlock(&chan->m);
    return SUCCESS;
}

static int client_recv_msg(client_handle_t* handle, 
                           msg_type_e expected_type, void** ret_loc) 
{
    channel_t* chan = handle->chan;
    pthread_mutex_lock(&chan->m);

    dbg("<-- recving along %p ....\n", chan);
    /* we are recving from the provider,
     * so assert our dir and wait on theirs */
    assert(chan->client_dir == TO_CLIENT);
    while (chan->queue_dir != TO_CLIENT || queue_size(chan->msgs) == 0) {
      pthread_cond_wait(&chan->c, &chan->m);
    }
    
    /* unpack the message, processing the forward if applicable */
    channel_msg_t* msg;
    assert(queue_dequeue(chan->msgs, (void**)&msg) == 0);

#if DEBUG_PRINT
    char buf[100];
    snprint_msg(buf, 100, msg);
    dbg("<-- c recving along %p: %s \n", chan, buf);
#endif

    channel_t* forward = unpack_msg(msg, expected_type, ret_loc);

    if (forward) {
        assert(queue_size(chan->msgs) == 0);
        pthread_mutex_unlock(&chan->m);
        free_channel(chan);
        handle->chan = forward;
        return client_recv_msg(handle, expected_type, ret_loc);
    }

    if (expected_type == SHIFT) {
        assert(queue_size(chan->msgs) == 0);
        chan->client_dir = TO_PROVIDER;
    }

    pthread_mutex_unlock(&chan->m);
    return SUCCESS;
}

                                /* forwarding */

int forward(provider_handle_t* p_handle, client_handle_t* c_handle) 
{

    /* provider       chan<-c_handle     p_handle->chan       client
     *   P ----------- c1 ----------- Q ----------- c2 --------- R
     *          c'             c             d             d'         
     */ 

    channel_t* c1 = c_handle->chan; /* provider side channel */
    channel_t* c2 = p_handle->chan; /* client side channel */

    /* lock both channels */
    pthread_mutex_lock(&c1->m); 
    pthread_mutex_lock(&c2->m);

    /* directions on c and d must be the same */
    assert(c1->client_dir == c2->provider_dir);
    channel_dir_e forward_dir = c1->client_dir;

    /* send it */
    switch (forward_dir) {
        case TO_PROVIDER: 
            pthread_mutex_unlock(&c2->m);
            _send_msg(c1, FORWARD, c2);
            c1->queue_dir = TO_PROVIDER;
            pthread_cond_signal(&c1->c);
            pthread_mutex_unlock(&c1->m); 
            break;
        case TO_CLIENT:   
            pthread_mutex_unlock(&c1->m); 
            _send_msg(c2, FORWARD, c1);
            c2->queue_dir = TO_CLIENT;
            pthread_cond_signal(&c2->c);
            pthread_mutex_unlock(&c2->m);
            break;
        default: 
            panic("bad direction\n");
    }

    /* both handles are owned by the current process (executing forward) */
    free(p_handle);
    free(c_handle);
    pthread_exit(0);
    while (1);
}

/* --------------------------------------------------------------- */
/* NOTE: everything below here is just a thin shim on the core api */
/* --------------------------------------------------------------- */

/************************ Provider sends to Client **************************/

int provider_send_label(int label, provider_handle_t* provider){
    return provider_send_msg(provider, LABEL, (void*) (intptr_t) label);
}

int provider_send_int(int n, provider_handle_t* provider){
    return provider_send_msg(provider, INT, (void*) (intptr_t) n);
}

int provider_send_ptr(void* p, provider_handle_t* provider){
    return provider_send_msg(provider, PTR, p);
}

int provider_send_channel(client_handle_t* send, provider_handle_t* provider){
    return provider_send_msg(provider, CHANNEL, (void*) send);
}

int provider_send_shift(provider_handle_t* provider) {
    return provider_send_msg(provider, SHIFT, NULL);
}

int provider_send_done(provider_handle_t* provider)
{
    if (provider_send_msg(provider, DONE, NULL)) {
        panic("Provider could not send done\n");
    }
    // The provider can now safely exit.
    free(provider);  /* recipient will free actual channel */
    pthread_exit(0);
    while (1);
}

/********************* Provider receives from Client ************************/

int provider_recv_label(provider_handle_t* provider){
    int label;
    provider_recv_msg(provider, LABEL, (void**) &label);
    return label;
}

int provider_recv_int(provider_handle_t* provider){
    int n;
    provider_recv_msg(provider, INT, (void**) &n);
    return n;
}

void* provider_recv_ptr(provider_handle_t* provider){
    void* ptr;
    provider_recv_msg(provider, PTR, (void**) &ptr);
    return ptr;
}

client_handle_t* provider_recv_channel(provider_handle_t* provider){
    client_handle_t* recv;
    provider_recv_msg(provider, CHANNEL, (void**) &recv);
    return recv;
}

void provider_recv_shift(provider_handle_t* provider){
    provider_recv_msg(provider, SHIFT, NULL);
}

/************************ Client sends to Providers ***************************/

int client_send_label(int label, client_handle_t* client){
    return client_send_msg(client, LABEL, (void*) (intptr_t) label);
}

int client_send_int(int n, client_handle_t* client){
    return client_send_msg(client, INT, (void*) (intptr_t) n);
}

int client_send_ptr(void* p, client_handle_t* client){
    return client_send_msg(client, PTR, p);
}

int client_send_channel(client_handle_t* send, client_handle_t* client){
    return client_send_msg(client, CHANNEL, (void*) send);
}

int client_send_shift(client_handle_t* client) {
    return client_send_msg(client, SHIFT, NULL);
}

/*********************** Client receives from Provider ************************/

int client_recv_label(client_handle_t* client){
    int label;
    client_recv_msg(client, LABEL, (void**) &label);
    return label;
}

int client_recv_int(client_handle_t* client){
    int n;
    client_recv_msg(client, INT, (void**) &n);
    return n;
}

void* client_recv_ptr(client_handle_t* client){
    void* ptr;
    client_recv_msg(client, PTR, (void**) &ptr);
    return ptr;
}

client_handle_t* client_recv_channel(client_handle_t* client){
    client_handle_t* recv;
    client_recv_msg(client, CHANNEL, (void**) &recv);
    return recv;
}

void client_recv_shift(client_handle_t* client){
    client_recv_msg(client, SHIFT, NULL);
}

int client_recv_done(client_handle_t* client) {
    int ret = client_recv_msg(client, DONE, NULL);
    free_channel(client->chan);
    free(client);
    return ret;
}
