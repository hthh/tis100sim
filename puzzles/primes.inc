// included into a switch statement

case -1: // default
case 1:
case 60135: {
	// 60135 PRIME DETECTOR
	static int primes_layout[ARENA_SIZE] = {
		N_NONE,  N_IN,    N_NONE,  N_NONE,
		N_USER,  N_USER,  N_USER,  N_USER,
		N_STACK, N_USER,  N_USER,  N_STACK,
		N_NONE,  N_USER,  N_USER,  N_USER,
		N_NONE,  N_NONE,  N_OUT,   N_NONE,
	};

	static int primes_input_a[] = {280, 122, 132, 797, 613, 563, 555, 516, 863, 516, 233, 204, 906, 518, 154, 179, 595, 809, 239, 281, 787, 991, 499, 361, 114, 631, 233, 133, 163, 277, 433, 710, 283, 751, 434, 917, 797, 383, 890};
	static int primes_output_a[] = {0, 0, 0, 1, 1, 1, 0, 0, 1, 0, 1, 0, 0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 0, 1, 1, 1, 0, 1, 1, 0, 0, 1, 1, 0};

	if (!arena_set_layout(&arena, primes_layout))
		return -1;

	arena.in_nodes[0].values = primes_input_a;
	arena.in_nodes[0].num_values = ARRAY_LENGTH(primes_input_a);
	arena.out_nodes[0].values = primes_output_a;
	arena.out_nodes[0].num_values = ARRAY_LENGTH(primes_output_a);
} break;
