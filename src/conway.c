#include "conway.h"

#include <stdlib.h> /* malloc, realloc, free */
#include <assert.h> /* assert */
#include <string.h> /* memset */


static int append(struct state_change_buffer *buf,
                  coordinate x, coordinate y, value v)
{
	assert(buf);

	if (buf->length == buf->capacity) {
		unsigned new_cap = buf->capacity * 2;
		if (new_cap == 0)
			new_cap = 8;
		void *tmp = realloc(buf->items,
		                    sizeof(struct state_change) * new_cap);
		if (!tmp)
			return 0;
		buf->items = tmp;
		buf->capacity = new_cap;
	}

	unsigned i = buf->length++;
	buf->items[i].x = x;
	buf->items[i].y = y;
	buf->items[i].v = v;

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

value get(struct quad *quad, coordinate x, coordinate y)
{
	assert(quad);

	struct bucket *current = find_bucket(quad, x, y, NULL);

	if (!current)
		return 0;

	coordinate ix = x - current->x * BUCKETSZ;
	coordinate iy = y - current->y * BUCKETSZ;
	coordinate i = ix + iy * BUCKETSZ;
	return current->bucket[i / VALUE_BIT] & (1 << (i % VALUE_BIT));
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

void update(struct quad *now, struct state_change_buffer *changes)
{
	for(unsigned i = 0; i < changes->length; ++i) {
		struct state_change c = changes->items[i];
		set(now, c.x, c.y, c.v);
	}
}

static void bucket_step(struct quad *now, struct bucket *bucket, struct state_change_buffer *changes)
{
	coordinate xp = bucket->x * BUCKETSZ;
	coordinate yp = bucket->y * BUCKETSZ;

	for(coordinate iy = 0; iy < BUCKETSZ+2; ++iy) {
		for(coordinate ix = 0; ix < BUCKETSZ+2; ++ix) {
			coordinate x = ix + xp - 1;
			coordinate y = iy + yp - 1;
			coordinate n = 0;

			value v = get(now, x, y);

			if (get(now, x-1, y))     // west
				n++;
			if (get(now, x-1, y-1))   // north-west
				n++;
			if (get(now, x, y-1))     // north
				n++;
			if (get(now, x+1, y-1))   // north-east
				n++;
			if (get(now, x+1, y))     // east
				n++;
			if (get(now, x+1, y+1))   // south-east
				n++;
			if (get(now, x, y+1))     // south
				n++;
			if (get(now, x-1, y+1))   // south-west
				n++;

			if (v) {
				switch(n) {
				 case 0:
				 case 1: // Starvation
					append(changes, x, y, 0);
					continue;
				 case 2:
				 case 3: // No change
					continue;
				default: // Overpopulation
					append(changes, x, y, 0);
					continue;
				}
			} else if (n == 3) {
				append(changes, x, y, 1);
			} 
		}
	}
}

void step(struct quad *now, struct state_change_buffer *changes)
{
	assert(now);
	assert(changes);

	if (now->leaf) {
		struct bucket *cur = now->items.head;
		while(cur) {
			bucket_step(now, cur, changes);
			cur = cur->next;
		}
	} else {
		for(unsigned i = 0; i < 4; ++i)
			step(now->children[i], changes);
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
