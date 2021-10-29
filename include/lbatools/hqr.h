#ifndef LBATOOLS_HQR_H
# define LBATOOLS_HQR_H


typedef struct
{
    int32_t		dec_size, comp_size;
    int16_t		comp_type, entry_type;
    int32_t		offset;
    int32_t		tbl_next_off, size_next_off;
    uint8_t		*desc, *data;

    uintptr_t		pad0;
    int32_t		pad1, parent;
} hqr_common_t;


typedef struct
{
    int32_t		dec_size, comp_size;
    int16_t		comp_type, entry_type;
    int32_t		offset;
    int32_t		tbl_next_off, size_next_off;
    uint8_t		*desc, *data;

    hqr_common_t 	*children;
    int32_t		children_no;

    int32_t		parent;				/* Parent for pointer. */
} hqr_entry_t;


typedef struct
{
    int32_t		offset, entry;
} hqr_offset_t;


typedef struct
{
    hqr_entry_t		*entries;
    hqr_offset_t	*offsets;
    int32_t		entries_no, offsets_no;
    FILE *		file;
} hqr_t;


extern hqr_t *	hqr_init(void);
extern void	hqr_free(hqr_t *hqr);
extern void	hqr_close(hqr_t *hqr);
extern int32_t	hqr_load(hqr_t *hqr, char *path);
extern int32_t	hqr_entry_delete(hqr_t *, int32_t entry, int32_t delete_children);
extern hqr_common_t	hqr_entry_new(int32_t entry_type, int32_t parent, int32_t dec_size, int16_t comp_type, char *buf);
extern int32_t	hqr_entry_insert(hqr_t *hqr, int32_t entry, int32_t child, hqr_common_t hc, int32_t add_as_child, char *buf);


#endif	/*LBATOOLS_HQR_H*/
