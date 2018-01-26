#include <stdio.h>   /* fgetc, fprintf, FILE* */

#include "conway.h"
#include "load.h"

#include <stdlib.h>  /* atoi */

int load_rle(struct quad *qua, struct bounds *bounds,
             FILE* stream, coordinate initial_x, coordinate initial_y)
{
	coordinate x = initial_x;
	coordinate y = initial_y;

	if (bounds) {
		bounds->west  = x;
		bounds->w_set = 1;
		bounds->north = y;
		bounds->n_set = 1;
	}

	int col = 0;
	int row = 0;

	int state = 0;

	char buf[128];
	unsigned offset = 0;
	int input_len = 0;

	char c = '\n';
	while(c != EOF) {
		switch(state) {
		case 0:
			if (c == '\n') {
				row++;
				col = 0;
				c = fgetc(stream);
				col++;

				if (c == '#') {
					while((c = fgetc(stream)) != EOF) {
						col++;
						if (c == '\n') {
							row++;
							col = 0;
							break;
						}
					}
				} else {
					state = 1;
				}
			} else {
				state = 1;
			}
			break;
		case 1:
			do {
				if (c == '\n') {
					row++;
					col = 0;
					state = 2;
					break;
				}
			}
			while (++col && (c = fgetc(stream)) != EOF);
			break;
		case 2:
			switch(c) {
			case '\n':
				row++;
				col = 0;
				/* FALLTHROUGH */
			case ' ':
			case '\t':
			case '\r':
				c = fgetc(stream);
				col++;
				break;
			default:
				state = 3;
			}
			break;
		case 3:
			if (c > 47 && c < 58) {
				buf[offset++] = c;
				c = fgetc(stream);
				col++;
			} else {
				buf[offset] = '\0';
				state = 4;
			}
			break;
		case 4:
			if (offset == 0)
				input_len = 1;
			else
				input_len = atoi(buf);
			offset = 0;

			switch(c) {
			case 'b':
				x += input_len;
				input_len = 0;
				break;
			case 'o':
				while(input_len--) {
					set(qua, x++, y, 1);
				}
				break;
			case '$':
				y += input_len;

				if (bounds) {
					if (!bounds->s_set || bounds->south < y) {
						bounds->south = y;
						bounds->s_set = 1;
					}

					if (!bounds->e_set || bounds->east < x) {
						bounds->east = x;
						bounds->e_set = 1;
					}
				}

				x = initial_x;
				break;
			case '!':
				if (bounds && (!bounds->e_set || bounds->east < x)) {
					bounds->east = x;
					bounds->e_set = 1;
				}
				return 1;
			default:
				fprintf(stderr,
				        "%u:%u: Unexpected character '%c'. "
				        "One of 'b', 'o', '$' is expected.\n",
				        row, col, c);
				return 0;
			}
			c = fgetc(stream);
			col++;
			state = 2;
			break;
		}
	}

	if (bounds && (!bounds->e_set || bounds->east < x)) {
		bounds->east = x;
		bounds->e_set = 1;
	}
	return 1;
}

int load_cells(struct quad *qua, struct bounds *bounds,
               FILE* stream, coordinate initial_x, coordinate initial_y)
{
	coordinate x = initial_x;
	coordinate y = initial_y;

	if (bounds) {
		bounds->west  = x;
		bounds->w_set = 1;
		bounds->north = y;
		bounds->n_set = 1;
	}

	char c;
	unsigned row = 1, col = 1;
	int comment = 0;
	while((c = fgetc(stream)) != EOF) {
		if (c == '\r')
			continue;

		if (comment) {
			if (c == '\n') {
				row++;
				col = 1;
				comment = 0;
			}
			continue;
		}

		switch(c) {
		case '\n':
			y++;
			if (bounds) {
				if (!bounds->s_set || bounds->south < y) {
					bounds->south = y;
					bounds->s_set = 1;
				}

				if (!bounds->e_set || bounds->east < x) {
					bounds->east = x;
					bounds->e_set = 1;
				}
			}
			x = initial_x;

			row++;
			col = 1;
			break;
		case 'O':
			set(qua, x, y, 1);
			x++;
			break;
		case '.':
			x++;
			break;
		case '!':
			if (col == 1) {
				comment = 1;
				continue;
			}
			/* FALLTHROUGH */
		default:
			fprintf(stderr,
			        "stdin:%u:%u: Invalid character '%c'\n",
			        row, col, c);
			return 0;
		}

		col++;
	}

	if (bounds && (!bounds->e_set || bounds->east < x)) {
		bounds->east = x;
		bounds->e_set = 1;
	}
	return 1;
}

