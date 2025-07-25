/* pnginfo.h - internal structures for libpng
 *
 * Copyright (c) 2018-2025 Cosmin Truta
 * Copyright (c) 1998-2002,2004,2006-2013,2018 Glenn Randers-Pehrson
 * Copyright (c) 1996-1997 Andreas Dilger
 * Copyright (c) 1995-1996 Guy Eric Schalnat, Group 42, Inc.
 *
 * This code is released under the libpng license.
 * For conditions of distribution and use, see the disclaimer
 * and license in png.h
 */

#ifndef PNGPRIV_H
#  error This file must not be included by applications; please include <png.h>
#endif

/* INTERNAL, PRIVATE definition of a PNG.
 *
 * png_info is a modifiable description of a PNG datastream.  The fields inside
 * this structure are accessed through png_get_<CHUNK>() functions and modified
 * using png_set_<CHUNK>() functions.
 *
 * Some functions in libpng do directly access members of png_info.  However,
 * this should be avoided.  png_struct objects contain members which hold
 * caches, sometimes optimised, of the values from png_info objects, and
 * png_info is not passed to the functions which read and write image data.
 */
#ifndef PNGINFO_H
#define PNGINFO_H

struct png_info_def
{
   /* The following are necessary for every PNG file */
   png_uint_32 width;       /* width of image in pixels (from IHDR) */
   png_uint_32 height;      /* height of image in pixels (from IHDR) */
   png_uint_32 valid;       /* valid chunk data (see PNG_INFO_ below) */
   size_t rowbytes;         /* bytes needed to hold an untransformed row */
   png_colorp palette;      /* array of color values (valid & PNG_INFO_PLTE) */
   png_uint_16 num_palette; /* number of color entries in "palette" (PLTE) */
   png_uint_16 num_trans;   /* number of transparent palette color (tRNS) */
   png_byte bit_depth;      /* 1, 2, 4, 8, or 16 bits/channel (from IHDR) */
   png_byte color_type;     /* see PNG_COLOR_TYPE_ below (from IHDR) */
   /* The following three should have been named *_method not *_type */
   png_byte compression_type; /* must be PNG_COMPRESSION_TYPE_BASE (IHDR) */
   png_byte filter_type;    /* must be PNG_FILTER_TYPE_BASE (from IHDR) */
   png_byte interlace_type; /* One of PNG_INTERLACE_NONE, PNG_INTERLACE_ADAM7 */

   /* The following are set by png_set_IHDR, called from the application on
    * write, but the are never actually used by the write code.
    */
   png_byte channels;       /* number of data channels per pixel (1, 2, 3, 4) */
   png_byte pixel_depth;    /* number of bits per pixel */
   png_byte spare_byte;     /* to align the data, and for future use */

#ifdef PNG_READ_SUPPORTED
   /* This is never set during write */
   png_byte signature[8];   /* magic bytes read by libpng from start of file */
#endif

   /* The rest of the data is optional.  If you are reading, check the
    * valid field to see if the information in these are valid.  If you
    * are writing, set the valid field to those chunks you want written,
    * and initialize the appropriate fields below.
    */

#ifdef PNG_cICP_SUPPORTED
   /* cICP chunk data */
   png_byte cicp_colour_primaries;
   png_byte cicp_transfer_function;
   png_byte cicp_matrix_coefficients;
   png_byte cicp_video_full_range_flag;
#endif

#ifdef PNG_iCCP_SUPPORTED
   /* iCCP chunk data. */
   png_charp iccp_name;     /* profile name */
   png_bytep iccp_profile;  /* International Color Consortium profile data */
   png_uint_32 iccp_proflen;  /* ICC profile data length */
#endif

#ifdef PNG_cLLI_SUPPORTED
   png_uint_32 maxCLL;  /* cd/m2 (nits) * 10,000 */
   png_uint_32 maxFALL;
#endif

#ifdef PNG_mDCV_SUPPORTED
   png_uint_16 mastering_red_x;  /* CIE (xy) x * 50,000 */
   png_uint_16 mastering_red_y;
   png_uint_16 mastering_green_x;
   png_uint_16 mastering_green_y;
   png_uint_16 mastering_blue_x;
   png_uint_16 mastering_blue_y;
   png_uint_16 mastering_white_x;
   png_uint_16 mastering_white_y;
   png_uint_32 mastering_maxDL; /* cd/m2 (nits) * 10,000 */
   png_uint_32 mastering_minDL;
#endif

#ifdef PNG_TEXT_SUPPORTED
   /* The tEXt, and zTXt chunks contain human-readable textual data in
    * uncompressed, compressed, and optionally compressed forms, respectively.
    * The data in "text" is an array of pointers to uncompressed,
    * null-terminated C strings. Each chunk has a keyword that describes the
    * textual data contained in that chunk.  Keywords are not required to be
    * unique, and the text string may be empty.  Any number of text chunks may
    * be in an image.
    */
   int num_text; /* number of comments read or comments to write */
   int max_text; /* current size of text array */
   png_textp text; /* array of comments read or comments to write */
#endif /* TEXT */

#ifdef PNG_tIME_SUPPORTED
   /* The tIME chunk holds the last time the displayed image data was
    * modified.  See the png_time struct for the contents of this struct.
    */
   png_time mod_time;
#endif

#ifdef PNG_sBIT_SUPPORTED
   /* The sBIT chunk specifies the number of significant high-order bits
    * in the pixel data.  Values are in the range [1, bit_depth], and are
    * only specified for the channels in the pixel data.  The contents of
    * the low-order bits is not specified.  Data is valid if
    * (valid & PNG_INFO_sBIT) is non-zero.
    */
   png_color_8 sig_bit; /* significant bits in color channels */
#endif

#if defined(PNG_tRNS_SUPPORTED) || defined(PNG_READ_EXPAND_SUPPORTED) || \
defined(PNG_READ_BACKGROUND_SUPPORTED)
   /* The tRNS chunk supplies transparency data for paletted images and
    * other image types that don't need a full alpha channel.  There are
    * "num_trans" transparency values for a paletted image, stored in the
    * same order as the palette colors, starting from index 0.  Values
    * for the data are in the range [0, 255], ranging from fully transparent
    * to fully opaque, respectively.  For non-paletted images, there is a
    * single color specified that should be treated as fully transparent.
    * Data is valid if (valid & PNG_INFO_tRNS) is non-zero.
    */
   png_bytep trans_alpha;    /* alpha values for paletted image */
   png_color_16 trans_color; /* transparent color for non-palette image */
#endif

#if defined(PNG_bKGD_SUPPORTED) || defined(PNG_READ_BACKGROUND_SUPPORTED)
   /* The bKGD chunk gives the suggested image background color if the
    * display program does not have its own background color and the image
    * is needs to composited onto a background before display.  The colors
    * in "background" are normally in the same color space/depth as the
    * pixel data.  Data is valid if (valid & PNG_INFO_bKGD) is non-zero.
    */
   png_color_16 background;
#endif

#ifdef PNG_oFFs_SUPPORTED
   /* The oFFs chunk gives the offset in "offset_unit_type" units rightwards
    * and downwards from the top-left corner of the display, page, or other
    * application-specific co-ordinate space.  See the PNG_OFFSET_ defines
    * below for the unit types.  Valid if (valid & PNG_INFO_oFFs) non-zero.
    */
   png_int_32 x_offset; /* x offset on page */
   png_int_32 y_offset; /* y offset on page */
   png_byte offset_unit_type; /* offset units type */
#endif

#ifdef PNG_pHYs_SUPPORTED
   /* The pHYs chunk gives the physical pixel density of the image for
    * display or printing in "phys_unit_type" units (see PNG_RESOLUTION_
    * defines below).  Data is valid if (valid & PNG_INFO_pHYs) is non-zero.
    */
   png_uint_32 x_pixels_per_unit; /* horizontal pixel density */
   png_uint_32 y_pixels_per_unit; /* vertical pixel density */
   png_byte phys_unit_type; /* resolution type (see PNG_RESOLUTION_ below) */
#endif

#ifdef PNG_eXIf_SUPPORTED
   png_uint_32 num_exif;  /* Added at libpng-1.6.31 */
   png_bytep exif;
#endif

#ifdef PNG_hIST_SUPPORTED
   /* The hIST chunk contains the relative frequency or importance of the
    * various palette entries, so that a viewer can intelligently select a
    * reduced-color palette, if required.  Data is an array of "num_palette"
    * values in the range [0,65535]. Data valid if (valid & PNG_INFO_hIST)
    * is non-zero.
    */
   png_uint_16p hist;
#endif

#ifdef PNG_pCAL_SUPPORTED
   /* The pCAL chunk describes a transformation between the stored pixel
    * values and original physical data values used to create the image.
    * The integer range [0, 2^bit_depth - 1] maps to the floating-point
    * range given by [pcal_X0, pcal_X1], and are further transformed by a
    * (possibly non-linear) transformation function given by "pcal_type"
    * and "pcal_params" into "pcal_units".  Please see the PNG_EQUATION_
    * defines below, and the PNG-Group's PNG extensions document for a
    * complete description of the transformations and how they should be
    * implemented, and for a description of the ASCII parameter strings.
    * Data values are valid if (valid & PNG_INFO_pCAL) non-zero.
    */
   png_charp pcal_purpose;  /* pCAL chunk description string */
   png_int_32 pcal_X0;      /* minimum value */
   png_int_32 pcal_X1;      /* maximum value */
   png_charp pcal_units;    /* Latin-1 string giving physical units */
   png_charpp pcal_params;  /* ASCII strings containing parameter values */
   png_byte pcal_type;      /* equation type (see PNG_EQUATION_ below) */
   png_byte pcal_nparams;   /* number of parameters given in pcal_params */
#endif

/* New members added in libpng-1.0.6 */
   png_uint_32 free_me;     /* flags items libpng is responsible for freeing */

#ifdef PNG_STORE_UNKNOWN_CHUNKS_SUPPORTED
   /* Storage for unknown chunks that the library doesn't recognize. */
   png_unknown_chunkp unknown_chunks;

   /* The type of this field is limited by the type of
    * png_struct::user_chunk_cache_max, else overflow can occur.
    */
   int                unknown_chunks_num;
#endif

#ifdef PNG_sPLT_SUPPORTED
   /* Data on sPLT chunks (there may be more than one). */
   png_sPLT_tp splt_palettes;
   int         splt_palettes_num; /* Match type returned by png_get API */
#endif

#ifdef PNG_sCAL_SUPPORTED
   /* The sCAL chunk describes the actual physical dimensions of the
    * subject matter of the graphic.  The chunk contains a unit specification
    * a byte value, and two ASCII strings representing floating-point
    * values.  The values are width and height corresponding to one pixel
    * in the image.  Data values are valid if (valid & PNG_INFO_sCAL) is
    * non-zero.
    */
   png_byte scal_unit;         /* unit of physical scale */
   png_charp scal_s_width;     /* string containing height */
   png_charp scal_s_height;    /* string containing width */
#endif

#ifdef PNG_INFO_IMAGE_SUPPORTED
   /* Memory has been allocated if (valid & PNG_ALLOCATED_INFO_ROWS)
      non-zero */
   /* Data valid if (valid & PNG_INFO_IDAT) non-zero */
   png_bytepp row_pointers;        /* the image bits */
#endif

#ifdef PNG_cHRM_SUPPORTED
   png_xy cHRM;
#endif

#ifdef PNG_gAMA_SUPPORTED
   png_fixed_point gamma;
#endif

#ifdef PNG_sRGB_SUPPORTED
   int rendering_intent;
#endif
#ifdef PNG_APNG_SUPPORTED
   png_uint_32 num_frames; /* including default image */
   png_uint_32 num_plays;
   png_uint_32 next_frame_width;
   png_uint_32 next_frame_height;
   png_uint_32 next_frame_x_offset;
   png_uint_32 next_frame_y_offset;
   png_uint_16 next_frame_delay_num;
   png_uint_16 next_frame_delay_den;
   png_byte next_frame_dispose_op;
   png_byte next_frame_blend_op;
#endif

};
#endif /* PNGINFO_H */
