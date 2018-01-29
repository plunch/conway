#include "work_queue.h"

#include <stdlib.h>  /* malloc, free */
#include <pthread.h>


struct workq_entry {
	void *data;
	void (*work)(void*, int);
	struct workq_entry *next;
};

struct wq {
	struct {
		pthread_mutex_t mutex;
		pthread_cond_t work_available, queue_empty;
	} locks;
	struct {
		unsigned active, target, waiting;
		int destroy;
	} workers;
	unsigned waiting;
	struct workq_entry *entries;
	struct {
		unsigned length;
		pthread_t *items;
	} threads;
};


static void* worker(void *_a)
{
	struct wq *q = _a;

	pthread_mutex_lock(&q->locks.mutex);
	for (;;) {
		if (q->workers.destroy)
			break;

		int reached_target = q->workers.active >= q->workers.target;
		int work_available = !!q->entries;
		if (work_available && !reached_target) {
			struct workq_entry *entry = q->entries;
			q->entries = entry->next;

			q->workers.active++;
			pthread_mutex_unlock(&q->locks.mutex);

			entry->work(entry->data, 1);
			free(entry);

			pthread_mutex_lock(&q->locks.mutex);
			q->workers.active--;

			if (q->workers.destroy)
				break;

			int workers_active = q->workers.active > 0;
			work_available = !!q->entries;
			if (!work_available && !workers_active)
				pthread_cond_signal(&q->locks.queue_empty);
		} else {
			if (q->workers.destroy)
				break;
			q->workers.waiting++;
			pthread_cond_wait(&q->locks.work_available,
			                  &q->locks.mutex);
			q->workers.waiting--;
		}
	}

	if (!q->workers.destroy) {
		pthread_mutex_unlock(&q->locks.mutex);
		return NULL;
	}

	if (q->workers.active == 0 && q->workers.waiting == 0) {
		pthread_cond_destroy(&q->locks.work_available);

		struct workq_entry *entry = q->entries;
		q->entries = NULL;
		while(entry) {
			entry->work(entry->data, 0);

			void *e = entry;
			entry = entry->next;
			free(e);
		}

		pthread_cond_signal(&q->locks.queue_empty);
	} else {
		// Wake someone else
		pthread_cond_signal(&q->locks.work_available);
	}

	pthread_mutex_unlock(&q->locks.mutex);
	return NULL;
}

static void internal_stop(struct wq *q)
{
	q->waiting++;
	pthread_cond_signal(&q->locks.work_available);
	pthread_cond_wait(&q->locks.queue_empty, &q->locks.mutex);
	q->waiting--;

	int last_waiter = q->waiting == 0;
	pthread_mutex_unlock(&q->locks.mutex);

	if (q->workers.destroy && last_waiter) {
		for(unsigned i = 0; i < q->threads.length; ++i)
			pthread_join(q->threads.items[i], NULL);

		pthread_cond_destroy(&q->locks.queue_empty);
		pthread_mutex_destroy(&q->locks.mutex);

		free(q);
	}
}

int workq_create(struct workq *queue)
{
	struct wq *q = malloc(sizeof(struct wq));
	queue->opaque = q;
	if (!q)
		return 0;

	int err;
	err = pthread_mutex_init(&q->locks.mutex, NULL);
	if (err) {
		free(q);
		queue->opaque = NULL;
		return 0;
	}
	err = pthread_cond_init(&q->locks.work_available, NULL);
	if (err) {
		free(q);
		queue->opaque = NULL;
		return 0;
	}
	err = pthread_cond_init(&q->locks.queue_empty, NULL);
	if (err) {
		free(q);
		queue->opaque = NULL;
		return 0;
	}

	q->workers.active = 0;
	q->workers.target = 0;
	q->workers.waiting = 0;
	q->workers.destroy = 0;
	q->waiting = 0;
	q->entries = NULL;
	q->threads.length = 0;
	q->threads.items = NULL;

	return 1;
}

void workq_destroy(struct workq *queue)
{
	if (!queue) return;
	if (!queue->opaque) return;

	struct wq *q = queue->opaque;

	pthread_mutex_lock(&q->locks.mutex);
	q->workers.target = 0;
	q->workers.destroy = 1;

	internal_stop(q);
}


int workq_start(struct workq *queue, unsigned workers)
{
	if (!queue) return 0;
	if (!queue->opaque) return 0;

	struct wq *q = queue->opaque;

	pthread_mutex_lock(&q->locks.mutex);

	if (q->workers.destroy)
		goto running;

	if (q->threads.length > 0)
		 goto running;

	q->threads.items = malloc(sizeof(pthread_t) * workers);
	if (!q->threads.items)
		goto exit;
	q->threads.length = workers;

	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	for(unsigned i = 0; i < q->threads.length; ++i) {
		int err = pthread_create(q->threads.items+i,
		                         &attr, worker, queue->opaque);
		if (err) {
			// Start as many as possible.
			q->threads.length = i;
			if (!q->threads.length) {
				// Release if no thread was started
				free(q->threads.items);
				q->threads.items = NULL;
			}
			goto running;
		}
	}

	q->workers.target = workers;

running:
	pthread_mutex_unlock(&q->locks.mutex);
	return q->threads.length;
exit:
	pthread_mutex_unlock(&q->locks.mutex);
	return 0;
}


void workq_stop(struct workq *queue)
{
	if (!queue) return;
	if (!queue->opaque) return;

	struct wq *q = queue->opaque;
	pthread_mutex_lock(&q->locks.mutex);
	q->workers.target = 0;

	internal_stop(q);
}

void workq_wait(struct workq *queue)
{
	if (!queue) return;
	if (!queue->opaque) return;

	struct wq *q = queue->opaque;

	pthread_mutex_lock(&q->locks.mutex);
	internal_stop(q);
}

int workq_add(struct workq *queue,
              void *data,
              void (*work)(void*, int))
{
	if (!queue) return 0;
	if (!queue->opaque) return 0;

	struct wq *q = queue->opaque;
	pthread_mutex_lock(&q->locks.mutex);

	struct workq_entry **entry = &q->entries;
	while(*entry) {
		entry = &(*entry)->next;
	}
	*entry = malloc(sizeof(struct workq_entry));
	if (!*entry)
		return 0;
	(*entry)->data = data;
	(*entry)->work = work;
	(*entry)->next = NULL;

	pthread_cond_signal(&q->locks.work_available);

	pthread_mutex_unlock(&q->locks.mutex);
	return 1;
}
