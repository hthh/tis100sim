
// 60135 PRIME DETECTOR
static int test_layout[ARENA_SIZE] = {
	N_NONE,  N_IN,    N_NONE,  N_NONE,
	N_USER,  N_USER,  N_USER,  N_USER,
	N_STACK, N_USER,  N_USER,  N_STACK,
	N_NONE,  N_USER,  N_USER,  N_USER,
	N_NONE,  N_NONE,  N_OUT,   N_NONE,
};

static int test_input_a[] = {280, 122, 132, 797, 613, 563, 555, 516, 863, 516, 233, 204, 906, 518, 154, 179, 595, 809, 239, 281, 787, 991, 499, 361, 114, 631, 233, 133, 163, 277, 433, 710, 283, 751, 434, 917, 797, 383, 890};
static int test_output_a[] = {0, 0, 0, 1, 1, 1, 0, 0, 1, 0, 1, 0, 0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 0, 1, 1, 1, 0, 1, 1, 0, 0, 1, 1, 0};

