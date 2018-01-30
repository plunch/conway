
#include <stdint.h>

#define COORD_MAX UINT16_MAX
typedef uint16_t coordinate;

#define VALUE_BIT (8*sizeof(unsigned char))
typedef unsigned char value;


#define BUCKETSZ 16
#define QUADSZ 4

struct bucket {
	coordinate x, y, num;
	struct bucket *next, *prev;

	value bucket[BUCKETSZ * BUCKETSZ / VALUE_BIT];
};


struct quad {
	coordinate west, east, north, south;
	unsigned leaf, count;
	struct quad *parent;
	union {
		struct {
			struct quad *nw, *ne, *sw, *se;
		} child_to;
		struct quad *children[4];
		struct {
			struct bucket *head, *tail;
		} items;
	};
};

struct state_change {
	coordinate x, y;
	value v;
};

struct state_change_buffer {
	unsigned length, capacity;
	void *opaque;
	struct state_change *items;
};

struct conway {
	struct quad *root;
	struct state_change_buffer changes;
	unsigned generation;
	void *opaque;
};

void release(struct quad *quad);
int conway_create(struct conway *cw, struct quad *root);
void conway_destroy(struct conway *cw);

value get(struct quad *quad, coordinate x, coordinate y);
void set(struct quad *quad, coordinate x, coordinate y, value v);

void update(struct conway *cw);

void step(struct quad *quad,
          struct state_change_buffer *changes,
          void *queue);

