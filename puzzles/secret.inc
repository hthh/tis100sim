// included into a switch statement

case 3: {
	// UNKNOWN (SECRET PUZZLE)
	static int secret_layout[ARENA_SIZE] = {
		N_NONE,  N_IN,    N_NONE,  N_NONE,
		N_USER,  N_USER,  N_USER,  N_NONE,
		N_USER,  N_USER,  N_USER,  N_NONE,
		N_NONE,  N_USER,  N_USER,  N_USER,
		N_NONE,  N_OUT,   N_OUT,   N_NONE,
	};

	static int secret_input_a[] = {90, 87, 85, 84, 63, 56, 62, 59, 64, 65, 38, 35, 37, 38, 33, 33, 66, 63, 57, 60, 89, 89, 10, 14, 18, 6, 35, 41, 39, 32, 93, 89, 9, 14, 7, 15, 59, 58, -1};
	static int secret_output_a[] = {3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 0, 0, 0, 0, 1, 1, 1, 1, 3, 3, 0, 0, 0, 0, 2, 2, -1};
	static int secret_output_b[] = {4, 3, 6, 2, 6, 1, 4, 2, 2, 3, 4, 0, 4, 1, 2, 3, 4, 0, 2, 2};

	if (!arena_set_layout(&arena, secret_layout))
		return -1;

	arena.in_nodes[0].values = secret_input_a;
	arena.in_nodes[0].num_values = ARRAY_LENGTH(secret_input_a);
	arena.out_nodes[0].values = secret_output_a;
	arena.out_nodes[0].num_values = ARRAY_LENGTH(secret_output_a);
	arena.out_nodes[0].output_prefix = "OUT.R";
	arena.out_nodes[1].values = secret_output_b;
	arena.out_nodes[1].num_values = ARRAY_LENGTH(secret_output_b);
	arena.out_nodes[1].output_prefix = "                    OUT.E";
} break;
