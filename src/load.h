
struct bounds  {
	coordinate west, east, north, south;
	unsigned char w_set, e_set, n_set, s_set;
};

int load_rle(struct quad *qua, struct bounds *bounds,
             FILE* stream, coordinate initial_x, coordinate initial_y);

int load_cells(struct quad *qua, struct bounds *bounds,
               FILE* stream, coordinate initial_x, coordinate initial_y);
