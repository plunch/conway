struct workq {
	void *opaque; // That's all you get!
};

int workq_create(struct workq *queue);
void workq_destroy(struct workq *queue);

int workq_add(struct workq *queue,
              void *data,
              void (*work)(void*, int));

int workq_start(struct workq *queue, unsigned workers);
void workq_stop(struct workq *queue);
void workq_wait(struct workq *queue);
