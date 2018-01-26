#include <stdio.h> /* fprintf, fopen, fclose, perror */

#include "conway.h"
#include "load.h"

#ifndef DBG_SILENT
#include "draw.h"
#endif

#include <unistd.h> /* getopt, opatrg, optind */
#include <time.h>   /* nanosleep */
#include <string.h> /* strtok */
#include <stdlib.h> /* atoi */


int main(int argc, char *argv[])
{
	if (BUCKETSZ % VALUE_BIT) {
		fprintf(stderr, "Size of bucket (%i) must be evenly divisible by number of bytes in the value-word\n",
		        BUCKETSZ);
		return 1;
	}

	struct draw display;
	display.view.x = 0;
	display.view.y = 0;
	display.view.w = 0;
	display.view.h = 0;
	display.view.scale = 4;

	int pattx = -1; int patty = -1;
	int speed = 100;
	char* tok;
	int rle = 0;


	int c;
	while((c = getopt(argc, argv, "hcrfs:b:t:")) != -1) {
		switch(c) {
		case 'h':
			fprintf(stderr, "Usage: conway [OPTION]... [FILE]\n"
			                "Options:\n"
			                "	-h	display this help screen.\n"
			                "	-f	run as fast as possible.\n"
			                "	-c	debug underlying data structures with colors.\n"
			                "	-s	number of milliseconds per generation.\n"
			                "	-b	view bounds and scale (x:y:w:h:scale).\n"
			                "	-t	where to place the pattern's top left (x:y).\n"
			                "	-r	read RLE input.\n"
			                "\n"
			                "With no FILE, or when FILE is -, read standard input.\n");
			return 0;
		case 'c':
			//dbg = 1;
			break;
		case 'f':
			if (speed != 100) {
				fprintf(stderr, "Option -s and -f may not be specified together.\n");
				return 1;
			}
			speed = 0;
			break;
		case 's':
			if (speed == 0) {
				fprintf(stderr, "Option -s and -f may not be specified together.\n");
				return 1;
			}
			speed = atoi(optarg);
			break;
		case 'r':
			rle = 1;
			break;
		case 't':
			tok = strtok(optarg, ":");
			if (tok == NULL) break;
			pattx = atoi(tok);
			tok = strtok(NULL, ":");
			if (tok == NULL) break;
			patty = atoi(tok);
			break;
		case 'b':
			tok = strtok(optarg, ":");
			if (tok == NULL) break;
			display.view.x = atoi(tok);

			tok = strtok(NULL, ":");
			if (tok == NULL) break;
			display.view.y = atoi(tok);

			tok = strtok(NULL, ":");
			if (tok == NULL) break;
			display.view.w = atoi(tok);

			tok = strtok(NULL, ":");
			if (tok == NULL) break;
			display.view.h = atoi(tok);

			tok = strtok(NULL, ":");
			if (tok == NULL) break;
			display.view.scale = atoi(tok);
			break;
		case '?':
			switch(optopt) {
			case 'x':
			case 'y':
			case 'w':
			case 'h':
				fprintf(stderr, "Option -%c requires an argument.\n", optopt);
				return 1;
			default:
				return 1;
			}
		default:
			abort();
		}
	}

	struct quad quad;
	quad.west = 0;
	quad.east = (COORD_MAX / BUCKETSZ)+1;
	quad.north = 0;
	quad.south = (COORD_MAX / BUCKETSZ)+1;
	quad.leaf = 1;
	quad.count = 0;
	quad.parent = NULL;
	quad.items.head = NULL;
	quad.items.tail = NULL;


	struct bounds patt_bounds = {
		0, 0, 0, 0,
		0, 0, 0, 0,
	};
	if (pattx == -1)
		pattx = display.view.x + display.view.w/2;
	if (patty == -1)
		patty = display.view.y + display.view.h/2;

	FILE* stream;
	switch(argc - optind) {
		case 0:
			stream = stdin;
			break;
		case 1:
			stream = fopen(argv[optind], "r");
			if (stream == NULL) {
				perror(argv[optind]);
				return 1;
			}
			break;
		default:
			fprintf(stderr, "Excess arguments:");
			for(int i = optind+1; i < argc; ++i)
				fprintf(stderr, " %s", argv[i]);
			fprintf(stderr, ".\n");
			return 1;
	}

	if (rle) {
		if (!load_rle(&quad, &patt_bounds, stream, pattx, patty))
			return 1;
	} else {
		if (!load_cells(&quad, &patt_bounds, stream, pattx, patty))
			return 1;
	}

	if (stream != stdin)
		fclose(stream);

	if (display.view.w == 0 || display.view.h == 0) {
		if (patt_bounds.w_set && patt_bounds.e_set && patt_bounds.n_set && patt_bounds.s_set) {
			display.view.x = patt_bounds.west - 4;
			display.view.y = patt_bounds.north - 4;

			display.view.w = patt_bounds.east - display.view.x + 8;
			display.view.h = patt_bounds.south - display.view.y + 8;
		} else {
			display.view.w = 200 / display.view.scale;
			display.view.h = 150 / display.view.scale;
		}
	}

#ifndef DBG_SILENT
	if (!draw_create(&display)) {
		fprintf(stderr, "%s", draw_geterror());
		return 1;
	}
#endif /* DBG_SILENT */


	unsigned generation = 0;
	struct state_change_buffer change_buffer = { 0, 0, NULL };
	struct timespec time = { 0, speed * 1000000 };
	do {
#ifdef DBG_SILENT
		if (generation >= 1000)
			break;
#else
		enum draw_update_result du = draw_update(&display);
		if (du != DR_OK)
			break;

		draw(&display, &quad, &change_buffer);
#endif /* DBG_SILENT */

		change_buffer.length = 0;
		step(&quad, &change_buffer);
		update(&quad, &change_buffer);

		generation++;
	} while(speed == 0 ? 1 : !nanosleep(&time, NULL));


	release(&quad);
	if (change_buffer.capacity > 0) {
		free(change_buffer.items);
		change_buffer.items = NULL;
		change_buffer.length = 0;
		change_buffer.capacity = 0;
	}

#ifndef DBG_SILENT
	draw_destroy(&display);
#endif
}
