/* This is an adaptation of the algorithm used to compress files used by
   Little Big Adventure 2. This is an implementation by Miran Grča
   (Battler).
   This is used for Little Big Adventure Compression Type 2. */
#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compress.h"


#define INDEX_BIT_COUNT		12
#define LENGTH_BIT_COUNT	4
#define RAW_LOOK_AHEAD_SIZE	(1 << LENGTH_BIT_COUNT)
#define MAX_OFFSET		((1 << INDEX_BIT_COUNT) + 1)
#define TREE_ROOT		(MAX_OFFSET)
#define UNUSED			-1
#define SMALLER			0
#define LARGER			1


typedef struct
{
    int32_t	children[2];
    int32_t	parent;
    int32_t	which_child;
} deftree_t;


static deftree_t	tree[MAX_OFFSET + 2];


static void
replace_parents(int32_t node)
{
    tree[tree[node + 1].children[SMALLER] + 1].parent = node;
    tree[tree[node + 1].children[LARGER] + 1].parent = node;
}


static void
replace_node(int32_t old_node, int32_t new_node)
{
    tree[new_node + 1] = tree[old_node + 1];

    replace_parents(new_node);

    tree[tree[old_node + 1].parent + 1].children[tree[old_node + 1].which_child] = new_node;
}


static void
update_parent(int32_t node, int32_t parent, int32_t which_child)
{
    tree[node + 1].parent = parent;
    tree[node + 1].which_child = which_child;
}


static int32_t
find_next_node(int32_t node)
{
    int32_t next = tree[node + 1].children[SMALLER];

    if (tree[next + 1].children[LARGER] == UNUSED)
	tree[node + 1].children[SMALLER] = tree[next + 1].children[SMALLER];
    else {
	while (tree[next + 1].children[LARGER] != UNUSED)
		next = tree[next + 1].children[LARGER];
	tree[tree[next + 1].parent + 1].children[LARGER] = tree[next + 1].children[SMALLER];
    }

    return(next);
}


static void
update_child(int32_t src_tree, int32_t which_child)
{
    if (tree[src_tree + 1].children[which_child] != UNUSED)
	update_parent(tree[src_tree + 1].children[which_child], tree[src_tree + 1].parent, tree[src_tree + 1].which_child);

    tree[tree[src_tree + 1].parent + 1].children[tree[src_tree + 1].which_child] = tree[src_tree + 1].children[which_child];
}


/*
 * This is the compression routine.  It has to first load up the look
 * ahead buffer, then go shorto the main compression loop.  The main loop
 * decides whether to output a single character or an index/length
 * token that defines a phrase.  Once the character or phrase has been
 * sent out, another loop has to run.  The second loop reads in new
 * characters, deletes the strings that are overwritten by the new
 * character, then adds the strings that are created by the new
 * character.
 */
int32_t
compress_lzmit(char *output, char *input, int32_t length)
{
    int32_t val, temp, src_off, out_len, offset_off, flag_bit, best_match = 1, best_node;
    int32_t cur_node, node, i, j, replacement, cmp_string, cur_string, src_tree, diff;

    memset(&(tree[1]), -1, (MAX_OFFSET + 1) * sizeof(deftree_t));

    src_off = flag_bit = offset_off = val = 0;
    best_match = out_len = 1;

    while ((best_match + src_off - 1) < length) {
	i = best_match;
	while (i > 0) {
		src_tree = src_off % MAX_OFFSET;

		if (tree[src_tree + 1].parent != UNUSED) {
			if ((tree[src_tree + 1].children[SMALLER] != UNUSED) && (tree[src_tree + 1].children[LARGER] != UNUSED)) {
				replacement = find_next_node(src_tree);
				update_parent(tree[replacement + 1].children[SMALLER], tree[replacement + 1].parent, tree[replacement + 1].which_child);
				replace_node(src_tree, replacement);
			} else
				update_child(src_tree, (tree[src_tree + 1].children[SMALLER] == UNUSED) ? LARGER : SMALLER);
		}

		tree[src_tree + 1].children[LARGER] = tree[src_tree + 1].children[SMALLER] = UNUSED;

		cur_node = (int32_t) tree[TREE_ROOT + 1].children[SMALLER];

		if (cur_node < 0) {
			best_match = best_node = 0;

			update_parent(src_tree, TREE_ROOT, 0);
			tree[TREE_ROOT + 1].children[SMALLER] = src_tree;
		} else {
			best_match = 2;

			while (1) {
				cur_string = src_off;
				cmp_string = cur_string - ((src_tree - cur_node + MAX_OFFSET) % MAX_OFFSET);
				node = cur_node;
				j = RAW_LOOK_AHEAD_SIZE + 2;
				cur_node = j - 1;

				do
					diff = (int32_t)(input[cur_string++]) - (int32_t)(input[cmp_string++]);
				while ((--j != 0) && (diff == 0));

				if ((j != 0) || (diff != 0)) {
					cur_node -= j;
					if (cur_node > best_match) {
						best_match = cur_node;
						best_node = node;
					}

					j = (diff >= 0) ? 1 : 0;
					cur_node = (int32_t) tree[node + 1].children[j];

					if (cur_node < 0) {
						update_parent(src_tree, node, j);
						tree[node + 1].children[j] = src_tree;
						break;
					}
				} else {
					replace_node(node, src_tree);
					tree[node + 1].parent = UNUSED;
					best_match = (RAW_LOOK_AHEAD_SIZE + 2);
					best_node = node;
					break;
				}
			}
		}

		if (--i > 0)
			src_off++;
	}

	if (out_len >= (length - RAW_LOOK_AHEAD_SIZE  - 1)) {
		out_len = -1;
		break;
	}

	val >>= 1;

	if ((best_match > 2) && (src_off + best_match <= length)) {
		temp = (best_match - 3) | (((src_off - best_node - 1 + MAX_OFFSET) % MAX_OFFSET) << LENGTH_BIT_COUNT);
		output[out_len] = temp & 0xff;
		output[out_len + 1] = temp >> 8;
		out_len += 2;
	} else {
		output[out_len++] = input[src_off];
		val |= 0x80;
		best_match = 1;
	}

	flag_bit++;
	if (flag_bit >= 8) {
		flag_bit = 0;
		output[offset_off] = val;
		offset_off = out_len;
		out_len++;
	}

	src_off++;
    }

    if (flag_bit == 0)
	out_len--;
    else if (flag_bit < 8)
	output[offset_off] = val >> (8 - flag_bit);

    return out_len;
}
