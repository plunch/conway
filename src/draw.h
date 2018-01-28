
struct draw {
	struct {
		coordinate x, y, w, h;
		unsigned scale;
	} view;
	int dbg;
	void *opaque;
};

int  draw_create(struct draw *d);
void draw_destroy(struct draw *d);

void draw(struct draw *d, struct quad *quad,
          struct state_change_buffer *changes);

enum draw_update_result {
	DR_OK,
	DR_QUIT,
	DR_ERROR,
};

enum draw_update_result draw_update(struct draw *d);

const char* draw_geterror();

