#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compress.h"


#define ENTRY_NULL	0	/* NULL entry, to be save as an offset of 0x00000000. */
#define ENTRY_NORMAL	1	/* Normal entry. */
#define ENTRY_POINTER	2	/* Pointer to another entry (use .parent). */
#define ENTRY_EOF	3	/* End of file (entry points to the end of file). */

#define COMPRESS_NONE	0
#define COMPRESS_LZSS	1
#define COMPRESS_LZMIT	2

#define UNUSED		-1

#define NUM_ENTRIES	65536
#define NUM_CHILDREN	65536
#define NUM_OFFSETS	65536


typedef struct
{
    int32_t	dec_size, comp_size;
    int16_t	comp_type, entry_type;
    int32_t	offset;
    int32_t	tbl_next_off, size_next_off;
    uint8_t	*desc, *data;
} hqr_child_t;


typedef struct
{
    int32_t	parent;			/* Parent for pointer. */
    int32_t	dec_size, comp_size;
    int16_t	comp_type, entry_type;
    int32_t	offset;
    int32_t	tbl_next_off, size_next_off;
    uint8_t	*desc, *data;

    hqr_child_t	*children;
    int32_t	children_no;
} hqr_entry_t;


typedef struct
{
    int32_t	offset, entry;
} hqr_offset_t;


static hqr_entry_t	*hqr_entries;
static hqr_offset_t	*hqr_offsets;
static int32_t		hqr_entries_no, hqr_offsets_no;
static int32_t		first_entry, last_entry, next_entry, next_offset;
static int32_t		first_offset, last_offset;
static FILE *		hqr_file = NULL;
static char *		comp_types[3] = { "None", "LZSS", "LZMIT" };
static char *		entry_types[8] = { "Null", "Normal", "Pointer", "EOF" };


static void
hqr_child_init(hqr_child_t *hc)
{
    hc->entry_type = ENTRY_NULL;
    hc->comp_type = COMPRESS_NONE;
    hc->dec_size = hc->comp_size = 0;
    hc->desc = hc->data = NULL;
    hc->tbl_next_off = hc->size_next_off = 0x00000000;
    hc->offset = 0x00000000;
}


static void
hqr_child_add(int32_t i, hqr_child_t hc)
{
    if (hqr_entries_no >= NUM_CHILDREN) {
	printf("ASSERT: Children exhausted for entry %i\n", i);
	return;
    }

    if (hqr_entries[i].children_no == 0)
	hqr_entries[i].children = (hqr_child_t *) malloc((hqr_entries[i].children_no + 1) * sizeof(hqr_child_t));
    else
	hqr_entries[i].children = (hqr_child_t *) realloc(hqr_entries[i].children,
							  (hqr_entries[i].children_no + 1) * sizeof(hqr_child_t));

    hqr_entries[i].children[hqr_entries[i].children_no] = hc;
    hqr_entries[i].children_no++;
}


static int32_t
hqr_next_child(int32_t i, hqr_child_t hc)
{
    hqr_child_add(i, hc);

    return (hqr_entries[i].children_no - 1);
}


static void
hqr_entry_init(hqr_entry_t *he)
{
    he->entry_type = ENTRY_NULL;
    he->comp_type = COMPRESS_NONE;
    he->dec_size = he->comp_size = 0;
    he->parent = UNUSED;
    he->desc = he->data = NULL;
    he->tbl_next_off = he->size_next_off = 0x00000000;
    he->offset = 0x00000000;

    he->children = NULL;
    he->children_no = 0;
}


static void
hqr_entry_add(hqr_entry_t he)
{
    if (hqr_entries_no >= NUM_ENTRIES) {
	printf("ASSERT: Entries exhausted\n");
	return;
    }

    if (hqr_entries_no == 0)
	hqr_entries = (hqr_entry_t *) malloc((hqr_entries_no + 1) * sizeof(hqr_entry_t));
    else
	hqr_entries = (hqr_entry_t *) realloc(hqr_entries, (hqr_entries_no + 1) * sizeof(hqr_entry_t));

    hqr_entries[hqr_entries_no] = he;
    hqr_entries_no++;
}


static int32_t
hqr_next_entry(hqr_entry_t he)
{
    hqr_entry_add(he);

    return (hqr_entries_no - 1);
}


static void
hqr_offset_init(hqr_offset_t *ho)
{
    ho->offset = 0x00000000;
    ho->entry = UNUSED;
}


static void
hqr_offset_add(hqr_offset_t ho)
{
    if (hqr_offsets_no >= NUM_OFFSETS) {
	printf("ASSERT: Offsets exhausted\n");
	return;
    }

    if (hqr_offsets_no == 0)
	hqr_offsets = (hqr_offset_t *) malloc((hqr_offsets_no + 1) * sizeof(hqr_offset_t));
    else
	hqr_offsets = (hqr_offset_t *) realloc(hqr_offsets, (hqr_offsets_no + 1) * sizeof(hqr_offset_t));

    hqr_offsets[hqr_offsets_no] = ho;
    hqr_offsets_no++;
}


static int32_t
hqr_next_offset(hqr_offset_t ho)
{
    hqr_offset_add(ho);

    return (hqr_offsets_no - 1);
}


static int32_t
hqr_find_same_offset(int32_t offset)
{
    int32_t i, ret = UNUSED;

    for (i = 0; i < hqr_offsets_no; i++) {
	if (hqr_offsets[i].offset == offset) {
		ret = i;
		break;
	}
    }

    return ret;
}


static void
hqr_init(void)
{
    hqr_entries_no = hqr_offsets_no = 0;

    hqr_entries = NULL;
    hqr_offsets = NULL;

    first_entry = last_entry = UNUSED;
    first_offset = last_offset = UNUSED;
    next_entry = next_offset = 0;
    hqr_file = NULL;
}


#if 0
static void
hqr_replace_parent(int32_t old, int32_t new)
{
    int32_t i;

    for (i = 0; i < hqr_entries_no; i++) {
	if (hqr_entries[i].parent == old)
		hqr_entries[i].parent = new;
	else if (hqr_entries[i].parent == new)
		hqr_entries[i].parent = old;
    }
}
#endif


static int32_t
hqr_find_next_offset(int32_t offset)
{
    int32_t i, ret = UNUSED;

    for (i = 0; i < hqr_offsets_no; i++) {
	if (hqr_offsets[i].offset > offset) {
		ret = hqr_offsets[i].offset;
		break;
	}
    }

    return ret;
}


#if 0
static int32_t
hqr_find_parent(int32_t offset)
{
    int32_t i, ret = UNUSED;

    for (i = 0; i < hqr_entries_no; i++) {
	if (hqr_entries[i].offset == offset) {
		ret = hqr_entries[i].offset;
		break;
	}
    }

    return ret;
}
#endif


static void
hqr_load(void)
{
    int32_t i, j;
    int32_t file_len, offset;
    int32_t next_e, next_c;
    int32_t prev_o, next_o;
    hqr_entry_t he;
    hqr_offset_t ho;
    hqr_child_t hc;

    if (hqr_file == NULL)
	return;

    fseek(hqr_file, 0, SEEK_END);
    file_len = ftell(hqr_file);
    if (file_len < 4)
	return;

    /* Pass 1: Load the main entries. */
    i = 0;
    while (1) {
	fseek(hqr_file, i << 2, SEEK_SET);
	fread(&offset, 1, 4, hqr_file);
	prev_o = hqr_find_same_offset(offset);
	if (offset != 0x00000000) {
		/* Not a NULL entry. */
		if (prev_o == UNUSED) {
			/* New offset, so this is a normal entry. */
			hqr_offset_init(&ho);
			ho.offset = offset;
			ho.entry = i;
			next_o = hqr_next_offset(ho);
			if (next_o == UNUSED) {
				printf("ASSERT: Failed to allocate next offset\n");
				return;
			}

			hqr_entry_init(&he);
			fseek(hqr_file, offset, SEEK_SET);
			if (offset == file_len) {
				/* EOF entry. */
				he.entry_type = ENTRY_EOF;
				printf("%05i (%08X): EOF\n", i, offset);
				break;
			} else {
				/* Normal entry. */
				he.entry_type = ENTRY_NORMAL;
				he.offset = offset;
				fread(&he.dec_size, 1, 4, hqr_file);
				fread(&he.comp_size, 1, 4, hqr_file);
				fread(&he.comp_type, 1, 2, hqr_file);
				if ((he.comp_type < 0) || (he.comp_type > 2))
					printf("ASSERT: Invalid compression type %i for entry %i\n", hqr_entries[next_e].comp_type, next_e);
				else if ((he.comp_type == 0) && (he.dec_size != he.comp_size))
					printf("ASSERT: Size mismatch in a non-compressed entry\n");
				he.data = (uint8_t *) malloc(he.comp_size);
				fread(he.data, 1, he.comp_size, hqr_file);
				printf("%05i (%08X): NORMAL: %08X, %08X, %5s\n", i, offset, he.dec_size, he.comp_size,
				       comp_types[he.comp_type]);
			}
		} else {
			/* Pointer to a previous entry. */
			he.entry_type = ENTRY_POINTER;
			he.offset = offset;
			he.parent = hqr_offsets[prev_o].entry;
			printf("%05i (%08X): PTR   : %05i\n", i, offset, he.parent);
		}
	} else {
		/* NULL entry. */
		he.entry_type = ENTRY_NULL;
		printf("%05i (%08X): NULL\n", i, offset);
	}

	next_e = hqr_next_entry(he);
	if (next_e == UNUSED) {
		printf("ASSERT: Failed to allocate next entry\n");
		return;
	}

	i++;

	if ((i << 2) == hqr_offsets[0].offset) {
		printf("ASSERT: No EOF entry in HQR file\n");
		break;
	}
    }

    /* Pass 2: Load the children. */
    for (i = 0; i < hqr_entries_no; i++) {
	if (hqr_entries[i].entry_type == ENTRY_NORMAL) {
		hqr_entries[i].tbl_next_off = hqr_find_next_offset(hqr_entries[i].offset);
		if (hqr_entries[i].tbl_next_off == UNUSED) {
			printf("ASSERT: hqr_entries[i].tbl_next_off = UNUSED\n");
			return;
		}
		hqr_entries[i].size_next_off = hqr_entries[i].offset + hqr_entries[i].comp_size + 10;

		if (hqr_entries[i].tbl_next_off != hqr_entries[i].size_next_off) {
			j = 0;
			he = hqr_entries[i];
			hqr_child_init(&hc);
			while (1) {
				hc.entry_type = he.entry_type;
				if (j == 0)		/* Use parent size_next_off. */
					hc.offset = he.size_next_off;
				else			/* Use previous child size_next_off. */
					hc.offset = hc.size_next_off;
				fseek(hqr_file, hc.offset, SEEK_SET);
				hc.tbl_next_off = he.tbl_next_off;
				fread(&hc.dec_size, 1, 4, hqr_file);
				fread(&hc.comp_size, 1, 4, hqr_file);
				hc.size_next_off = hc.offset + hc.comp_size + 10;
				fread(&hc.comp_type, 1, 2, hqr_file);
				if ((hc.comp_type < 0) || (hc.comp_type > 2))
					printf("ASSERT: Invalid compression type %i for entry %i.%i\n", hc.comp_type, i, j);
				else if ((hc.comp_type == 0) && (hc.dec_size != hc.comp_size))
					printf("ASSERT: Size mismatch in a non-compressed entry\n");
				hc.data = (uint8_t *) malloc(hc.comp_size);
				fread(hc.data, 1, hc.comp_size, hqr_file);
				printf("%05i.%05i (%08X): NORMAL: %08X, %08X, %s\n", i, j, hc.offset, hc.dec_size, hc.comp_size,
				       comp_types[hc.comp_type]);

				next_c = hqr_next_child(i, hc);
				if (next_c == UNUSED) {
					printf("ASSERT: Failed to allocate next child for entry %i\n", i);
					return;
				}

				j++;

				printf("%08X, %08X\n", hc.size_next_off, hc.tbl_next_off);
				if (hc.size_next_off == hc.tbl_next_off)
					break;
			}
		}
	}
    }
}


/* Constraints:
	- [I] EOF entries may not be deleted;
	- [I] When deleting normal entries, find the first pointer, if any, and convert it into a normal entry;
	- Any entry with corresponding hidden entries can only be deleted when all hidden entries are gone;
	- Pointers to entries with hidden entries need to have their own hidden entries updated to match those
	  of the entry it points to;
	- [I] Any entry with a parent above ours must have its parent number updated.

	Perhaps having the hidden entries on a child linked list belonging to the parent entry would be better.
	Then, deleting a parent that has any children would simply cause its child to take its place*, and any
	pointer update would be simpler as well.

	* Any child would take over the parent's entry and have its own entry deleted instead. */
#if 0
static void
hqr_entry_delete(int32_t entry)
{
    int32_t i, p;
    int32_t new;
    hqr_entry_t he;

    if (entry == UNUSED)
	return;

    if ((hqr_entries[entry].entry_type & ENTRY_MASK) == ENTRY_EOF)
	return;

    i = first_entry;
    p = 0;
    while (i != UNUSED) {
	if (hqr_entries[i].parent > entry)
		hqr_entries[i].parent--;
	else if ((hqr_entries[i].parent == entry) && (p == 0)) {
		p++;
		new = i;
		he = hqr_entries[i];
		hqr_entries[i] = hqr_entries[entry];
		hqr_entries[i].prev = he.prev;
		hqr_entries[i].next = he.next;
	} else if ((hqr_entries[i].parent == entry) && (p > 0)) {
		p++;
		hqr_entries[i].parent = new;
	}
	i = hqr_entries[i].next;
    }

    if (hqr_entries[entry].prev != UNUSED)
	hqr_entries[hqr_entries[entry].prev].next = hqr_entries[entry].next;

    if (hqr_entries[entry].next != UNUSED)
	hqr_entries[hqr_entries[entry].next].prev = hqr_entries[entry].prev;

    if (p != 0) {
	if (hqr_entries[entry].desc != NULL) {
		free(hqr_entries[entry].desc);
		hqr_entries[entry].desc = NULL;
	}

	if (hqr_entries[entry].data != NULL) {
		free(hqr_entries[entry].data);
		hqr_entries[entry].data = NULL;
	}
    } else {
	hqr_entries[entry].desc = NULL;
	hqr_entries[entry].data = NULL;
    }

    hqr_entries[entry].prev = hqr_entries[entry].next = UNUSED;

    hqr_list_sort();
}
#endif


static void
hqr_file_close(void)
{
    if (hqr_file != NULL) {
	fclose(hqr_file);
	hqr_file = NULL;
    }
}


static void
hqr_file_open_r(char *path)
{
    if (hqr_file != NULL)
	hqr_file_close();

    hqr_file = fopen(path, "rb");
}


#if 0
static void
hqr_file_open_w(char *path)
{
    if (hqr_file != NULL)
	hqr_file_close();

    hqr_file = fopen(path, "wb");
}
#endif


static void
hqr_free(void)
{
    int32_t i;

    for (i = 0; i < hqr_entries_no; i++) {
	if (hqr_entries[i].desc != NULL) {
		free(hqr_entries[i].desc);
		hqr_entries[i].desc = NULL;
	}

	if (hqr_entries[i].data != NULL) {
		free(hqr_entries[i].data);
		hqr_entries[i].data = NULL;
	}

	if (hqr_entries[i].children != NULL) {
		free(hqr_entries[i].children);
		hqr_entries[i].children = NULL;
	}
    }

    free(hqr_entries);
    hqr_entries = NULL;

    hqr_entries_no = 0;

    free(hqr_offsets);
    hqr_offsets = NULL;

    hqr_offsets_no = 0;
}


static void
hqr_close(void)
{
    hqr_free();
    hqr_file_close();
}


int
main(int argc, char *argv[])
{
    int32_t i, j;
    hqr_entry_t he;

    if (argc != 2) {
	printf("ASSERT: Invalid argc: %i\n", argc);
	return 1;
    }

    hqr_init();

    hqr_file_open_r(argv[1]);
    if (hqr_file == NULL) {
	printf("ASSERT: Error opening file: %s\n", argv[1]);
	return 2;
    }

    hqr_load();

    if (hqr_entries_no == 0) {
	printf("ASSERT: File %s has no entries\n", argv[1]);
	hqr_close();
	return 3;
    }

    printf("%s\n", argv[1]);
    for (i = 0; i < hqr_entries_no; i++) {
	if (hqr_entries[i].entry_type == ENTRY_NORMAL) {
		printf("    +---- %05i: %7s (%5s)\n", i, entry_types[hqr_entries[i].entry_type],
		       comp_types[hqr_entries[i].comp_type]);
		he = hqr_entries[i];
	} else if (hqr_entries[i].entry_type == ENTRY_POINTER) {
		printf("    +---- %05i: %7s (%05i)\n", i, entry_types[hqr_entries[i].entry_type], hqr_entries[i].parent);
		he = hqr_entries[hqr_entries[i].parent];
	} else {
		printf("    +---- %05i: %7s\n", i, entry_types[hqr_entries[i].entry_type]);
		he = hqr_entries[i];
	}

	if (he.children_no > 0) {
		for (j = 0; j < he.children_no; j++)
			printf("    |       +---- %05i.%05i: %7s (%5s)\n", i, j, entry_types[he.children[j].entry_type],
			       comp_types[he.children[j].comp_type]);
	}
    }

    hqr_close();

    return 0;
}
