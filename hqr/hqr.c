#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lbatools/compress.h>
#include <lbatools/hqr.h>


#define ENTRY_CHILD	-2	/* Child entry. */
#define ENTRY_UNUSED	-1	/* Unused entry. */
#define ENTRY_NULL	0	/* NULL entry, to be save as an offset of 0x00000000. */
#define ENTRY_NORMAL	1	/* Normal entry. */
#define ENTRY_POINTER	2	/* Pointer to another entry (use .parent). */
#define ENTRY_EOF	3	/* End of file (entry points to the end of file). */

#define COMPRESS_STORE	0
#define COMPRESS_LZSS	1
#define COMPRESS_LZMIT	2

#define UNUSED		-1

#define NUM_ENTRIES	65536
#define NUM_CHILDREN	65536
#define NUM_OFFSETS	65536


static hqr_entry_t	*hqr_entries;
static hqr_offset_t	*hqr_offsets = NULL;
static int32_t		hqr_entries_no, hqr_offsets_no;
// static int32_t		next_entry, next_offset;
static FILE *		hqr_file = NULL;
static char *		comp_types[3] = { "None", "LZSS", "LZMIT" };
static char *		entry_types[8] = { "Null", "Normal", "Pointer", "EOF" };


static void
hqr_child_init(hqr_common_t *hc)
{
    hc->entry_type = ENTRY_NULL;
    hc->comp_type = COMPRESS_STORE;
    hc->dec_size = hc->comp_size = 0;
    hc->desc = hc->data = NULL;
    hc->tbl_next_off = hc->size_next_off = 0x00000000;
    hc->offset = 0x00000000;
}


static void
hqr_child_add(hqr_t *hqr, int32_t i, hqr_common_t hc)
{
    if (hqr->entries_no >= NUM_CHILDREN) {
	printf("ASSERT: Children exhausted for entry %i\n", i);
	return;
    }

    if (hqr->entries[i].children_no == 0)
	hqr->entries[i].children = (hqr_common_t *) malloc((hqr->entries[i].children_no + 1) * sizeof(hqr_common_t));
    else
	hqr->entries[i].children = (hqr_common_t *) realloc(hqr->entries[i].children,
							    (hqr->entries[i].children_no + 1) * sizeof(hqr_common_t));

    hqr->entries[i].children[hqr->entries[i].children_no] = hc;
    hqr->entries[i].children_no++;
}


static int32_t
hqr_next_child(hqr_t *hqr, int32_t i, hqr_common_t hc)
{
    hqr_child_add(hqr, i, hc);

    return (hqr->entries[i].children_no - 1);
}


static void
hqr_entry_init(hqr_entry_t *he)
{
    he->entry_type = ENTRY_NULL;
    he->comp_type = COMPRESS_STORE;
    he->dec_size = he->comp_size = 0;
    he->parent = UNUSED;
    he->desc = he->data = NULL;
    he->tbl_next_off = he->size_next_off = 0x00000000;
    he->offset = 0x00000000;

    he->children = NULL;
    he->children_no = 0;
}


static void
hqr_entry_add(hqr_t *hqr, hqr_entry_t he)
{
    if (hqr->entries_no >= NUM_ENTRIES) {
	printf("ASSERT: Entries exhausted\n");
	return;
    }

    if (hqr->entries_no == 0)
	hqr->entries = (hqr_entry_t *) malloc((hqr->entries_no + 1) * sizeof(hqr_entry_t));
    else
	hqr->entries = (hqr_entry_t *) realloc(hqr->entries, (hqr->entries_no + 1) * sizeof(hqr_entry_t));

    hqr->entries[hqr->entries_no] = he;
    hqr->entries_no++;
}


static int32_t
hqr_next_entry(hqr_t *hqr, hqr_entry_t he)
{
    hqr_entry_add(hqr, he);

    return (hqr->entries_no - 1);
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


void
hqr_offsets_clear(void)
{
    if (hqr_offsets != NULL) {
	free(hqr_offsets);
	hqr_offsets = NULL;
    }

    hqr_offsets_no = 0;

}


hqr_t *
hqr_init(void)
{
    hqr_t *hqr = (hqr_t *) malloc(sizeof(hqr_t));

    memset(hqr, 0x00, sizeof(hqr_t));

    return hqr;
}


#if 0
static void
hqr_replace_parent(hqr_t *hqr, int32_t old, int32_t new)
{
    int32_t i;

    for (i = 0; i < hqr->entries_no; i++) {
	if (hqr->entries[i].parent == old)
		hqr->entries[i].parent = new;
	else if (hqr->entries[i].parent == new)
		hqr->entries[i].parent = old;
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
hqr_find_parent(hqr_t *hqr, int32_t offset)
{
    int32_t i, ret = UNUSED;

    for (i = 0; i < hqr->entries_no; i++) {
	if (hqr->entries[i].offset == offset) {
		ret = hqr->entries[i].offset;
		break;
	}
    }

    return ret;
}
#endif


static void
hqr_file_close(hqr_t *hqr)
{
    if (hqr == NULL)
	return 0;

    if (hqr->file != NULL) {
	fclose(hqr->file);
	hqr->file = NULL;
    }
}


static int32_t
hqr_load_internal(hqr_t *hqr, char *path)
{
    int32_t i, j;
    int32_t file_len, offset;
    int32_t next_e, next_c;
    int32_t prev_o, next_o;
    hqr_entry_t he;
    hqr_offset_t ho;
    hqr_common_t hc;

    if (hqr->file != NULL)
	hqr_file_close(hqr);

    hqr->file = fopen(path, "rb");
    if (hqr->file == NULL)
	return 0;

    fseek(hqr->file, 0, SEEK_END);
    file_len = ftell(hqr->file);
    if (file_len < 4)
	return 0;

    /* Pass 1: Load the main entries. */
    i = 0;
    while (1) {
	fseek(hqr->file, i << 2, SEEK_SET);
	fread(&offset, 1, 4, hqr->file);
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
				return 0;
			}

			hqr_entry_init(&he);
			fseek(hqr->file, offset, SEEK_SET);
			if (offset == file_len) {
				/* EOF entry. */
				he.entry_type = ENTRY_EOF;
				printf("%05i (%08X): EOF\n", i, offset);
				break;
			} else {
				/* Normal entry. */
				he.entry_type = ENTRY_NORMAL;
				he.offset = offset;
				fread(&he.dec_size, 1, 4, hqr->file);
				fread(&he.comp_size, 1, 4, hqr->file);
				fread(&he.comp_type, 1, 2, hqr->file);
				if ((he.comp_type < 0) || (he.comp_type > 2))
					printf("ASSERT: Invalid compression type %i for entry %i\n", hqr_entries[next_e].comp_type, next_e);
				else if ((he.comp_type == 0) && (he.dec_size != he.comp_size))
					printf("ASSERT: Size mismatch in a non-compressed entry\n");
				he.data = (uint8_t *) malloc(he.comp_size);
				fread(he.data, 1, he.comp_size, hqr->file);
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

	next_e = hqr_next_entry(hqr, he);
	if (next_e == UNUSED) {
		printf("ASSERT: Failed to allocate next entry\n");
		return 0;
	}

	i++;

	if ((i << 2) == hqr_offsets[0].offset) {
		printf("ASSERT: No EOF entry in HQR file\n");
		break;
	}
    }

    /* Pass 2: Load the children. */
    for (i = 0; i < hqr->entries_no; i++) {
	if (hqr->entries[i].entry_type == ENTRY_NORMAL) {
		hqr->entries[i].tbl_next_off = hqr_find_next_offset(hqr->entries[i].offset);
		if (hqr->entries[i].tbl_next_off == UNUSED) {
			printf("ASSERT: hqr->entries[i].tbl_next_off = UNUSED\n");
			return;
		}
		hqr->entries[i].size_next_off = hqr->entries[i].offset + hqr->entries[i].comp_size + 10;

		if (hqr->entries[i].tbl_next_off != hqr->entries[i].size_next_off) {
			j = 0;
			he = hqr->entries[i];
			hqr_child_init(&hc);
			while (1) {
				if (j == 0)		/* Use parent size_next_off. */
					hc.offset = he.size_next_off;
				else			/* Use previous child size_next_off. */
					hc.offset = hc.size_next_off;
				fseek(hqr_file, hc.offset, SEEK_SET);
				hc.tbl_next_off = he.tbl_next_off;
				fread(&hc.dec_size, 1, 4, hqr->file);
				fread(&hc.comp_size, 1, 4, hqr->file);
				hc.size_next_off = hc.offset + hc.comp_size + 10;
				fread(&hc.comp_type, 1, 2, hqr->file);
				if ((hc.comp_type < 0) || (hc.comp_type > 2))
					printf("ASSERT: Invalid compression type %i for entry %i.%i\n", hc.comp_type, i, j);
				else if ((hc.comp_type == 0) && (hc.dec_size != hc.comp_size))
					printf("ASSERT: Size mismatch in a non-compressed entry\n");
				hc.data = (uint8_t *) malloc(hc.comp_size);
				fread(hc.data, 1, hc.comp_size, hqr->file);
				printf("%05i.%05i (%08X): NORMAL: %08X, %08X, %s\n", i, j, hc.offset, hc.dec_size, hc.comp_size,
				       comp_types[hc.comp_type]);

				next_c = hqr_next_child(hqr, i, hc);
				if (next_c == UNUSED) {
					printf("ASSERT: Failed to allocate next child for entry %i\n", i);
					return 0;
				}

				j++;

				printf("%08X, %08X\n", hc.size_next_off, hc.tbl_next_off);
				if (hc.size_next_off == hc.tbl_next_off)
					break;
			}
		}
	}
    }

    return 1;
}


/* Wrapper to make sure we always clean up hqr_offets after loading a file. */
int32_t
hqr_load(hqr_t *hqr, char *path)
{
    int32_t ret;

    if (hqr == NULL)
	return 0;

    ret = hqr_load_internal(hqr, path);

    if (hqr_offsets != NULL) {
	free(hqr_offsets);
	hqr_offsets = NULL;
    }

    hqr_file_close(hqr);

    return ret;
}


int32_t
hqr_entry_delete(hqr_t *hqr int32_t entry, int32_t delete_children)
{
    int32_t i, new, delete, list_entry_del = 0, first_ptr = UNUSED;
    hqr_entry_t he;

    if (hqr == NULL)
	return 0;

    if (entry == UNUSED)
	return 0;

    if (hqr->entries[entry].entry_type == ENTRY_EOF)
	return 0;

    delete = (hqr->entries[entry].children_no == 0) || delete_children;

    /* Pass 1: Find all entries that point to us. */
    if (hqr->entries[entry].entry_type == ENTRY_NORMAL) {
	/* There are no pointers to pointers or pointers to NULL entries. */
	for (i = 0; i < hqr->entries_no; i++) {
		if (hqr->entries[i].parent == entry) {
			first_ptr = i;
			break;
   		}
	}
    }

    if (first_ptr != UNUSED) {
	/* If anything points to us, then do not delete the data field or children, but pass them to the
	   pointer. */
	for (i = first_ptr + 1; i < hqr->entries_no; i++) {
		if (hqr->entries[i].parent == entry)
			hqr->entries[i].parent = first_ptr;
	}

	hqr->entries[first_ptr].type = ENTRY_NORMAL;
	hqr->entries[first_ptr].dec_size = hqr->entries[entry].dec_size;
	hqr->entries[first_ptr].comp_size = hqr->entries[entry].comp_size;
	hqr->entries[first_ptr].data = hqr->entries[entry].data;
	hqr->entries[first_ptr].children_no = hqr->entries[entry].children_no;
	hqr->entries[first_ptr].children = hqr->entries[entry].children;

	/* Clean up our own entry. */
	hqr->entries[entry].data = NULL;
	hqr->entries[entry].children_no = NULL;
	hqr->entries[entry].chilren = NULL;
	/* Force no deletion of children if we're passing ourselves to our first pointer. */
	delete = 0;
    }

    /* Finish removing ourselves, with the data field first. */
    if (hqr->entries[entry].data != NULL) {
	free(hqr->entries[entry].data);
	hqr->entries[entry].data = NULL;
    }

    /* Next, the description field. */
    if (hqr->entries[entry].desc != NULL) {
	free(hqr->entries[entry].desc);
	hqr->entries[entry].desc = NULL;
    }

    if ((hqr->entries[entry].children_no == 0) || delete) {
	/* Update all parents - do this here so that if we pass ourselves to our
	   first child, we do not update parents as our entry is still going to
	   exist. */
	for (i = 0; i < hqr->entries_no; i++) {
		if (hqr->entries[i].parent > entry)
			hqr->entries[i].parent--;
	}
	list_entry_del = 1;
    }

    if (hqr->entries[entry].children_no != 0) {
	/* And now the children. */
	if (delete) {
		/* Delete all of them. */
		for (i = 0; i < hqr->entries[entry].children_no; i++) {
			/* Data field. */
			if (hqr->entries[entry].children[i].data != NULL) {
				free(hqr->entries[entry].children[i].data);
				hqr->entries[entry].children[i].data = NULL;
			}

			/* Next, the description field. */
			if (hqr->entries[entry].children[i].desc != NULL) {
				free(hqr->entries[entry].children[i].desc);
				hqr->entries[entry].children[i].desc = NULL;
			}
		}

		hqr->entries[entry].children_no = 0;
		free(hqr->entries[entry].children);
		hqr->entries[entry].children = NULL;
	} else {
		/* Pass ourselves to our first child. */
		hqr->entries[entry].dec_size = hqr->entries[entry].children[0].dec_size;
		hqr->entries[entry].comp_size = hqr->entries[entry].children[0].comp_size;
		hqr->entries[entry].comp_type = hqr->entries[entry].children[0].comp_type;
		hqr->entries[entry].desc = hqr->entries[entry].children[0].desc;
		hqr->entries[entry].data = hqr->entries[entry].children[0].data;

		/* Remove the first child from the list. */
		for (i = 1; i < hqr->entries[entry].children_no; i++)
			hqr->entries[entry].children[i - 1] = hqr->entries[entry].children[i];
		hqr->entries[entry].children_no--;
		memset(hqr->entries[entry].children[hqr->entries[entry].children_no], 0x00, sizeof(hqr_common_t));
		if (hqr->entries[entry].children_no == 0) {
			free(hqr->entries[entry].children);
			hqr->entries[entry].children = NULL;
		} else
			hqr->entries[entry].children = (hqr_common_t *) realloc(hqr->entries[entry].children,
									hqr->entries[entry].children_no * sizeof(hqr_common_t));
	}
    }

    if (list_entry_del) {
	/* Remove ourselves from the list. */
	for (i = entry + 1; i < hqr_entries_no; i++)
		hqr->entries[i - 1] = hqr->entries[i];
	hqr->entries_no--;
	memset(hqr->entries[hqr->entries_no], 0x00, sizeof(hqr_entry_t));
	if (hqr->entries_no == 0) {
		free(hqr->entries);
		hqr->entries = NULL;
	} else
		hqr->entries = (hqr_entry_t *) realloc(hqr->entries, hqr->entries_no * sizeof(hqr_entry_t));
    }

    return 1;
}


hqr_common_t
hqr_entry_new(int32_t entry_type, int32_t parent, int32_t dec_size, int16_t comp_type, char *buf)
{
    hqr_common_t hc;

    memset(&hc, 0x00, sizeof(hqr_common_t));

    hc.entry_type = entry_type;
    hc.parent = parent;
    hc.dec_size = dec_size;

    /* Force Store if someone tries an invalid type. */
    if ((comp_type < COMPRESS_STORE) || (comp_type < COMPRESS_LZMIT))
	comp_type = 0;
    hc.comp_type = comp_type;

    hc.data = (char *) malloc(val << 1);
    hc.comp_size = compress(comp_type, hc.data, buf, val);

    return hc;
}


/* Add a HQR entry:
	- After a child:
		- Before a child - always add as a child;
		- Before a normal entry - add as a normal entry or child depending on the parameter;
		- Before anything else - always add as a normal entry;
	- After a normal entry:
		- Before a child - add as a normal entry or child depending on the parameter;
		- Before another normal entry - add as a normal entry or child depending on the parameter;
		- Before anything else - always add as a normal entry;
	- After anything else:
		- Before a child - this is an invalid situation that can not exist;
		- Before another normal entry - always add as a normal entry;
		- Before anything else - always add as a normal entry. */
int32_t
hqr_entry_insert(hqr_t *hqr, int32_t entry, int32_t child, hqr_common_t hc, int32_t add_as_child, char *buf)
{
    int32_t prev_entry_type = ENTRY_UNUSED, next_entry_type = ENTRY_UNUSED;
    int32_t i;

    /* Uninitialized High Quality Resource, do nothing. */
    if (hqr_entries == NULL)
	return 0;

    /* Attempting to insert an invalid or EOF type, do nothing. */
    if ((hc.entry_type < ENTRY_NULL) && (hc.entry_type >= ENTRY_EOF))
	return 0;

    /* Return without doing anything if the entry number to insert at is invalid. */
    if ((entry < 0) || (entry > hqr->entries_no))
	return 0;

    /* Return without doing anything if we're trying to add a child to a non-existent entry. */
    if ((child != 0) && (entry == hqr->entries_no))
	return 0;

    /* Return without doing anything if the child number to insert at is invalid. */
    if ((child < 0) || ((entry < hqr->entries_no) && (child > hqr->entries[entry].children_no)))
	return 0;

    if ((child == 0) && (entry > 0))
	prev_entry_type = hqr_entries[entry - 1].entry_type;
    else if (child > 0)
	prev_entry_type = ENTRY_CHILD;

    if ((child == hqr->entries[entry].children_no) && (entry < (hqr->entries_no - 1)))
	next_entry_type = hqr->entries[entry].entry_type;
    else if (child < hqr->entries[entry].children_no)
	next_entry_type = ENTRY_CHILD;

    /* Uninitialized High Quality Resource, do nothing. */
    if ((prev_entry_type == ENTRY_UNUSED) && (next_entry_type == ENTRY_UNUSED))
	return 0;

    /* Attempting to insert an entry after the EOF entry, do nothing. */
    if (prev_entry_type == ENTRY_EOF)
	return 0;

    /* Impossible combination, get out. */
    if (prev_entry_type != ENTRY_NORMAL) && (next_entry_type == ENTRY_CHILD))
	return 0;

    /* If the next entry is not normal, always insert it as a normal entry. */
    if ((next_entry_type != ENTRY_NORMAL) && (next_entry_type != ENTRY_EOF))
	add_as_child = 0;

    /* If we're after an entry that's not normal, and before an entry that's
       either normal or EOF, always insert it as a normal entry. */
    if ((prev_entry_type != ENTRY_NORMAL) && ((next_entry_type == ENTRY_NORMAL) || (next_entry_type == ENTRY_EOF)))
	add_as_child = 0;

    /* If we're between children, always insert it as a child. */
    if ((prev_entry_type == ENTRY_CHILD) && (next_entry_type == ENTRY_CHILD))
	add_as_child = 1;

    if (add_as_child) {
	/* Attempting to insert a pointer or null entry as a child, do nothing. */
	if (hc.entry_type != ENTRY_NORMAL)
		return 0;
    }

    if (add_as_child) {
	if (hqr->entries[entry].children_no == 0)
		hqr->entries[entry].children = (hqr_common_t *) malloc((hqr->entries[entry].children_no + 1) *
								sizeof(hqr_common_t));
	else
		hqr->entries[entry].children = (hqr_common_t *) realloc(hqr->entries[entry].children,
								(hqr->entries[entry].children_no + 1) * sizeof(hqr_common_t));

	if (child < hqr->entries[entry].children_no) {
		for (i = hqr->entries[entry].children_no; i > child; i--)
			hqr->entries[entry].children[i] = hqr->entries[entry].children[i - 1];
	}

	hqr->entries[entry].children[child] = hc;
	hqr->entries[entry].children_no++;
    } else {
	if (hqr->entries_no == 0)
		hqr->entries = (hqr_entry_t *) malloc((hqr->entries_no + 1) * sizeof(hqr_entry_t));
	else
		hqr->entries = (hqr_entry_t *) realloc(hqr->entries, (hqr->entries_no + 1) * sizeof(hqr_entry_t));

	/* Update parents. */
	for (i = 0; i < hqr->entries_no; i++) {
		 if (hqr->entries[i].parent >= entry)
			hqr->entries[i].parent++;
	}

	if (entry < hqr->entries_no) {
		for (i = hqr->entries_no; i > entry; i--)
			hqr->entries[i] = hqr->entries[i - 1];
	}

	hqr->entries[entry] = (hqr_entry_t *) hc;
	hqr_->ntries_no++;
    }

    return 1;
}


#if 0
static void
hqr_file_open_w(hqr_t *hqr, char *path)
{
    if (hqr->file != NULL)
	hqr->file_close();

    hqr->file = fopen(path, "wb");
}
#endif


void
hqr_free(hqr_t *hqr)
{
    int32_t i;

    for (i = 0; i < hqr->entries_no; i++) {
	if (hqr->entries[i].desc != NULL) {
		free(hqr->entries[i].desc);
		hqr->entries[i].desc = NULL;
	}

	if (hqr->entries[i].data != NULL) {
		free(hqr->entries[i].data);
		hqr->entries[i].data = NULL;
	}

	if (hqr->entries[i].children != NULL) {
		free(hqr->entries[i].children);
		hqr->entries[i].children = NULL;
	}
    }

    free(hqr->entries);
    hqr->entries = NULL;

    hqr->entries_no = 0;
}


void
hqr_close(hqr_t *hqr)
{
    hqr_free(hqr);
    hqr_file_close(hqr);

    free(hqr);
}
