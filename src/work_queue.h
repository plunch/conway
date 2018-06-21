struct workq {
	void *opaque; // That's all you get!
};

/*
 * Creates and initializes a workq 
 * Returns non-zero on success
 */
int workq_create(struct workq *queue);
/*
 * Stops all workers and deallocates the workq.
 * Any queued work not yet started will be deallocated
 * using the provided function.
 */
void workq_destroy(struct workq *queue);

/*
 * Adds an operation to be executed on a worker thread.
 *
 * Parameters:
 * - data: Pointer passed to the work method
 * - work: Pointer to a function to either execute the
 *         work, or deallocate any resources. The second
 *         integer parameter will be non-zero if work is
 *         to be executed, and zero if the queue is stopping
 *         and no work, apart from deallocation, should be done.
 *
 * Returns non-zero on success.
 */
int workq_add(struct workq *queue,
              void *data,
              void (*work)(void*, int));

/*
 * Starts the workq using with the provided number of
 * worker threads.
 * If the workq is already running, this function does
 * nothing.
 *
 * Returns the number of worker threads started.
 * Returns zero on failure.
 */
int workq_start(struct workq *queue, unsigned workers);
/*
 * Waits for any running operations and stops the queue.
 */
void workq_stop(struct workq *queue);
/*
 * Waits ofr all queued operations to finish.
 */
void workq_wait(struct workq *queue);
