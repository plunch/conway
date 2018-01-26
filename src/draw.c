#include "conway.h"
#include "draw.h"

#include <stdlib.h> /* malloc, free */
#include <string.h> /* strerror */
#include <errno.h>  /* errno */
#include <string.h> /* strerror */

#include <SDL2/SDL.h>

const char* draw_geterror()
{
	return SDL_GetError();
}

struct draw_data {
	SDL_Surface *screen;
	SDL_Window  *window;
	Uint32       wasInit;
	int dirty;
	int xrelacc, yrelacc;
};

int draw_create(struct draw *draw)
{
	struct draw_data *data = malloc(sizeof(struct draw_data));
	if (!data) {
		SDL_SetError("%s", strerror(errno));
		return 0;
	}

	data->dirty = 1;
	data->xrelacc = 0;
	data->yrelacc = 0;
	data->wasInit = SDL_WasInit(SDL_INIT_VIDEO);

	if (!data->wasInit && SDL_Init(SDL_INIT_VIDEO) < 0)
		goto exit;

	data->window = SDL_CreateWindow("conway", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
	                                draw->view.w * draw->view.scale, draw->view.h * draw->view.scale,
	                                SDL_WINDOW_RESIZABLE);
	if (!data->window)
		goto exit;

	data->screen = SDL_GetWindowSurface(data->window);
	if (!data->screen)
		goto exit;

	draw->opaque = data;

	return 1;
exit:
	free(data);
	return 0;
}

void draw_destroy(struct draw *d)
{
	struct draw_data *data = d->opaque;

	if (!data)
		return;

	if (data->window)
		SDL_DestroyWindow(data->window);
	data->window = NULL;
	data->screen = NULL;

	if (data->wasInit)
		SDL_Quit();
	free(data);
	d->opaque = NULL;
}

enum draw_update_result draw_update(struct draw *draw)
{
	struct draw_data *d = draw->opaque;
	
	int x, y, w, h;

	SDL_Event e;
	while(SDL_PollEvent(&e)) {
		switch(e.type) {

		case SDL_QUIT:
			return DR_QUIT;

		case SDL_WINDOWEVENT:
			switch(e.window.event) {
			case SDL_WINDOWEVENT_SIZE_CHANGED:
				w = ((int)e.window.data1 - d->screen->w) / draw->view.scale;
				h = ((int)e.window.data2 - d->screen->h) / draw->view.scale;
				draw->view.w = (int)draw->view.w + w;
				draw->view.h = (int)draw->view.h + h;
				d->screen = SDL_GetWindowSurface(d->window);
				if (!d->screen)
					return DR_ERROR;
				d->dirty = 1;
				break;
			}
			break;

		case SDL_MOUSEMOTION:
			if (e.motion.state & SDL_BUTTON_LMASK) {
				x = (e.motion.xrel + d->xrelacc) / (int)draw->view.scale;
				y = (e.motion.yrel + d->yrelacc) / (int)draw->view.scale;
				d->xrelacc = e.motion.xrel + d->xrelacc - x * (int)draw->view.scale;
				d->yrelacc = e.motion.yrel + d->yrelacc - y * (int)draw->view.scale;
				draw->view.x = (int)draw->view.x - x;
				draw->view.y = (int)draw->view.y - y;
				d->dirty = 1;
			}
			break;

		case SDL_MOUSEBUTTONUP:
			if (e.button.button == SDL_BUTTON_LEFT) {
				d->xrelacc = 0;
				d->yrelacc = 0;
			}

			break;
		}
	}

	return DR_OK;
}



static void dbg_draw(struct draw *d, struct draw_data *data, struct quad *quad, unsigned depth)
{
	SDL_Surface *screen = data->screen;

	struct sdlcol { Uint8 r, g, b; } colors[] = {
		{ 222, 107, 26 },
		{ 69,  53,  42 },
		{ 171, 53,  37 },
		{ 85,  181, 107 },
		{ 19,  103, 14 },
	};

	SDL_Rect scbounds = { 0, 0, screen->w, screen->h };

	unsigned color_sz = sizeof(colors)/sizeof(colors[0]);

	SDL_Rect r, b;
	r.x = (coordinate)(quad->west * BUCKETSZ - d->view.x);
	r.y = (coordinate)(quad->north * BUCKETSZ - d->view.y);
	r.w = (quad->east  - quad->west) * BUCKETSZ;
	r.h = (quad->south - quad->north) * BUCKETSZ;
	r.x *= d->view.scale;
	r.y *= d->view.scale;
	r.w *= d->view.scale;
	r.h *= d->view.scale;
	if (SDL_IntersectRect(&r, &scbounds, &b)) {
		struct sdlcol col = colors[depth % color_sz];
		SDL_FillRect(screen, &b, SDL_MapRGB(screen->format, col.r/2, col.g/2, col.b/2));
	} else {
		return;
	}


	if (quad->leaf) {
		struct bucket *cur = quad->items.head;
		while(cur != NULL) {

			r.x = (coordinate)(cur->x * BUCKETSZ - d->view.x);
			r.x *= d->view.scale;
			r.y = (coordinate)(cur->y * BUCKETSZ - d->view.y);
			r.y *= d->view.scale;
			r.w = BUCKETSZ * d->view.scale;
			r.h = BUCKETSZ * d->view.scale;

			if (SDL_IntersectRect(&r, &scbounds, &b)) {
				struct sdlcol c = colors[cur->num % color_sz];
				SDL_FillRect(screen, &b, SDL_MapRGB(screen->format, c.r, c.g, c.b));
			}
			cur = cur->next;
		}
	} else {
		for(unsigned i = 0; i < 4; ++i)
			dbg_draw(d, data, quad->children[i], depth + 1);
	}
}


void draw(struct draw *d, struct quad *quad,
          struct state_change_buffer *changes)
{
	struct draw_data *data = d->opaque;
	SDL_Surface *screen = data->screen;

	if (quad->count == 0)
		return;

	if (screen->w <= 0 || screen->h <= 0)
		return;

	int mlock;
	if ((mlock = SDL_MUSTLOCK(screen))) {
		if (SDL_LockSurface(screen) < 0)
			return;
	}


	Uint32 on = SDL_MapRGB(screen->format, 255, 255, 255);
	Uint32 off = SDL_MapRGB(screen->format, 0, 0, 0);
	SDL_Rect screen_bounds = { 0, 0, screen->w, screen->h };
	SDL_Rect r, b;

	if (data->dirty) {
		(void)(changes);

		SDL_FillRect(screen, &screen_bounds, off);

		/*
		if (color_dbg) {
			dbg_draw(g, screen,
		         	 vx, vy, vw, vh,
		         	 scale, 0);
		}
		*/

		for(coordinate y = 0; y < d->view.h; y++) {
			for(coordinate x = 0; x < d->view.w; x++) {
				value v = get(quad, d->view.x + x, d->view.y + y);

				if (!v)
					continue;

				r.x = x * d->view.scale;
				r.y = y * d->view.scale;
				r.w = d->view.scale;
				r.h = d->view.scale;

				if (SDL_IntersectRect(&r, &screen_bounds, &b))
					SDL_FillRect(screen, &b, on);
			}
		}

		data->dirty = 0;
	} else {
		for(unsigned i = 0; i < changes->length; ++i) {
			struct state_change c = changes->items[i]; 

			coordinate scx = c.x-d->view.x;
			coordinate scy = c.y-d->view.y;

			int x = scx * d->view.scale;
			int y = scy * d->view.scale;

			SDL_Rect r;
			r.x = x;
			r.y = y;
			r.w = d->view.scale;
			r.h = d->view.scale;

			if (SDL_IntersectRect(&r, &screen_bounds, &b))
				SDL_FillRect(screen, &b, c.v ? on : off);
		}
	}

	if (mlock)
		SDL_UnlockSurface(screen);

	SDL_UpdateWindowSurface(data->window);
}
