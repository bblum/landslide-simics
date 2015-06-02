/* channel.h */
#include <pthread.h>

#ifndef __CHANNEL_H_
#define __CHANNEL_H_

typedef struct channel channel_t;
typedef struct provider_handle provider_handle_t;
typedef struct client_handle client_handle_t;

/************************ Creation and Destroying **************************/

/*
 * Input: None
 * Output: A channel starting with positive direction
 * Behaviour: Generates a new channel 
 */
channel_t* new_channel_positive();

/*
 * Input: None
 * Output: A channel starting with negative direction
 * Behaviour: Generates a new channel 
 */
channel_t* new_channel_negative();

/*
 * Input: A channel
 * Output: A provider handle
 * Behaviour: Generate a provider handle for the given channel
 */
provider_handle_t* provider_handle(channel_t* channel);

/*
 * Input: A channel
 * Output: A client handle
 * Behaviour: Generate a client handle for the given channel
 */
client_handle_t* client_handle(channel_t* channel);

/*
 * Input: A channel
 * Output: None
 * Behaviour: Frees up a channel
 */
void free_channel(channel_t *channel);

/*
 * Input: A provider handle
 * Output: None
 * Behaviour: Frees a provider handle
 */
void free_provider_handle(provider_handle_t* p);

/*
 * Input: A client handle
 * Output: None
 * Behaviour: Frees a client handle
 */
void free_client_handle(client_handle_t* p);

/******************************* Forwarding *********************************/

/*
 * Input: A provider and client handle
 * Output: 0 if successful and negative error code otherwise
 * Behaviour: Forwards the client to the provider, ending the calling process
 */
int forward(provider_handle_t* provider, client_handle_t* client);

/************************ Provider sends to Client **************************/

/*
 * Input: A label and provider handle
 * Output: 0 if successful and negative result otherwise
 * Behaviour: Sends the label from the provider 
 */
int provider_send_label(int label, provider_handle_t* provider); 

/*
 * Input: A int and provider handle
 * Output: 0 if successful and negative result otherwise
 * Behaviour: Sends the int from the provider 
 */
int provider_send_int(int n, provider_handle_t* provider); 

/*
 * Input: A pointer and provider handle
 * Output: 0 if successful and negative result otherwise
 * Behaviour: Sends the pointer from the provider 
 */
int provider_send_ptr(void* ptr, provider_handle_t* provider); 

/*
 * Input: A channel to send and provider to send from
 * Output: 0 if successful and negative result otherwise
 * Behaviour: Sends the channel from the provider 
 */
int provider_send_channel(client_handle_t* send, provider_handle_t* provider); 

/*
 * Input: A provider handle
 * Output: 0 if successful and negative error code otherwise
 * Behaviour: Sends the shift from the provider
 */
int provider_send_shift(provider_handle_t* provider);

/*
 * Input: A provider handle
 * Output: 0 if successful and negative error code otherwise
 * Behaviour: Sends the done msg from the provider
 */
int provider_send_done(provider_handle_t* provider);

/********************** Provider receives from Client *************************/

/*
 * Input: A provider handle
 * Output: A label
 * Behaviour: The provider receives a label 
 */
int provider_recv_label(provider_handle_t* provider); 

/*
 * Input: A provider handle
 * Output: An integer
 * Behaviour: The provider receives an integer 
 */
int provider_recv_int(provider_handle_t* provider); 

/*
 * Input: A provider handle
 * Output: A pointer
 * Behaviour: The provider receives a pointer 
 */
void* provider_recv_ptr(provider_handle_t* provider); 

/*
 * Input: A provider handle
 * Output: A channel pointer
 * Behaviour: The provider receives a channel 
 */
client_handle_t* provider_recv_channel(provider_handle_t* provider); 

/*
 * Input: A provider handle
 * Output: Nothing
 * Behaviour: The provider receives a shift
 */
void provider_recv_shift(provider_handle_t* provider);

/************************ Client sends to providers ***************************/

/*
 * Input: A label and client handle
 * Output: 0 if successful and negative result otherwise
 * Behaviour: Sends the label from the client 
 */
int client_send_label(int label, client_handle_t* client); 

/*
 * Input: A int and client handle
 * Output: 0 if successful and negative result otherwise
 * Behaviour: Sends the int from the client 
 */
int client_send_int(int n, client_handle_t* client); 

/*
 * Input: A pointer and client handle
 * Output: 0 if successful and negative result otherwise
 * Behaviour: Sends the pointer from the client 
 */
int client_send_ptr(void* ptr, client_handle_t* client); 

/*
 * Input: A channel to send and client to send from
 * Output: 0 if successful and negative result otherwise
 * Behaviour: Sends the channel from the client 
 */
int client_send_channel(client_handle_t* send, client_handle_t* client); 

/*
 * Input: A client handle
 * Output: 0 if successful and negative error code otherwise
 * Behaviour: Sends the shift from the client
 */
int client_send_shift(client_handle_t* client);

/*********************** Client receives from Provider ************************/

/*
 * Input: A client handle
 * Output: A label
 * Behaviour: The client receives a label 
 */
int client_recv_label(client_handle_t* client); 

/*
 * Input: A client handle
 * Output: An integer
 * Behaviour: The client receives an integer 
 */
int client_recv_int(client_handle_t* client); 

/*
 * Input: A client handle
 * Output: A pointer
 * Behaviour: The client receives a pointer 
 */
void* client_recv_ptr(client_handle_t* client); 

/*
 * Input: A client handle
 * Output: A channel pointer
 * Behaviour: The client receives a channel 
 */
client_handle_t* client_recv_channel(client_handle_t* client); 

/*
 * Input: A client handle
 * Output: Nothing
 * Behaviour: The client receives a shift
 */
void client_recv_shift(client_handle_t* client);

/*
 * Input: A client handle
 * Output: 0 if successful and negative error code otherwise
 * Behaviour: The client receives a DONE message 
 */
int client_recv_done(client_handle_t* client); 

#endif /* __CHANNEL_H_ */
