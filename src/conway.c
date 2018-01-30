#include "conway.h"

#include "work_queue.h"

#include <stdlib.h> /* malloc, realloc, free */
#include <assert.h> /* assert */
#include <string.h> /* memset */
#include <pthread.h>

int conway_create(struct conway *cw, struct quad *root)
{
	if (!cw) return 0;
	if (!root) return 0;

	pthread_mutex_t *mut = malloc(sizeof(pthread_mutex_t));
	if (!mut)
		return 0;

	int err = pthread_mutex_init(mut, NULL);
	if (err) {
		free(mut);
		return 0;
	}

	cw->root = root;
	cw->changes.length = 0;
	cw->changes.capacity = 0;
	cw->changes.items = 0;
	cw->changes.opaque = mut;
	cw->opaque = NULL;
	cw->generation = 0;

	return 1;
}

void conway_destroy(struct conway *cw)
{
	if (!cw) return;

	pthread_mutex_t *mut = cw->changes.opaque;
	if (mut) {
		pthread_mutex_destroy(mut);
		free(mut);
	}

	if (cw->changes.capacity > 0) {
		free(cw->changes.items);
		cw->changes.items = NULL;
		cw->changes.length = 0;
		cw->changes.capacity = 0;
	}
}

static int append(struct state_change_buffer *buf,
                  coordinate x, coordinate y, value v)
{
	assert(buf);
	assert(buf->opaque);

	pthread_mutex_t *mut = buf->opaque;

	pthread_mutex_lock(mut);

	if (buf->length == buf->capacity) {
		unsigned new_cap = buf->capacity * 2;
		if (new_cap == 0)
			new_cap = 8;
		void *tmp = realloc(buf->items,
		                    sizeof(struct state_change) * new_cap);
		if (!tmp) {
			pthread_mutex_unlock(mut);
			return 0;
		}
		buf->items = tmp;
		buf->capacity = new_cap;
	}

	unsigned i = buf->length++;
	buf->items[i].x = x;
	buf->items[i].y = y;
	buf->items[i].v = v;

	pthread_mutex_unlock(mut);
	return 1;
}

// {{{1 is_in_{bucket,quad}

static int is_in_bucket(struct bucket *bucket, coordinate x, coordinate y)
{
	return bucket->x == x / BUCKETSZ
	    && bucket->y == y / BUCKETSZ;
}

static int is_in_quad(struct quad *quad, coordinate x, coordinate y)
{
	coordinate qx = x / BUCKETSZ;
	coordinate qy = y / BUCKETSZ;
	return quad->west  <= qx && quad->east  > qx
	    && quad->north <= qy && quad->south > qy;
}

// 1}}}

// {{{1 find_{bucket,quad}

static struct quad* find_quad(struct quad *quad, coordinate x, coordinate y)
{
	if (!quad)
		return NULL;

	if (is_in_quad(quad, x, y)) {
		if (quad->leaf) {
			return quad;
		} else {
			for(unsigned i = 0; i < 4; ++i) {
				if (is_in_quad(quad->children[i], x, y))
					return find_quad(quad->children[i], x, y);
			}
			assert(0);
			return NULL;
		}
	} else {
		if (!quad->parent)
			return NULL;
		else
			return find_quad(quad->parent, x, y);
	}
}

static struct bucket* find_bucket(struct quad *quad, coordinate x, coordinate y,
                                  struct quad **leaf_quad)
{
	assert(quad);

	struct quad *leaf = find_quad(quad, x, y);

	if (leaf_quad)
		*leaf_quad = leaf;

	if (!leaf || leaf->count == 0)
		return NULL;

	struct bucket *cur = leaf->items.head;

	while(cur) {
		if (is_in_bucket(cur, x, y))
			return cur;

		cur = cur->next;
	}
	return NULL;
}

// 1}}}

// {{{1 split_quad

static int split_quad(struct quad *quad)
{
	assert(quad);
	assert(quad->leaf);

	struct quad *children = malloc(sizeof(struct quad) * 4);
	if (!children)
		return 0;

	struct bucket *cur = quad->items.head;
	quad->leaf = 0;
	quad->items.head = NULL;
	quad->items.tail = NULL;

	for(unsigned i = 0; i < 4; ++i) {
		quad->children[i] = children + i;

		children[i].parent = quad;
		children[i].leaf = 1;
		children[i].count = 0;
		children[i].items.head = NULL;
		children[i].items.tail = NULL;
	}


	coordinate half = (quad->east - quad->west) / 2;

	coordinate hcenter = quad->east - half;
	coordinate vcenter = quad->south - half;

	quad->child_to.nw->west = quad->west;
	quad->child_to.nw->east = hcenter;
	quad->child_to.nw->north = quad->north;
	quad->child_to.nw->south = vcenter;

	quad->child_to.ne->west = hcenter;
	quad->child_to.ne->east = quad->east;
	quad->child_to.ne->north = quad->north;
	quad->child_to.ne->south = vcenter;

	quad->child_to.sw->west = quad->west;
	quad->child_to.sw->east = hcenter;
	quad->child_to.sw->north = vcenter;
	quad->child_to.sw->south = quad->south;

	quad->child_to.se->west = hcenter;
	quad->child_to.se->east = quad->east;
	quad->child_to.se->north = vcenter;
	quad->child_to.se->south = quad->south;

	while(cur) {
		struct bucket *add = cur;
		cur = cur->next;


		struct quad *child = NULL;
		unsigned c = 0;
		for(unsigned i = 0; i < 4; ++i) {
			if (is_in_quad(quad->children[i], add->x * BUCKETSZ, add->y * BUCKETSZ)) {
				child = quad->children[i];
				c++;
			}
		}
		assert(child);
		assert(c == 1);

		if (child->count++) {
			child->items.tail->next = add;
			add->next = NULL;
			add->prev = child->items.tail;
			child->items.tail = add;
		} else {
			child->items.head = add;
			child->items.tail = add;
			add->next = NULL;
			add->prev = NULL;
		}
	}

	return 1;
}

// 1}}}

// {{{1 new_bucket

static struct bucket* new_bucket(struct quad *quad, coordinate x, coordinate y,
                                 struct quad **leaf_quad)
{
	assert(quad);

	struct quad *leaf = find_quad(quad, x, y);

	if (leaf->count >= QUADSZ) {
		if (!split_quad(leaf)) {
			// malloc err
			return NULL; // TODO: Better handling?
		}

		leaf = find_quad(leaf, x, y);
	}
	if (leaf_quad)
		*leaf_quad =  leaf;

	struct bucket *new = malloc(sizeof(struct bucket));
	if (!new)
		return NULL;
	new->x = x / BUCKETSZ;
	new->y = y / BUCKETSZ;
	assert(is_in_quad(leaf, new->x * BUCKETSZ, new->y * BUCKETSZ));

	memset(new->bucket, 0, BUCKETSZ * BUCKETSZ / VALUE_BIT * sizeof(value));

	new->next = NULL;

	struct quad *cur = leaf;
	while(cur) {
		cur->count++;
		cur = cur->parent;
	}

	if (!leaf->items.tail) {
		leaf->items.tail = new;
		leaf->items.head = new;
		new->prev = NULL;
		new->num = 0;
	} else {
		leaf->items.tail->next = new;
		new->prev = leaf->items.tail;
		leaf->items.tail = new;
		new->num = new->prev->num+1;
	}

	return new;
}

// 1}}}

// {{{1 get/set

static value index_bucket(struct bucket *bucket,
                          coordinate ix, coordinate iy)
{
	coordinate i = ix + iy * BUCKETSZ;
	return (bucket->bucket[i / VALUE_BIT] >> (i % VALUE_BIT)) & 1;
}

value get(struct quad *quad, coordinate x, coordinate y)
{
	assert(quad);

	struct bucket *current = find_bucket(quad, x, y, NULL);

	if (!current)
		return 0;

	coordinate ix = x - current->x * BUCKETSZ;
	coordinate iy = y - current->y * BUCKETSZ;
	return index_bucket(current, ix, iy);
}

void set(struct quad *quad,
                coordinate x, coordinate y, value v)
{
	assert(quad);

	struct quad *leaf = NULL;
	struct bucket *current = find_bucket(quad, x, y, &leaf);

	if (!current) {
		assert(leaf);
		if (v)
			current = new_bucket(leaf, x, y, &leaf);
		else
			return;

		if (!current) // TODO: Do better?
			return;
	}

	coordinate ix = x - current->x * BUCKETSZ;
	coordinate iy = y - current->y * BUCKETSZ;
	coordinate i = ix + iy * BUCKETSZ;
	if (v)
		current->bucket[i / VALUE_BIT] |= (1 << (i % VALUE_BIT));
	else
		current->bucket[i / VALUE_BIT] &= ~(1 << (i % VALUE_BIT));

	// "Garbage collection": Check if a bucket contains only dead cells.
	if (v) return;

	for(unsigned j = 0; j < BUCKETSZ * BUCKETSZ / VALUE_BIT; ++j) {
		if (current->bucket[j])
			return;
	}

	if (current->next) {
		current->next->prev = current->prev;
	} else {
		leaf->items.tail = current->prev;
		if (leaf->items.tail)
			leaf->items.tail->next = NULL;
	}
	if (current->prev) {
		current->prev->next = current->next;
	} else {
		leaf->items.head = current->next;
		if (leaf->items.head)
			leaf->items.head->prev = NULL;
	}

	free(current);

	while(leaf) {
		leaf->count--;
		leaf = leaf->parent;
	}
}

// 1}}}

/* {{{1 update */

void update(struct conway *cw)
{
	cw->generation++;
	for(unsigned i = 0; i < cw->changes.length; ++i) {
		struct state_change c = cw->changes.items[i];
		set(cw->root, c.x, c.y, c.v);
	}
}

union bucket_neighbours
{
	struct {
		struct bucket *w,  *e,  *n,  *s,
		              *nw, *ne, *sw, *se;
	};
	struct bucket *items[8];
};

union neighbor_coordinates { 
	struct { coordinate x, y; } items[8];
	struct {
		struct { coordinate x, y; } w;
		struct { coordinate x, y; } e;
		struct { coordinate x, y; } n;
		struct { coordinate x, y; } s;
		struct { coordinate x, y; } nw;
		struct { coordinate x, y; } ne;
		struct { coordinate x, y; } sw;
		struct { coordinate x, y; } se;
	};
};

static void cell(struct state_change_buffer* changes,
                 unsigned n,
                 coordinate x, coordinate y, value v)
{
	if (v) {
		switch(n) {
		 case 0:
		 case 1: // Starvation
			append(changes, x, y, 0);
			return;
		 case 2:
		 case 3: // No change
		 	return;
		default: // Overpopulation
			append(changes, x, y, 0);
			return;
		}
	} else if (n == 3) {
		append(changes, x, y, 1);
	} 
}

enum bucket_edge {
	EDGE_NORTH = (1<<0),
	EDGE_SOUTH = (1<<1),
	EDGE_EAST  = (1<<2),
	EDGE_WEST  = (1<<3),
};

static void corner_step(struct state_change_buffer *changes,
                        union bucket_neighbours *neighbour,
                        struct bucket *bucket, coordinate xp, coordinate yp,
                        enum bucket_edge edge)
{
	union bucket_neighbours n = *neighbour;

	coordinate x, y;

	coordinate max = BUCKETSZ-1;

	switch(edge) {
	case EDGE_NORTH|EDGE_WEST: // NE
		x = 0; y = 0;
		break;
	case EDGE_NORTH|EDGE_EAST: // NW
		x = max; y = 0;
		break;
	case EDGE_SOUTH|EDGE_WEST: // SW
		x = 0; y = max;
		break;
	case EDGE_SOUTH|EDGE_EAST: // SE
		x = max; y = max;
		break;
	}

	struct { int x, y; } delta[8] = {
		{ -1,  0 }, // w
		{ +1,  0 }, // e
		{  0, -1 }, // n
		{  0, +1 }, // s
		{ -1, -1 }, // nw
		{ +1, -1 }, // ne
		{ -1, +1 }, // sw
		{ +1, +1 }, // se
	};

	union neighbor_coordinates coord;

	for(unsigned i = 0; i < 8; ++i) {
		coord.items[i].x = x + delta[i].x;
		coord.items[i].y = y + delta[i].y;
	}

	for(unsigned i = 0; i < 8; ++i)
		n.items[i] = bucket;


	switch(edge) {
	case EDGE_NORTH|EDGE_WEST: // NE
		n.n  = neighbour->n;
		coord.n.x = 0;
		coord.n.y = max;

		n.ne = neighbour->n;
		coord.ne.x = 1;
		coord.ne.y = max;

		n.nw = neighbour->nw;
		coord.nw.x = max;
		coord.nw.y = max;

		n.w  = neighbour->w;
		coord.w.x = max;
		coord.w.y = 0;

		n.sw = neighbour->w;
		coord.sw.x = max;
		coord.sw.y = 1;

		break;
	case EDGE_NORTH|EDGE_EAST: // NW
		n.n  = neighbour->n;
		coord.n.x = max;
		coord.n.y = max;

		n.ne = neighbour->ne;
		coord.ne.x = 0;
		coord.ne.y = max;

		n.nw = neighbour->n;
		coord.nw.x = max-1;
		coord.nw.y = max;

		n.e  = neighbour->e;
		coord.e.x = 0;
		coord.e.y = 0;
		n.se = neighbour->e;
		coord.se.x = 0;
		coord.se.y = 1;

		break;
	case EDGE_SOUTH|EDGE_WEST: // SW
		n.s  = neighbour->s;
		coord.s.x = 0;
		coord.s.y = 0;

		n.se = neighbour->s;
		coord.se.x = 1;
		coord.se.y = 0;

		n.sw = neighbour->sw;
		coord.sw.x = max;
		coord.sw.y = 0;

		n.w  = neighbour->w;
		coord.w.x = max;
		coord.w.y = max;

		n.nw = neighbour->w;
		coord.nw.x = max;
		coord.nw.y = max-1;

		break;
	case EDGE_SOUTH|EDGE_EAST: // SE
		n.s  = neighbour->s;
		coord.s.x = max;
		coord.s.y = 0;

		n.se = neighbour->se;
		coord.se.x = 0;
		coord.se.y = 0;

		n.sw = neighbour->s;
		coord.sw.x = max-1;
		coord.sw.y = 0;

		n.e  = neighbour->e;
		coord.e.x = 0;
		coord.e.y = max;

		n.ne = neighbour->e;
		coord.ne.x = 0;
		coord.ne.y = max-1;

		break;
	}


	unsigned c = 0;

	unsigned coordsz = sizeof(coord.items)/sizeof(coord.items[0]);

	for(unsigned j = 0; j < coordsz; ++j) {
		if (!n.items[j]) continue;

		c += index_bucket(n.items[j],
			          coord.items[j].x,
			          coord.items[j].y);
	}

	value v = bucket && index_bucket(bucket, x, y);

	cell(changes, c, xp+x, yp+y, v);
}

static void edge_step(struct state_change_buffer *changes,
                      union bucket_neighbours *neighbour,
                      struct bucket *bucket, coordinate xp, coordinate yp,
                      enum bucket_edge edge)
{
	union bucket_neighbours n = *neighbour;

	for(unsigned i = 0; i < 8; ++i)
		n.items[i] = bucket;

	switch(edge) {
	case EDGE_NORTH:
		n.n  = neighbour->n;
		n.ne = neighbour->n;
		n.nw = neighbour->n;
		break;
	case EDGE_SOUTH:
		n.s  = neighbour->s;
		n.se = neighbour->s;
		n.sw = neighbour->s;
		break;
	case EDGE_EAST:
		n.e  = neighbour->e;
		n.ne = neighbour->e;
		n.se = neighbour->e;
		break;
	case EDGE_WEST:
		n.w  = neighbour->w;
		n.nw = neighbour->w;
		n.sw = neighbour->w;
		break;
	}

	for (coordinate i = 1; i < BUCKETSZ-1; ++i) {

		coordinate x, y;

		switch(edge) {
		case EDGE_NORTH:
			x = i; y = 0;
			break;
		case EDGE_SOUTH:
			x = i; y = BUCKETSZ-1;
			break;
		case EDGE_WEST:
			x = 0; y = i;
			break;
		case EDGE_EAST:
			x = BUCKETSZ-1; y = i;
			break;
		}


		union neighbor_coordinates coord = {
			{
				{ x-1, y+0 }, // w
				{ x+1, y+0 }, // e
				{ x+0, y-1 }, // n
				{ x+0, y+1 }, // s
				{ x-1, y-1 }, // nw
				{ x+1, y-1 }, // ne
				{ x-1, y+1 }, // sw
				{ x+1, y+1 }, // se
			},
		};

		switch(edge) {
		case EDGE_NORTH:
			coord.n.y  = BUCKETSZ-1;
			coord.nw.y = BUCKETSZ-1;
			coord.ne.y = BUCKETSZ-1;
			break;
		case EDGE_SOUTH:
			coord.s.y  = 0;
			coord.sw.y = 0;
			coord.se.y = 0;
			break;
			
		case EDGE_WEST:
			coord.w.x  = BUCKETSZ-1;
			coord.nw.x = BUCKETSZ-1;
			coord.sw.x = BUCKETSZ-1;
			break;

		case EDGE_EAST:
			coord.e.x  = 0;
			coord.ne.x = 0;
			coord.se.x = 0;
			break;
		}

		unsigned c = 0;

		unsigned coordsz = sizeof(coord.items)/sizeof(coord.items[0]);

		for(unsigned j = 0; j < coordsz; ++j) {
			if (!n.items[j]) continue;

			c += index_bucket(n.items[j],
			                  coord.items[j].x,
			                  coord.items[j].y);
		}

		value v = bucket && index_bucket(bucket, x, y);
		cell(changes, c, xp+x, yp+y, v);
	}
}

static void bucket_step(struct quad *now,
                        struct bucket *bucket,
                        union bucket_neighbours *neighbours,
                        struct state_change_buffer *changes)
{
	coordinate xp = bucket->x * BUCKETSZ;
	coordinate yp = bucket->y * BUCKETSZ;

	edge_step(changes, neighbours, bucket, xp, yp, EDGE_NORTH);
	edge_step(changes, neighbours, bucket, xp, yp, EDGE_SOUTH);
	edge_step(changes, neighbours, bucket, xp, yp, EDGE_WEST);
	edge_step(changes, neighbours, bucket, xp, yp, EDGE_EAST);

	corner_step(changes, neighbours, bucket, xp, yp, EDGE_NORTH|EDGE_WEST);
	corner_step(changes, neighbours, bucket, xp, yp, EDGE_NORTH|EDGE_EAST);
	corner_step(changes, neighbours, bucket, xp, yp, EDGE_SOUTH|EDGE_WEST);
	corner_step(changes, neighbours, bucket, xp, yp, EDGE_SOUTH|EDGE_EAST);

	if (!neighbours->n) {
		yp -= BUCKETSZ;

                union bucket_neighbours mn;
		mn.s  = bucket;
		mn.w  = neighbours->nw;
		mn.e  = neighbours->ne;
		mn.sw = neighbours->w;
		mn.se = neighbours->e;

		edge_step(changes, &mn, NULL, xp, yp, EDGE_SOUTH);
		corner_step(changes, &mn, NULL,
		            xp, yp, EDGE_SOUTH|EDGE_WEST);
		corner_step(changes, &mn, NULL,
		            xp, yp, EDGE_SOUTH|EDGE_EAST);

		yp += BUCKETSZ;
	}

	if (!neighbours->s) {
		yp += BUCKETSZ;

                union bucket_neighbours mn;
		mn.n  = bucket;
		mn.w  = neighbours->sw;
		mn.e  = neighbours->se;
		mn.nw = neighbours->w;
		mn.ne = neighbours->e;

		edge_step(changes, &mn, NULL, xp, yp, EDGE_NORTH);
		corner_step(changes, &mn, NULL,
		            xp, yp, EDGE_NORTH|EDGE_WEST);
		corner_step(changes, &mn, NULL,
		            xp, yp, EDGE_NORTH|EDGE_EAST);

		yp -= BUCKETSZ;
	}

	if (!neighbours->w) {
		xp -= BUCKETSZ;

                union bucket_neighbours mn;
		mn.e  = bucket;
		mn.n  = neighbours->nw;
		mn.s  = neighbours->sw;
		mn.ne = neighbours->n;
		mn.se = neighbours->s;

		edge_step(changes, &mn, NULL, xp, yp, EDGE_EAST);
		corner_step(changes, &mn, NULL,
		            xp, yp, EDGE_NORTH|EDGE_EAST);
		corner_step(changes, &mn, NULL,
		            xp, yp, EDGE_SOUTH|EDGE_EAST);

		xp += BUCKETSZ;
	}

	if (!neighbours->e) {
		xp += BUCKETSZ;

                union bucket_neighbours mn;
		mn.w  = bucket;
		mn.n  = neighbours->ne;
		mn.s  = neighbours->se;
		mn.nw = neighbours->n;
		mn.sw = neighbours->s;

		edge_step(changes, &mn, NULL, xp, yp, EDGE_WEST);
		corner_step(changes, &mn, NULL,
		            xp, yp, EDGE_NORTH|EDGE_WEST);
		corner_step(changes, &mn, NULL,
		            xp, yp, EDGE_SOUTH|EDGE_WEST);

		xp -= BUCKETSZ;
	}


	struct { coordinate x, y; } delta[8] = {
		{ -1,  0 }, // w
		{ +1,  0 }, // e
		{  0, -1 }, // n
		{  0, +1 }, // s
		{ -1, -1 }, // nw
		{ +1, -1 }, // ne
		{ -1, +1 }, // sw
		{ +1, +1 }, // se
	};
	unsigned deltasz = sizeof(delta)/sizeof(delta[0]);


	for(coordinate y = 1; y < BUCKETSZ-1; ++y) {
		for(coordinate x = 1; x < BUCKETSZ-1; ++x) {
			unsigned n = 0;

			for(unsigned i = 0; i < deltasz; ++i) {
				n += index_bucket(bucket,
			          	  	  x + delta[i].x,
			          	  	  y + delta[i].y);
			}

			cell(changes, n, xp+x, yp+y, index_bucket(bucket, x, y));
		}
	}
}

struct step_arguments {
	struct quad *now;
	struct state_change_buffer *changes;
};

static void run_step(struct quad *now, struct state_change_buffer *changes);

static void run_stepa(void *opaque, int run)
{
	struct step_arguments *args = opaque;
	struct quad *now = args->now;
	struct state_change_buffer *changes = args->changes;

	free(args);

	if (run)
		run_step(now, changes);
}

static void run_step(struct quad *now, struct state_change_buffer *changes)
{
	assert(now);
	assert(changes);

	if (now->leaf) {

		union bucket_neighbours neighbours;

		struct bucket *cur = now->items.head;
		while(cur) {

			struct { coordinate x, y; } delta[8] = {
				{ -1,  0 }, // w
				{ +1,  0 }, // e
				{  0, -1 }, // n
				{  0, +1 }, // s
				{ -1, -1 }, // nw
				{ +1, -1 }, // ne
				{ -1, +1 }, // sw
				{ +1, +1 }, // se
			};
			unsigned deltasz = sizeof(delta)/sizeof(delta[0]);


			for(unsigned i = 0; i < deltasz; ++i) {
				coordinate x, y;
				x = (cur->x + delta[i].x) * BUCKETSZ;
				y = (cur->y + delta[i].y) * BUCKETSZ;
				struct bucket *b = find_bucket(now, x, y, NULL);
				neighbours.items[i] = b;
			}

			bucket_step(now, cur, &neighbours, changes);
			cur = cur->next;
		}
	} else {
		for(unsigned i = 0; i < 4; ++i)
			run_step(now->children[i], changes);
	}
}

void step(struct quad *now,
          struct state_change_buffer *changes,
          void *q)
{
	struct workq *queue = q;

	if (now->leaf) {
		struct step_arguments *a = malloc(sizeof(struct step_arguments));
		if (!a)
			return;

		a->now = now;
		a->changes = changes;
		workq_add(queue, a, run_stepa);
	} else {
		for(unsigned i = 0; i < 4; ++i)
			step(now->children[i], changes, queue);
	}
}


/* 1}}} */

void release(struct quad *quad)
{
	if (quad->leaf) {
		struct bucket *cur = quad->items.head;
		struct bucket *del = cur;
		while(cur) {
			del = cur;
			cur = cur->next;
			free(del);
		}
	} else {
		for(unsigned i = 0; i < 4; ++i) {
			release(quad->children[i]);
		}
		free(quad->children[0]);
	}
}
