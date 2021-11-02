/* This is mostly code from the book "The Data Compression Book" by Mark
   Nelson, adapted to a more modern dialect of C and converted to use
   stdint.h data types in order to ensure it does not break when compiled
   for 64-bit, as well as changed to a different code style.
   The changes were done by Miran Grča (Battler).
   This is used for Little Big Adventure Compression Type 1, because this
   same code was also used for that algorithm (albeit with an assembly
   reimplementation of a number of the functions) by Adeline Software
   International. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lbatools/compress.h>


/************************** Start of LZSS.C ************************
 *
 * This is the LZSS module, which implements an LZ77 style compression
 * algorithm.  As iplemented here it uses a 12 bit index into the sliding
 * window, and a 4 bit length, which is adjusted to reflect phrase lengths
 * of between 2 and 17 bytes.
 *
 * Various constants used to define the compression parameters.  The
 * INDEX_BIT_COUNT tells how many bits we allocate to indices into the
 * text window.  This directly determines the WINDOW_SIZE.  The
 * LENGTH_BIT_COUNT tells how many bits we allocate for the length of
 * an encode phrase. This determines the size of the look ahead buffer.
 * The TREE_ROOT is a special node in the tree that always points to
 * the root node of the binary phrase tree.  END_OF_STREAM is a special
 * index used to flag the fact that the file has been completely
 * encoded, and there is no more data.  UNUSED is the null index for
 * the tree. MOD_WINDOW() is a macro used to perform arithmetic on tree
 * indices.
 *
 */
#define INDEX_BIT_COUNT		12
#define LENGTH_BIT_COUNT	4
#define WINDOW_SIZE		(1 << INDEX_BIT_COUNT)
#define RAW_LOOK_AHEAD_SIZE	(1 << LENGTH_BIT_COUNT)
#define BREAK_EVEN		(( 1 + INDEX_BIT_COUNT + LENGTH_BIT_COUNT) / 9)
#define LOOK_AHEAD_SIZE		(RAW_LOOK_AHEAD_SIZE + BREAK_EVEN)
#define TREE_ROOT		WINDOW_SIZE
#define UNUSED			-1
#define MOD_WINDOW(a)		(( a) & (WINDOW_SIZE - 1))


/*
 * These are the two global data structures used in this program.
 * The window[] array is exactly that, the window of previously seen
 * text, as well as the current look ahead text.  The tree[] structure
 * contains the binary tree of all of the strings in the window sorted
* in order.
*/
struct deftree {
    int32_t parent;
    int32_t smaller_child;
    int32_t larger_child;
};

static unsigned char	window[WINDOW_SIZE * 5];
static struct deftree	tree[WINDOW_SIZE + 2];

static int32_t		match_pos = 0;


/*
 * However, to make the tree really usable, a single phrase has to be
 * added to the tree so it has a root node.  That is done right here.
*/
static void
init_tree(int32_t r)
{
    int32_t i;

    for (i = 0; i <= WINDOW_SIZE; i++) {
	tree[i].parent = UNUSED;
	tree[i].larger_child = UNUSED;
	tree[i].smaller_child = UNUSED;
    }
    tree[TREE_ROOT].larger_child = r;
    tree[r].parent = TREE_ROOT;
}


/*
 * This routine is used when a node is being deleted.  The link to
 * its descendant is broken by pulling the descendant in to overlay
 * the existing link.
 */
static void
contract_node(int32_t old_node, int32_t new_node)
{
    tree[new_node].parent = tree[old_node].parent;
    if (tree[tree[old_node].parent].larger_child == old_node)
	tree[tree[old_node].parent].larger_child = new_node;
    else
	tree[tree[old_node].parent].smaller_child = new_node;
    tree[old_node].parent = UNUSED;
}


/*
 * This routine is also used when a node is being deleted.  However,
 * in this case, it is being replaced by a node that was not previously
 * in the tree.
 */
static void
replace_node(int32_t old_node, int32_t new_node)
{
    int32_t parent;

    parent = tree[old_node].parent;
    if (tree[parent].smaller_child == old_node)
	tree[parent].smaller_child = new_node;
    else
	tree[parent].larger_child = new_node;
    tree[new_node] = tree[old_node];
    if (tree[new_node].smaller_child != UNUSED)
	tree[tree[new_node].smaller_child].parent = new_node;
    if (tree[new_node].larger_child != UNUSED)
	tree[tree[new_node].larger_child].parent = new_node;
    tree[old_node].parent = UNUSED;
}


/*
 * This routine is used to find the next smallest node after the node
 * argument.  It assumes that the node has a smaller child.  We find
 * the next smallest child by going to the smaller_child node, then
 * going to the end of the larger_child descendant chain.
*/
static int
find_next_node(int32_t node)
{
    int32_t next;

    next = tree[node].smaller_child;
    while (tree[next].larger_child != UNUSED)
	next = tree[next].larger_child;
    return(next);
}


/*
 * This routine performs the classic binary tree deletion algorithm.
 * If the node to be deleted has a null link in either direction, we
 * just pull the non-null link up one to replace the existing link.
 * If both links exist, we instead delete the next link in order, which
 * is guaranteed to have a null link, then replace the node to be deleted
 * with the next link.
 */
static void
delete_string(int32_t p)
{
    int32_t replacement;

    if (tree[p].parent == UNUSED)
	return;
    if (tree[p].larger_child == UNUSED)
	contract_node(p, tree[p].smaller_child);
    else if (tree[p].smaller_child == UNUSED)
	contract_node(p, tree[p].larger_child);
    else {
	replacement = find_next_node(p);
	delete_string(replacement);
	replace_node(p, replacement);
    }
}


/*
 * This where most of the work done by the encoder takes place.  This
 * routine is responsible for adding the new node to the binary tree.
 * It also has to find the best match among all the existing nodes in
 * the tree, and return that to the calling routine.  To make matters
 * even more complicated, if the new_node has a duplicate in the tree,
 * the old_node is deleted, for reasons of efficiency.
 */
static int32_t
add_string(int32_t new_node)
{
    int32_t i, test_node, delta, match_length;
    int32_t *child;

    test_node = tree[TREE_ROOT].larger_child;
    match_length = 0;
    for ( ; ; ) {
	for ( i = 0 ; i < LOOK_AHEAD_SIZE ; i++ ) {
		delta = window[MOD_WINDOW(new_node + i)] - window[MOD_WINDOW(test_node + i)];
		if (delta != 0)
			break;
	}

	if (i >= match_length) {
		match_length = i;
		match_pos = test_node;

		if (match_length >= LOOK_AHEAD_SIZE) {
			replace_node(test_node, new_node);
			return(match_length);
		}
	}

	if (delta >= 0)
		child = &tree[test_node].larger_child;
	else
		child = &tree[test_node].smaller_child;
	if (*child == UNUSED) {
		*child = new_node;
		tree[new_node].parent = test_node;
		tree[new_node].larger_child = UNUSED;
		tree[new_node].smaller_child = UNUSED;
		return(match_length);
	}
	test_node = *child;
    }
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
compress_lzss(char *output, char *input, int32_t length)
{
    int32_t i, j = 0, k = 0;
    int32_t info = 0, look_ahead_bytes;
    int32_t replace_count, match_length = 0;
    int32_t count_bits = 0;
    int32_t new_node = 0;
    int16_t temp;
    char mask = 1;
    int32_t len = 0, save_length = length;

    match_pos = 0;

    for (i = 0; i < LOOK_AHEAD_SIZE; i++) {
	if (length == 0)
		break;
	window[new_node + i] = input[j++];
	length--;
    }

    look_ahead_bytes = i;
    init_tree(new_node);
    info = k++;

    if (++len >= save_length)
	return(save_length);

    output[info] = 0;

    while (look_ahead_bytes > 0) {
	if (match_length > look_ahead_bytes)
		match_length = look_ahead_bytes;

	if (match_length <= BREAK_EVEN) {
		replace_count = 1;
		output[info] |= mask;
		output[k++] = window[new_node];
		if (++len >= save_length)
		return( save_length );
	} else {
		if ((len = len + 2) >= save_length)
			return(save_length);

		temp = (short) ((MOD_WINDOW(new_node - match_pos - 1) << LENGTH_BIT_COUNT) |
			       (match_length - BREAK_EVEN - 1));
		output[k] = temp & 0xff;
		output[k + 1] = temp >> 8;

		k += 2;
		replace_count = match_length;
	}

	if (++count_bits == 8) {
		if (++len >= save_length)
			return save_length;

		info = k++;
		output[info] = 0;
		count_bits = 0;
		mask = 1;
	} else
		mask = (char) (mask << 1);

	for (i = 0; i < replace_count; i++) {
		delete_string(MOD_WINDOW(new_node + LOOK_AHEAD_SIZE));
		if (length == 0)
			look_ahead_bytes--;
		else {
			window[MOD_WINDOW(new_node + LOOK_AHEAD_SIZE)] = input[j++];
			length--;
		}

		new_node = MOD_WINDOW(new_node + 1);
		if (look_ahead_bytes)
			match_length = add_string(new_node);
	}
    }

    if (count_bits == 0)
	len--;

    return len;
}
/************************** End of LZSS.C *************************/
