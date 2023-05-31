/* SPDX-FileCopyrightText: 2001-2002 NaN Holding BV. All rights reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup DNA
 * \brief blenloader genfile private function prototypes
 */

#pragma once

#include "intern/dna_utils.h"

struct SDNA;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * DNAstr contains the prebuilt SDNA structure defining the layouts of the types
 * used by this version of Blender. It is defined in a file dna.c, which is
 * generated by the makesdna program during the build process (see makesdna.c).
 */
extern const unsigned char DNAstr[];
/** Length of DNAstr. */
extern const int DNAlen;

/**
 * Primitive (non-struct, non-pointer/function/array) types,
 * \warning Don't change these values!
 * Currently changes here will work on native endianness,
 * however #DNA_struct_switch_endian currently checks these
 * hard-coded values against those from old files.
 */
typedef enum eSDNA_Type {
  SDNA_TYPE_CHAR = 0,
  SDNA_TYPE_UCHAR = 1,
  SDNA_TYPE_SHORT = 2,
  SDNA_TYPE_USHORT = 3,
  SDNA_TYPE_INT = 4,
  /* SDNA_TYPE_LONG     = 5, */ /* deprecated (use as int) */
  /* SDNA_TYPE_ULONG    = 6, */ /* deprecated (use as int) */
  SDNA_TYPE_FLOAT = 7,
  SDNA_TYPE_DOUBLE = 8,
/* ,SDNA_TYPE_VOID = 9 */
/* define so switch statements don't complain */
#define SDNA_TYPE_VOID 9
  SDNA_TYPE_INT64 = 10,
  SDNA_TYPE_UINT64 = 11,
  SDNA_TYPE_INT8 = 12,
} eSDNA_Type;

/**
 * For use with #DNA_struct_reconstruct & #DNA_struct_get_compareflags
 */
enum eSDNA_StructCompare {
  /* Struct has disappeared
   * (values of this struct type will not be loaded by the current Blender) */
  SDNA_CMP_REMOVED = 0,
  /* Struct is the same
   * (can be loaded with straight memory copy after any necessary endian conversion) */
  SDNA_CMP_EQUAL = 1,
  /* Struct is different in some way
   * (needs to be copied/converted field by field) */
  SDNA_CMP_NOT_EQUAL = 2,
  /* This is only used temporarily by #DNA_struct_get_compareflags. */
  SDNA_CMP_UNKNOWN = 3,
};

/**
 * Constructs and returns a decoded SDNA structure from the given encoded SDNA data block.
 */
struct SDNA *DNA_sdna_from_data(const void *data,
                                int data_len,
                                bool do_endian_swap,
                                bool data_alloc,
                                const char **r_error_message);
void DNA_sdna_free(struct SDNA *sdna);

/* Access for current Blender versions SDNA. */
void DNA_sdna_current_init(void);
/* borrowed reference */
const struct SDNA *DNA_sdna_current_get(void);
void DNA_sdna_current_free(void);

struct DNA_ReconstructInfo;
/**
 * Pre-process information about how structs in \a newsdna can be reconstructed from structs in
 * \a oldsdna. This information is then used to speedup #DNA_struct_reconstruct.
 */
struct DNA_ReconstructInfo *DNA_reconstruct_info_create(const struct SDNA *oldsdna,
                                                        const struct SDNA *newsdna,
                                                        const char *compare_flags);
void DNA_reconstruct_info_free(struct DNA_ReconstructInfo *reconstruct_info);

/**
 * Returns the index of the struct info for the struct with the specified name.
 */
int DNA_struct_find_nr_ex(const struct SDNA *sdna, const char *str, unsigned int *index_last);
int DNA_struct_find_nr(const struct SDNA *sdna, const char *str);
/**
 * Does endian swapping on the fields of a struct value.
 *
 * \param sdna: SDNA of the struct_nr belongs to
 * \param struct_nr: Index of struct info within sdna
 * \param data: Struct data that is to be converted
 */
void DNA_struct_switch_endian(const struct SDNA *sdna, int struct_nr, char *data);
/**
 * Constructs and returns an array of byte flags with one element for each struct in oldsdna,
 * indicating how it compares to newsdna.
 */
const char *DNA_struct_get_compareflags(const struct SDNA *sdna, const struct SDNA *newsdna);
/**
 * \param reconstruct_info: Information preprocessed by #DNA_reconstruct_info_create.
 * \param old_struct_nr: Index of struct info within oldsdna.
 * \param blocks: The number of array elements.
 * \param old_blocks: Array of struct data.
 * \return An allocated reconstructed struct.
 */
void *DNA_struct_reconstruct(const struct DNA_ReconstructInfo *reconstruct_info,
                             int old_struct_nr,
                             int blocks,
                             const void *old_blocks);

/**
 * Returns the offset of the field with the specified name and type within the specified
 * struct type in #SDNA, -1 on failure.
 */
int DNA_elem_offset(struct SDNA *sdna, const char *stype, const char *vartype, const char *name);

/**
 * Returns the size of struct fields of the specified type and name.
 *
 * \param type: Index into sdna->types/types_size
 * \param name: Index into sdna->names,
 * needed to extract possible pointer/array information.
 */
int DNA_elem_size_nr(const struct SDNA *sdna, short type, short name);

bool DNA_struct_find(const struct SDNA *sdna, const char *stype);
bool DNA_struct_elem_find(const struct SDNA *sdna,
                          const char *stype,
                          const char *vartype,
                          const char *name);

/**
 * Returns the size in bytes of a primitive type.
 */
int DNA_elem_type_size(eSDNA_Type elem_nr);

/**
 * Rename a struct
 */
bool DNA_sdna_patch_struct(struct SDNA *sdna,
                           const char *struct_name_old,
                           const char *struct_name_new);
/**
 * Replace \a elem_old with \a elem_new for struct \a struct_name
 * handles search & replace, maintaining surrounding non-identifier characters
 * such as pointer & array size.
 */
bool DNA_sdna_patch_struct_member(struct SDNA *sdna,
                                  const char *struct_name,
                                  const char *elem_old,
                                  const char *elem_new);

void DNA_sdna_alias_data_ensure(struct SDNA *sdna);

/* Alias lookups (using runtime struct member names). */

/**
 * \note requires #DNA_sdna_alias_data_ensure_structs_map to be called.
 */
int DNA_struct_alias_find_nr_ex(const struct SDNA *sdna,
                                const char *str,
                                unsigned int *index_last);
/**
 * \note requires #DNA_sdna_alias_data_ensure_structs_map to be called.
 */
int DNA_struct_alias_find_nr(const struct SDNA *sdna, const char *str);
/**
 * \note requires #DNA_sdna_alias_data_ensure_structs_map to be called.
 */
bool DNA_struct_alias_elem_find(const struct SDNA *sdna,
                                const char *stype,
                                const char *vartype,
                                const char *name);
/**
 * Separated from #DNA_sdna_alias_data_ensure because it's not needed
 * unless we want to lookup aliased struct names (#DNA_struct_alias_find_nr and friends).
 */
void DNA_sdna_alias_data_ensure_structs_map(struct SDNA *sdna);

#ifdef __cplusplus
}
#endif
