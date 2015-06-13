/* TIS-100 simulator - hthh, 2015

	Work in progress. Seems to run most programs accurately at the moment.

	Intended to be 100% compatible. This is intended as a fast cycle counter
	to augment the TIS-100 game, and make it easier to experiment with
	optimizations, or automate solution verification. It is not intended as
	a game.

	Define "SINGLE_STEP" for some terrible debug output.

	(This might be vulnerable to malicious input, I'm not sure.)

	License:
		This software is dedicated to the public domain by its author.
*/


#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <assert.h>

#define TIMEOUT_CYCLES  20000000

#define LINES_PER_NODE  15
#define STACK_NODE_SIZE 15
#define LINE_LENGTH     18
#define TOKENS_PER_LINE 4
#define MAX_USER_NODES  12
#define MAX_IN_NODES    4
#define MAX_OUT_NODES   4
#define MAX_STACK_NODES 2
#define MAX_IMAGE_NODES 1
#define MAX_SAVE_LINES  ((LINES_PER_NODE + 2) * MAX_USER_NODES)
#define ARENA_WIDTH     4
#define ARENA_HEIGHT    5
#define ARENA_SIZE      (ARENA_WIDTH * ARENA_HEIGHT)
#define ARENA_I(x,y)    ((y)*ARENA_WIDTH+(x))

#define IMAGE_WIDTH     30
#define IMAGE_HEIGHT    18
#define IMAGE_SIZE      (IMAGE_WIDTH * IMAGE_HEIGHT)

#define ARRAY_LENGTH(x)   (sizeof(x)/sizeof((x)[0]))

//#define SINGLE_STEP     /* display terrible debug output */
//#define DRAW_IMAGE      /* display image when updated */
#define OUTPUT_OUT_NODES  /* display output values */

enum opcode_numbers {
	OP_NONE = 0, OP_NOP, OP_SWP, OP_SAV, OP_NEG, OP_ADD, OP_SUB, OP_JRO,
	OP_MOV, OP_JMP, OP_JNZ, OP_JEZ, OP_JGZ, OP_JLZ,
};

enum register_numbers {
	R_NONE = 0, R_LEFT, R_RIGHT, R_UP, R_DOWN, R_ACC, R_NIL, R_ANY, R_LAST,
};

enum direction_numbers {
	D_LEFT, D_RIGHT, D_UP, D_DOWN,
	NUM_DIRECTIONS = 4,
};

enum write_states {
	WS_RUNNING, WS_WILL_BE_READABLE, WS_READABLE, WS_WILL_BE_RUNNING,
};

enum image_states {
	IS_READ_X, IS_READ_Y, IS_READ_PIXELS, IS_COMPLETED,
};

enum step_stages {
	S_RUN, S_COMMIT,
};

enum node_types {
	N_NONE = 0, N_IN, N_OUT, N_USER, N_STACK, N_IMAGE,
};

typedef unsigned char uint8;
typedef char token[LINE_LENGTH+1];

struct tokens {
	int num;
	token v[TOKENS_PER_LINE];
};

struct line {
	uint8 opcode;
	uint8 sreg;  // R_NONE if immediate
	uint8 dreg;  // or jump line number
	short immediate;
};

struct prelink_line {
	struct line l;
	char label[LINE_LENGTH];
	char target[LINE_LENGTH];
};

struct base_node {
	uint8 type;

	// node output
	uint8 write_state;
	uint8 write_bits;  // directions output is available
	int write_value;

	struct base_node *neighbors[NUM_DIRECTIONS];
};

struct user_node {
	struct base_node b;
	struct line lines[LINES_PER_NODE];
	int acc;
	int bak;
	uint8 ip;
	uint8 empty;
	uint8 last;
};

struct in_node {
	struct base_node b;
	int *values;
	int num_values;
	int i;
};

struct out_node {
	struct base_node b;
	struct arena *arena; // to signal completion
	int *values;
	int num_values;
	int i;
	const char *output_prefix;
};

struct image_node {
	struct base_node b;
	struct arena *arena; // to signal completion
	uint8 *solution;
	int wrong_pixels;
	int state;
	uint8 cursor_x, cursor_y;
	uint8 display[IMAGE_SIZE];
};

struct stack_node {
	struct base_node b;
	int used;
	int values[STACK_NODE_SIZE];
};

// full state
struct arena {
	int completed;  // number of completed (correct) output nodes
	int error;      // flag indicating incorrect output

	int user_node_count;
	int in_node_count;
	int out_node_count;
	int stack_node_count;
	int image_node_count;

	struct in_node in_nodes[MAX_IN_NODES];
	struct out_node out_nodes[MAX_OUT_NODES];
	struct user_node user_nodes[MAX_USER_NODES];
	struct stack_node stack_nodes[MAX_STACK_NODES];
	struct image_node image_nodes[MAX_IMAGE_NODES];

	struct base_node *nodes[ARENA_SIZE];
};


static int direction_opposite(int d) {
	return d ^ 1; // heh :)
}

static int op_num_arguments(int opcode_number) {
	if (opcode_number == OP_MOV)
		return 2;
	else if (opcode_number <= OP_NEG)
		return 0;
	return  1;
}

static int op_has_src(int opcode_number) {
	return (opcode_number >= OP_ADD && opcode_number <= OP_MOV);
}

static int op_operand_is_label(int opcode_number) {
	return (opcode_number >= OP_JMP && opcode_number <= OP_JLZ);
}

// this is the worst decision i've made all week
static int register_to_direction(int regnum) {
	assert(regnum >= R_LEFT && regnum <= R_DOWN);
	return regnum - R_LEFT;
}

static int parse_register(char *c) {
	if (!strcmp(c, "ACC"))   return R_ACC;
	if (!strcmp(c, "ANY"))   return R_ANY;
	if (!strcmp(c, "NIL"))   return R_NIL;
	if (!strcmp(c, "LEFT"))  return R_LEFT;
	if (!strcmp(c, "RIGHT")) return R_RIGHT;
	if (!strcmp(c, "UP"))    return R_UP;
	if (!strcmp(c, "DOWN"))  return R_DOWN;
	if (!strcmp(c, "LAST"))  return R_LAST;
	return R_NONE;
}

static int parse_opcode(char *name) {
	int len = strlen(name);
	if (len != 3)
		return OP_NONE; // invalid op

	// NB: this is little-endian/unaligned only
	switch (*(int*)name) {
		case 0x504F4E: return OP_NOP;
		case 0x505753: return OP_SWP;
		case 0x564153: return OP_SAV;
		case 0x47454E: return OP_NEG;
		case 0x444441: return OP_ADD;
		case 0x425553: return OP_SUB;
		case 0x4F524A: return OP_JRO;
		case 0x564F4D: return OP_MOV;
		case 0x504D4A: return OP_JMP;
		case 0x5A4E4A: return OP_JNZ;
		case 0x5A454A: return OP_JEZ;
		case 0x5A474A: return OP_JGZ;
		case 0x5A4C4A: return OP_JLZ;
		default:       return OP_NONE;
	}
}

static char *get_op_name(int opcode) {
	switch (opcode) {
		case OP_NONE: return "<none>";
		case OP_NOP:  return "NOP";
		case OP_SWP:  return "SWP";
		case OP_SAV:  return "SAV";
		case OP_NEG:  return "NEG";
		case OP_ADD:  return "ADD";
		case OP_SUB:  return "SUB";
		case OP_JRO:  return "JRO";
		case OP_MOV:  return "MOV";
		case OP_JMP:  return "JMP";
		case OP_JNZ:  return "JNZ";
		case OP_JEZ:  return "JEZ";
		case OP_JGZ:  return "JGZ";
		case OP_JLZ:  return "JLZ";
	}
	assert(0);
}

static char *get_reg_name(int r) {
	switch (r) {
		case R_NONE:  return "<none>";
		case R_ACC:   return "ACC";
		case R_NIL:   return "NIL";
		case R_LEFT:  return "LEFT";
		case R_RIGHT: return "RIGHT";
		case R_UP:    return "UP";
		case R_DOWN:  return "DOWN";
		case R_ANY:   return "ANY";
		case R_LAST:  return "LAST";
	}
	assert(0);
}


// utils

static short tis100_clamp(int v) {
	if (v >  999) return  999;
	if (v < -999) return -999;
	return v;
}

static int is_tis100_separator(char c) {
	// amusingly, in TIS-100, commas are optional and '!' is whitespace
	return (c == ' ' || c == ',' || c == '!');
}

static int is_tis100_terminator(char c) {
	return (c == '\0' || c == '#' || c == '\r' || c == '\n');
}


// tokenizing

static int tokenize_line(char *line, struct tokens *t) {
	char *p = line;
	char *o;

	assert(strlen(line) <= LINE_LENGTH);

	t->num = 0;

	// skip leading whitespace
	while (!is_tis100_terminator(*p)) {
		while (is_tis100_separator(*p))
			p++;

		if (is_tis100_terminator(*p))
			return 1;

		if (t->num >= TOKENS_PER_LINE) {
			printf("error: too many tokens to parse line\n");
			return 0;
		}

		o = t->v[t->num++];
		while (!is_tis100_terminator(*p) && !is_tis100_separator(*p)) {
			*o++ = *p;
			if (*p++ == ':' && t->num == 1)
				break;
		}
		*o++ = '\0';
	}

	return 1;
}


// parsing

static int parse_line(char *line, struct prelink_line *out) {
	struct tokens tokens;
	if (!tokenize_line(line, &tokens))
		return 0;

	memset(out, 0, sizeof *out);

	if (tokens.num == 0)
		return 1; // successful parse

	int token_index = 0;
	char *first = tokens.v[0];
	int len = strlen(first);
	if (first[len-1] == ':') {
		if (len == 1) {
			printf("error: empty label is invalid\n");
			return 0;
		}
		
		first[len-1] = '\0';
		strcpy(out->label, first);
		token_index = 1;
	}

	if (token_index < tokens.num) {
		char *op = tokens.v[token_index++];
		out->l.opcode = parse_opcode(op);
		if (!out->l.opcode) {
			printf("error: invalid opcode '%s'\n", op);
			return 0;
		}

		int num_operands = op_num_arguments(out->l.opcode);
		if (num_operands != tokens.num - token_index) {
			printf("error: '%s' expects %d operands\n", op, num_operands);
			return 0;
		}

		if (num_operands) {
			if (op_operand_is_label(out->l.opcode)) {
				strcpy(out->target, tokens.v[token_index]);
			} else {
				// validate src
				char *src = tokens.v[token_index++];
				out->l.sreg = parse_register(src);
				if (out->l.sreg == R_NONE) {
					char *e = NULL;
					long long v = strtoll(src, &e, 10);
					if (*src == '+' || *e || v < INT_MIN || v > INT_MAX) {
						printf("error: invalid operand '%s'\n", src);
						return 0;
					}
					out->l.immediate = tis100_clamp(v);
				}

				if (num_operands == 2) {
					char *dst = tokens.v[token_index];
					out->l.dreg = parse_register(dst);
					if (out->l.dreg == R_NONE) {
						printf("error: invalid register '%s'\n", dst);
						return 0;
					}
				}
			}
		}
	}

	return 1;
}


// link labels and initialize user nodes

static int link_node(int count, struct prelink_line *line, struct user_node *node) {
	struct line *out = node->lines;
	int i, j;

	// quadratic time techniques because n < 16
	for (i = 1; i < count; i++) {
		if (line[i].label[0]) {
			for (j = 0; j < i; j++) {
				if (!strcmp(line[i].label, line[j].label)) {
					printf("error: duplicate label '%s'\n", line[i].label);
					return 0;
				}
			}
		}
	}

	for (i = 0; i < count; i++) {
		out[i] = line[i].l;
		if (line[i].target[0]) {
			out[i].dreg = 0xFF;
			for (j = 0; j < count; j++) {
				if (!strcmp(line[i].target, line[j].label)) {
					out[i].dreg = j;
					break;
				}
			}
			if (out[i].dreg == 0xFF) {
				printf("error: undefined label '%s'\n", line[i].target);
				return 0;
			}
		}
	}

	node->empty = 1;
	for (i = 0; i < count; i++) {
		if (out[i].opcode) {
			node->ip = i;
			node->empty = 0;
			break;
		}
	}

	return 1;
}


static int load_user_nodes(char *save_data, struct user_node *user_nodes, int *count) {
	char *lines[MAX_SAVE_LINES];
	int num_save_lines = 0;
	char *p = save_data;

	// trim leading whitespace
	while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
		p++;

	while (*p) {
		// trim indentation
		while (*p == ' '  || *p == '\t')
			p++;

		char *e = p;
		while (*e != '\r' && *e != '\n' && *e != '\0')
			e++;

		int l = e - p;
		if (l > LINE_LENGTH) {
			printf("error: invalid save file: line too long\n");
			return 0;
		}

		if (num_save_lines >= MAX_SAVE_LINES) {
			printf("error: invalid save file: too many lines\n");
			return 0;
		}

		lines[num_save_lines++] = p;

		p = e;
		if (*p == '\r')
			p++;
		if (*p == '\n')
			p++;
		*e = '\0';
	}

	if (num_save_lines == 0 || strcmp(lines[0], "@0")) {
		printf("error: invalid save file: should start with '@0'\n");
		return 0;
	}

	struct prelink_line node_lines[LINES_PER_NODE];
	int n, node_line = 0, node = 0;

	memset(user_nodes, 0, MAX_USER_NODES * sizeof *user_nodes);

	for (n = 1; n < num_save_lines; n++) {
		if (lines[n][0] == '@') {
			// TODO: check the number is as expected
			if (!link_node(node_line, node_lines, &user_nodes[node]))
				return 0;
			node++;
			node_line = 0;
			if (node >= MAX_USER_NODES) {
				printf("error: invalid save file: too many nodes\n");
				return 0;
			}
			continue;
		}

		if (node_line >= LINES_PER_NODE) {
			if (!lines[n][0])
				continue;
			printf("error: invalid save file: too many lines in node\n");
			return 0;
		}

		if (!parse_line(lines[n], &node_lines[node_line++]))
			return 0;
	}

	if (!link_node(node_line, node_lines, &user_nodes[node]))
		return 0;
	node++;

	*count = node;

	return 1;
}


// initialize the arena - sets node neighbours and x/y

static void link_arena(struct base_node **arena) {
	int x, y;
	for (y = 0; y < ARENA_HEIGHT; y++) {
		for (x = 0; x < ARENA_WIDTH; x++) {
			struct base_node *this = arena[ARENA_I(x, y)];
			if (!this)
				continue;
			if (x > 0) {
				struct base_node *left = arena[ARENA_I(x-1, y)];
				if (left) {
					this->neighbors[D_LEFT] = left;
					left->neighbors[D_RIGHT] = this;
				}
			}

			if (y > 0) {
				struct base_node *up = arena[ARENA_I(x, y-1)];
				if (up) {
					this->neighbors[D_UP] = up;
					up->neighbors[D_DOWN] = this;
				}
			}
		}
	}
}

static int node_consume(struct base_node *n, int direction, int *value) {
	if (n->write_state != WS_READABLE || !(n->write_bits & (1 << direction))) {
		*value = 0;
		return 0;
	}

	*value = n->write_value;
	n->write_state = WS_WILL_BE_RUNNING;
	return 1;
}

static int node_read_from_direction(struct base_node *n, int direction, int *value) {
	struct base_node *other = n->neighbors[direction];
	if (other)
		return node_consume(other, direction_opposite(direction), value);
	*value = 0;
	return 0;
}


//
// USER NODE
//

static int user_node_do_read_from_register(struct user_node *n, int reg, int *value) {
	switch (reg) {
		case R_NIL:
			*value = 0;
			return 1;

		case R_ACC:
			*value = n->acc;
			return 1;

		case R_LAST:
			if (!n->last) {
				*value = 0;
				return 1;
			}
			*value = 0;
			return node_read_from_direction(&n->b, n->last - 1, value);

		case R_UP:
		case R_DOWN:
		case R_LEFT:
		case R_RIGHT:
			return node_read_from_direction(&n->b, register_to_direction(reg), value);

		case R_ANY: {
			// TODO: this is probably inaccurate
			int i;
			for (i = 0; i < NUM_DIRECTIONS; i++) {
				if (node_read_from_direction(&n->b, i, value)) {
					n->last = i + 1;
					return 1;
				}
			}
			return 0;
		}
	}

	*value = 0;
	return 0;
}

static void user_node_next_instruction(struct user_node *n) {
	do {
		n->ip++;

		if (n->ip >= LINES_PER_NODE)
			n->ip = 0;
	} while (n->lines[n->ip].opcode == OP_NONE);
}

static void user_node_prev_instruction(struct user_node *n) {
	do {
		n->ip--;

		if (n->ip < 0)
			n->ip = LINES_PER_NODE;
	} while (n->lines[n->ip].opcode == OP_NONE);
}

static void user_node_step(struct user_node *n, int step) {
	if (step == S_COMMIT) {
		if (n->b.write_state == WS_WILL_BE_READABLE) {
			n->b.write_state = WS_READABLE;
		} else if (n->b.write_state == WS_WILL_BE_RUNNING) {
			n->b.write_state = WS_RUNNING;
			user_node_next_instruction(n);
		}
	} else if (step == S_RUN) {
		if (n->b.write_state != WS_RUNNING)
			return; // nothing to do - another node will need to unblock this one

		if (n->lines[n->ip].opcode == OP_NONE)
			user_node_next_instruction(n); // should only happen at start-up

		int src_value = 0;
		struct line *l = &n->lines[n->ip];

		if (op_has_src(l->opcode)) {
			if (l->sreg != R_NONE) {
				if (!user_node_do_read_from_register(n, l->sreg, &src_value))
					return; // abort - retry next cycle
			} else
				src_value = l->immediate;
		}

		int branch = 0;
		switch (l->opcode) {
			case OP_ADD: n->acc = tis100_clamp(n->acc + src_value); break;
			case OP_SUB: n->acc = tis100_clamp(n->acc - src_value); break;
			case OP_SAV: n->bak = n->acc; break;
			case OP_NEG: n->acc = -n->acc; break;
			case OP_NOP: break;

			case OP_SWP: {
				int tmp = n->bak;
				n->bak = n->acc;
				n->acc = tmp;
			} break;

			case OP_JMP: branch = 1; break;
			case OP_JEZ: branch = (n->acc == 0); break;
			case OP_JNZ: branch = (n->acc != 0); break;
			case OP_JGZ: branch = (n->acc  > 0); break;
			case OP_JLZ: branch = (n->acc  < 0); break;

			case OP_JRO: {
				int old_ip, i;
				// emulate weird bounding behaviour
				if (src_value < 0) {
					for (i = 0; i > src_value; i--) {
						old_ip = n->ip;
						user_node_prev_instruction(n);
						if (n->ip > old_ip) {
							n->ip = old_ip;
							return;
						}
					}
				} else {
					for (i = 0; i < src_value; i++) {
						old_ip = n->ip;
						user_node_next_instruction(n);
						if (n->ip < old_ip) {
							n->ip = old_ip;
							return;
						}
					}
				}
				return; // do not advance
			} break;

			case OP_MOV: {
				n->b.write_bits = 0;
				switch (l->dreg) {
					case R_LEFT:
					case R_RIGHT:
					case R_UP:
					case R_DOWN:
						n->b.write_bits = 1 << register_to_direction(l->dreg);
						break;

					case R_ANY:
						n->b.write_bits = 0x0F;
						break;

					case R_LAST:
						if (n->last)
							n->b.write_bits = 1 << (n->last - 1);
						break;

					case R_ACC:
						n->acc = src_value;
						break;

					case R_NIL:
						break;

					default: assert(0);
				}

				if (n->b.write_bits) {
					n->b.write_state = WS_WILL_BE_READABLE;
					n->b.write_value = src_value;
					return; // do not increment
				}
			} break;

			default: assert(0);
		}

		// JRO deals with its own problems
		if (branch) {
			n->ip = l->dreg;
			if (n->lines[n->ip].opcode == OP_NONE)
				user_node_next_instruction(n);
			return;
		}

		// increment
		user_node_next_instruction(n);
	}
}

//
// IN NODE
//

static void in_node_step(struct in_node *n, int step) {
	if (step == S_COMMIT) {
		if (n->b.write_state == WS_WILL_BE_READABLE)
			n->b.write_state = WS_READABLE;
		else if (n->b.write_state == WS_WILL_BE_RUNNING)
			n->b.write_state = WS_RUNNING;
	} else if (step == S_RUN) {
		if (n->b.write_state != WS_RUNNING || n->i >= n->num_values)
			return; // nothing to do

		n->b.write_state = WS_WILL_BE_READABLE;
		n->b.write_value = n->values[n->i++];
		n->b.write_bits = 0x0F;
	}
}


//
// OUT NODE
//

static void out_node_step(struct out_node *n, int step) {
	int value;
	int expected;

	if (step == S_RUN && n->i < n->num_values && node_read_from_direction(&n->b, D_UP, &value)) {
		expected = n->values[n->i++];
		if (expected == value) {
			if (n->i == n->num_values)
				n->arena->completed++;
		} else
			n->arena->error = 1;
#ifdef OUTPUT_OUT_NODES
		if (n->output_prefix)
			printf("%s: ", n->output_prefix);
		printf("%c %3d %3d\n", expected == value ? ' ' : 'X', expected, value);
#endif
	}
}


//
// IMAGE NODE
//

static void image_node_step(struct image_node *n, int step) {
	int value;

	if (step == S_RUN && node_read_from_direction(&n->b, D_UP, &value)) {
		if (value < 0 && n->state != IS_COMPLETED) {
			n->state = IS_READ_X;
			return;
		}

		switch (n->state) {
			case IS_READ_X: {
				n->cursor_x = value;
				n->state = IS_READ_Y;
			} break;

			case IS_READ_Y: {
				n->cursor_y = value;
				n->state = IS_READ_PIXELS;
			} break;

			case IS_READ_PIXELS: {
				if (n->cursor_x < IMAGE_WIDTH && n->cursor_y < IMAGE_HEIGHT && value <= 4) {
					int i = IMAGE_WIDTH * n->cursor_y + n->cursor_x;
					if (value != n->display[i]) {
						if (n->display[i] == n->solution[i]) {
							n->wrong_pixels++; // now incorrect
						} else if (value == n->solution[i]) {
							n->wrong_pixels--; // now correct
							if (n->wrong_pixels == 0) {
								n->arena->completed++;
								n->state = IS_COMPLETED;
							}
						}
						n->display[i] = value;
					}
					n->cursor_x++;
				}
			} break;
		}

#ifdef DRAW_IMAGE
		int x, y;
		printf("image:\n");
		for (y = 0; y < IMAGE_HEIGHT; y++) {
			for (x = 0; x < IMAGE_WIDTH; x++)
				putchar(" ?!WR"[n->display[IMAGE_WIDTH * y + x]]);
			putchar('\n');
		}
#endif
	}
}

static void image_node_set_solution(struct image_node *node, uint8 *solution) {
	int i;
	node->solution = solution;
	node->wrong_pixels = 0;
	for (i = 0; i < IMAGE_SIZE; i++)
		if (solution[i])
			node->wrong_pixels++;
}


//
// STACK NODE
//

static void stack_node_step(struct stack_node *n, int step) {
	if (step == S_RUN) {
		int value, d;

		if (n->b.write_state == WS_WILL_BE_RUNNING) {
			assert(n->used > 0);
			n->values[--n->used] = 0;
		}

		for (d = 0; n->used < STACK_NODE_SIZE && d < NUM_DIRECTIONS; d++)
			if (node_read_from_direction(&n->b, d, &value))
				n->values[n->used++] = value;
	} else if (step == S_COMMIT) {
		if (n->used) {
			n->b.write_state = WS_READABLE;
			n->b.write_bits = 0x0F;
			n->b.write_value = n->values[n->used-1];
		} else {
			n->b.write_state = WS_RUNNING;
		}
	}
}


//
// DEBUG OUTPUT FUNCTIONS
//

static void sdump_line(struct line *line, char *p) {
	*p = '\0';
	if (line->opcode != OP_NONE) {
		p += sprintf(p, "%s", get_op_name(line->opcode));
		if (op_num_arguments(line->opcode)) {
			if (op_operand_is_label(line->opcode))
				p += sprintf(p, " L%d", line->dreg);
			else if (line->sreg)
				p += sprintf(p, " %s", get_reg_name(line->sreg));
			else
				p += sprintf(p, " %d", line->immediate);

			if (line->opcode == OP_MOV)
				p += sprintf(p, ", %s", get_reg_name(line->dreg));
		}
	}
}

static void dump_arena(struct base_node **arena) {
#define DASHES "+-----------------------------------------------------------------------------------+"
	int x, y, i;
	puts(DASHES);
	for (y = 1; y < 4; y++) {
		for (i = -1; i < LINES_PER_NODE; i++) {
			printf("|");
			for (x = 0; x < 4; x++) {
				struct base_node *n = arena[ARENA_I(x,y)];
				const char *v = "";
				char buffer[64];
				char c = ' ';
				if (i == -1) {
					if (n && n->type == N_USER) {
						struct user_node *un = (struct user_node *) n;
						char *p = buffer + sprintf(buffer, "#%3d|%3d|", un->acc, un->bak);
						if (n->write_state == WS_READABLE)
							sprintf(p, "%3d", n->write_value);
						v = buffer;
					}
				} else {
					if (n && n->type == N_USER) {
						struct user_node *un = (struct user_node *) n;
						if (!un->empty && un->ip == i)
							c = (n->write_state == WS_READABLE ? '*' : '>');
						if (un->lines[i].opcode) {
							sdump_line(&un->lines[i], buffer);
							v = buffer;
						}
					}
				}
				printf("%c%-18s |", c, v);
			}
			printf("\n");
		}
		puts(DASHES);
	}
}

static int load_user_nodes_filename(const char *filename, struct arena *arena) {
	char save_data[4096];

	FILE *f = fopen(filename, "rb");
	if (!f) {
		printf("error: could not open save file '%s'\n", filename);
		return 0;
	}
	int size = fread(save_data, 1, sizeof save_data - 1, f);
	save_data[size] = '\0';

	if (size == sizeof save_data - 1) {
		printf("error: invalid save file: too large\n");
		return 0;
	}
	if (!load_user_nodes(save_data, arena->user_nodes, &arena->user_node_count))
		return 0;

	return 1;
}

static int arena_set_layout(struct arena *arena, int *layout) {
	int i, user_node_index = 0;

	for (i = 0; i < ARENA_SIZE; i++) {
		switch (layout[i]) {
			case N_NONE: break; // NULL value is already placed

			case N_IN: {
				struct in_node *n = &arena->in_nodes[arena->in_node_count++];
				arena->nodes[i] = &n->b;
			} break;

			case N_OUT: {
				struct out_node *n = &arena->out_nodes[arena->out_node_count++];
				n->arena = arena;
				arena->nodes[i] = &n->b;
			} break;

			case N_USER: {
				if (user_node_index >= arena->user_node_count) {
					printf("error: not enough nodes in save file\n");
					return 0;
				}
				struct user_node *n = &arena->user_nodes[user_node_index++];

				// an empty node is equivalent to a "none" node, but much
				// more expensive to simulate
				if (!n->empty)
					arena->nodes[i] = &n->b;
			} break;

			case N_STACK: {
				struct stack_node *n = &arena->stack_nodes[arena->stack_node_count++];
				arena->nodes[i] = &n->b;
			} break;

			case N_IMAGE: {
				struct image_node *n = &arena->image_nodes[arena->image_node_count++];
				n->arena = arena;
				arena->nodes[i] = &n->b;
			} break;
		}

		if (arena->nodes[i])
			arena->nodes[i]->type = layout[i];
	}

	if (user_node_index != arena->user_node_count) {
		printf("error: too many nodes in save file\n");
		return 0;
	}

	return 1;
}

//
// MAIN TEST / SIMULATION LOOP
//

#include "puzzles/list.inc"

#define USAGE "usage: sim <save file path> [puzzle number]\n" PUZZLE_LIST

int main(int argc, char **argv) {
	if (argc != 2 && argc != 3) {
		printf(USAGE);
		return -1;
	}
	char *path = argv[1];

	struct arena arena;
	memset(&arena, 0, sizeof arena);

	if (!load_user_nodes_filename(path, &arena))
		return -1;

	int puzzle = -1;
	if (argc == 3)
		puzzle = atoi(argv[2]);
	else {
		char *last = path + strlen(path);
		while (last > path && last[-1] != '/' && last[-1] != '\\')
			last--;
		if (!strncmp("UNKNOWN", last, 7))
			puzzle = 3;
		else if (*last >= '0' && *last <= '9')
			puzzle = atoi(last);
	}

	switch (puzzle) {

		// hacky includes for puzzles
#include "puzzles/all.inc"

		default: {
			printf(USAGE);
			return -1;
		} break;
	}

	link_arena(arena.nodes);

	// run!
	int cycle, stage, i, j;
	int required = arena.image_node_count + arena.out_node_count;
	for (cycle = 0; !arena.error && arena.completed < required && cycle < TIMEOUT_CYCLES; cycle++) {
#ifdef SINGLE_STEP
		printf("\n%d: \n", cycle);
		dump_arena(arena.nodes);
		printf("single-step: press enter...\n");
		getchar();
#endif

		// simulate all nodes
		for (stage = 0; stage < 2; stage++) {
			for (i = 0; i < arena.user_node_count; i++)
				if (!arena.user_nodes[i].empty)
					user_node_step(&arena.user_nodes[i], stage);

			for (i = 0; i < arena.in_node_count; i++)
				in_node_step(&arena.in_nodes[i], stage);

			for (i = 0; i < arena.out_node_count; i++)
				out_node_step(&arena.out_nodes[i], stage);

			for (i = 0; i < arena.image_node_count; i++)
				image_node_step(&arena.image_nodes[i], stage);

			// stack node must be last
			for (i = 0; i < arena.stack_node_count; i++)
				stack_node_step(&arena.stack_nodes[i], stage);
		}
	}

	if (arena.error) {
		printf("done: invalid output\n");
	} else if (arena.completed == required) {
		int nodes = 0, instructions = 0;
		for (i = 0; i < arena.user_node_count; i++) {
			if (!arena.user_nodes[i].empty) {
				nodes++;
				for (j = 0; j < LINES_PER_NODE; j++)
					if (arena.user_nodes[i].lines[j].opcode != OP_NONE)
						instructions++;
			}
		}
		printf("done: %d cycles, %d nodes, %d instructions\n", cycle, nodes, instructions);
	} else {
		printf("done: timeout\n");
	}

	return 0;
}
