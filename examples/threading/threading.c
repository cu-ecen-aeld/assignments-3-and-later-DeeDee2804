#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{

    // Wait, obtain mutex, wait, release mutex as described by thread_data structure
    struct thread_data* thread_func_args = (struct thread_data *) thread_param;

    // Sleep a little bit before obtain the mutex
    usleep(thread_func_args->obtain_wait_time * 1000);
    pthread_mutex_lock(thread_func_args->mutex);
    
    // Sleep a little bit more then release the mutex
    usleep(thread_func_args->release_wait_time * 1000);
    pthread_mutex_unlock(thread_func_args->mutex);

    thread_func_args->thread_complete_success = true;
    return (void *) thread_func_args;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    /**
     * Allocate memory for thread_data, setup mutex and wait arguments, pass thread_data to created thread
     * using threadfunc() as entry point.
     * Return true if successful.
     */
    
    struct thread_data *thread_func_data = (struct thread_data*) malloc(sizeof(struct thread_data));
    thread_func_data->mutex = mutex;
    thread_func_data->obtain_wait_time = wait_to_obtain_ms;
    thread_func_data->release_wait_time = wait_to_release_ms;
    pthread_create(thread, NULL, threadfunc, (void *) thread_func_data);
    return true;
}

