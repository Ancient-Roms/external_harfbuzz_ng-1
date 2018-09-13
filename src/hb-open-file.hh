/*
 * Copyright © 2007,2008,2009  Red Hat, Inc.
 * Copyright © 2012  Google, Inc.
 *
 *  This is part of HarfBuzz, a text shaping library.
 *
 * Permission is hereby granted, without written agreement and without
 * license or royalty fees, to use, copy, modify, and distribute this
 * software and its documentation for any purpose, provided that the
 * above copyright notice and the following two paragraphs appear in
 * all copies of this software.
 *
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES
 * ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN
 * IF THE COPYRIGHT HOLDER HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 * THE COPYRIGHT HOLDER SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND THE COPYRIGHT HOLDER HAS NO OBLIGATION TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 *
 * Red Hat Author(s): Behdad Esfahbod
 * Google Author(s): Behdad Esfahbod
 */

#ifndef HB_OPEN_FILE_HH
#define HB_OPEN_FILE_HH

#include "hb-open-type.hh"
#include "hb-ot-head-table.hh"


namespace OT {


/*
 *
 * The OpenType Font File
 *
 */


/*
 * Organization of an OpenType Font
 */

struct OpenTypeFontFile;
struct OffsetTable;
struct TTCHeader;


typedef struct TableRecord
{
  int cmp (Tag t) const
  { return -t.cmp (tag); }

  static int cmp (const void *pa, const void *pb)
  {
    const TableRecord *a = (const TableRecord *) pa;
    const TableRecord *b = (const TableRecord *) pb;
    return b->cmp (a->tag);
  }

  inline bool sanitize (hb_sanitize_context_t *c) const
  {
    TRACE_SANITIZE (this);
    return_trace (c->check_struct (this));
  }

  Tag		tag;		/* 4-byte identifier. */
  CheckSum	checkSum;	/* CheckSum for this table. */
  Offset32	offset;		/* Offset from beginning of TrueType font
				 * file. */
  HBUINT32	length;		/* Length of this table. */
  public:
  DEFINE_SIZE_STATIC (16);
} OpenTypeTable;

typedef struct OffsetTable
{
  friend struct OpenTypeFontFile;

  inline unsigned int get_table_count (void) const
  { return tables.len; }
  inline const TableRecord& get_table (unsigned int i) const
  {
    return tables[i];
  }
  inline unsigned int get_table_tags (unsigned int  start_offset,
				      unsigned int *table_count, /* IN/OUT */
				      hb_tag_t     *table_tags /* OUT */) const
  {
    if (table_count)
    {
      if (start_offset >= tables.len)
        *table_count = 0;
      else
        *table_count = MIN<unsigned int> (*table_count, tables.len - start_offset);

      const TableRecord *sub_tables = tables.arrayZ + start_offset;
      unsigned int count = *table_count;
      for (unsigned int i = 0; i < count; i++)
	table_tags[i] = sub_tables[i].tag;
    }
    return tables.len;
  }
  inline bool find_table_index (hb_tag_t tag, unsigned int *table_index) const
  {
    Tag t;
    t.set (tag);
    /* Linear-search for small tables to work around fonts with unsorted
     * table list. */
    int i = tables.len < 64 ? tables.lsearch (t) : tables.bsearch (t);
    if (table_index)
      *table_index = i == -1 ? (unsigned) Index::NOT_FOUND_INDEX : (unsigned) i;
    return i != -1;
  }
  inline const TableRecord& get_table_by_tag (hb_tag_t tag) const
  {
    unsigned int table_index;
    find_table_index (tag, &table_index);
    return get_table (table_index);
  }

  public:

  inline bool serialize (hb_serialize_context_t *c,
			 hb_tag_t sfnt_tag,
			 Supplier<hb_tag_t> &tags,
			 Supplier<hb_blob_t *> &blobs,
			 unsigned int table_count)
  {
    TRACE_SERIALIZE (this);
    /* Alloc 12 for the OTHeader. */
    if (unlikely (!c->extend_min (*this))) return_trace (false);
    /* Write sfntVersion (bytes 0..3). */
    sfnt_version.set (sfnt_tag);
    /* Take space for numTables, searchRange, entrySelector, RangeShift
     * and the TableRecords themselves.  */
    if (unlikely (!tables.serialize (c, table_count))) return_trace (false);

    const char *dir_end = (const char *) c->head;
    HBUINT32 *checksum_adjustment = nullptr;

    /* Write OffsetTables, alloc for and write actual table blobs. */
    for (unsigned int i = 0; i < table_count; i++)
    {
      TableRecord &rec = tables.arrayZ[i];
      hb_blob_t *blob = blobs[i];
      rec.tag.set (tags[i]);
      rec.length.set (hb_blob_get_length (blob));
      rec.offset.serialize (c, this);

      /* Allocate room for the table and copy it. */
      char *start = (char *) c->allocate_size<void> (rec.length);
      if (unlikely (!start)) {return false;}

      memcpy (start, hb_blob_get_data (blob, nullptr), rec.length);

      /* 4-byte allignment. */
      c->align (4);
      const char *end = (const char *) c->head;

      if (tags[i] == HB_OT_TAG_head && end - start >= head::static_size)
      {
	head *h = (head *) start;
	checksum_adjustment = &h->checkSumAdjustment;
	checksum_adjustment->set (0);
      }

      rec.checkSum.set_for_data (start, end - start);
    }
    tags += table_count;
    blobs += table_count;

    tables.qsort ();

    if (checksum_adjustment)
    {
      CheckSum checksum;

      /* The following line is a slower version of the following block. */
      //checksum.set_for_data (this, (const char *) c->head - (const char *) this);
      checksum.set_for_data (this, dir_end - (const char *) this);
      for (unsigned int i = 0; i < table_count; i++)
      {
	TableRecord &rec = tables.arrayZ[i];
	checksum.set (checksum + rec.checkSum);
      }

      checksum_adjustment->set (0xB1B0AFBAu - checksum);
    }

    return_trace (true);
  }

  inline bool sanitize (hb_sanitize_context_t *c) const
  {
    TRACE_SANITIZE (this);
    return_trace (c->check_struct (this) && tables.sanitize (c));
  }

  protected:
  Tag		sfnt_version;	/* '\0\001\0\00' if TrueType / 'OTTO' if CFF */
  BinSearchArrayOf<TableRecord>
		tables;
  public:
  DEFINE_SIZE_ARRAY (12, tables);
} OpenTypeFontFace;


/*
 * TrueType Collections
 */

struct TTCHeaderVersion1
{
  friend struct TTCHeader;

  inline unsigned int get_face_count (void) const { return table.len; }
  inline const OpenTypeFontFace& get_face (unsigned int i) const { return this+table[i]; }

  inline bool sanitize (hb_sanitize_context_t *c) const
  {
    TRACE_SANITIZE (this);
    return_trace (table.sanitize (c, this));
  }

  protected:
  Tag		ttcTag;		/* TrueType Collection ID string: 'ttcf' */
  FixedVersion<>version;	/* Version of the TTC Header (1.0),
				 * 0x00010000u */
  LArrayOf<LOffsetTo<OffsetTable> >
		table;		/* Array of offsets to the OffsetTable for each font
				 * from the beginning of the file */
  public:
  DEFINE_SIZE_ARRAY (12, table);
};

struct TTCHeader
{
  friend struct OpenTypeFontFile;

  private:

  inline unsigned int get_face_count (void) const
  {
    switch (u.header.version.major) {
    case 2: /* version 2 is compatible with version 1 */
    case 1: return u.version1.get_face_count ();
    default:return 0;
    }
  }
  inline const OpenTypeFontFace& get_face (unsigned int i) const
  {
    switch (u.header.version.major) {
    case 2: /* version 2 is compatible with version 1 */
    case 1: return u.version1.get_face (i);
    default:return Null(OpenTypeFontFace);
    }
  }

  inline bool sanitize (hb_sanitize_context_t *c) const
  {
    TRACE_SANITIZE (this);
    if (unlikely (!u.header.version.sanitize (c))) return_trace (false);
    switch (u.header.version.major) {
    case 2: /* version 2 is compatible with version 1 */
    case 1: return_trace (u.version1.sanitize (c));
    default:return_trace (true);
    }
  }

  protected:
  union {
  struct {
  Tag		ttcTag;		/* TrueType Collection ID string: 'ttcf' */
  FixedVersion<>version;	/* Version of the TTC Header (1.0 or 2.0),
				 * 0x00010000u or 0x00020000u */
  }			header;
  TTCHeaderVersion1	version1;
  } u;
};

/*
 * Mac Resource Fork
 *
 * http://mirror.informatimago.com/next/developer.apple.com/documentation/mac/MoreToolbox/MoreToolbox-99.html
 */

struct ResourceRecord
{
  inline const hb_bytes_t get_data (const void *data_base) const
  { return hb_bytes_t (data_base+offset); }

  inline bool sanitize (hb_sanitize_context_t *c,
			const void *data_base) const
  {
    TRACE_SANITIZE (this);
    return_trace (c->check_struct (this) &&
		  offset.sanitize (c, data_base));
  }

  protected:
  HBUINT16	id;		/* Resource ID, is really should be signed? */
  HBINT16	nameOffset;	/* Offset from beginning of resource name list
				 * to resource name, -1 means there is none. */
  HBUINT8	attrs;		/* Resource attributes */
  OffsetTo<LArrayOf<HBUINT8>, HBUINT24>
		offset;		/* Offset from beginning of data block to
				 * data for this resource */
  HBUINT32	reserved;	/* Reserved for handle to resource */
  public:
  DEFINE_SIZE_STATIC (12);
};

struct ResourceTypeRecord
{
  inline unsigned int get_resource_count () const { return resCountM1 + 1; }

  inline bool is_sfnt () const { return tag == HB_TAG ('s','f','n','t'); }

  inline const ResourceRecord& get_resource_record (unsigned int i,
						    const void *type_base) const
  {
    return hb_array_t<ResourceRecord> ((type_base+resourcesZ).arrayZ,
				       get_resource_count ()) [i];
  }

  inline bool sanitize (hb_sanitize_context_t *c,
			const void *type_base,
			const void *data_base) const
  {
    TRACE_SANITIZE (this);
    return_trace (c->check_struct (this) &&
		  resourcesZ.sanitize (c, type_base,
				       get_resource_count (),
				       data_base));
  }

  protected:
  Tag		tag;		/* Resource type. */
  HBUINT16	resCountM1;	/* Number of resources minus 1. */
  OffsetTo<UnsizedArrayOf<ResourceRecord> >
		resourcesZ;	/* Offset from beginning of resource type list
				 * to reference item list for this type. */
  public:
  DEFINE_SIZE_STATIC (8);
};

struct ResourceMap
{
  inline const ResourceTypeRecord& get_type_record (unsigned int i) const
  {
    // Why offset from the third byte of the object? I'm not sure
    return hb_array_t<ResourceTypeRecord> (((2 + (const char *) this )+typeListZ).arrayZ,
					   get_type_count ()) [i];
  }

  inline unsigned int get_type_count () const { return typeCountM1 + 1; }

  inline const ResourceRecord &get_resource_record (const ResourceTypeRecord &type,
						    unsigned int i) const
  {
    return type.get_resource_record (i, &(this+typeListZ));
  }

  inline bool sanitize (hb_sanitize_context_t *c, const void *data_base) const
  {
    TRACE_SANITIZE (this);
    const void *type_base = &(this+typeListZ);
    return_trace (c->check_struct (this) &&
		  typeListZ.sanitize (c, 2 + (const char *) this,
				      get_type_count (),
				      type_base,
				      data_base));
  }

  protected:
  HBUINT8	reserved0[16];	/* Reserved for copy of resource header */
  HBUINT32	reserved1;	/* Reserved for handle to next resource map */
  HBUINT16	resreved2;	/* Reserved for file reference number */
  HBUINT16	attrs;		/* Resource fork attribute */
  OffsetTo<UnsizedArrayOf<ResourceTypeRecord> >
		typeListZ;	/* Offset from beginning of map to
				 * resource type list */
  Offset16	nameList;	/* Offset from beginning of map to
				 * resource name list */
  HBUINT16	typeCountM1;	/* Number of types in the map minus 1 */
  public:
  DEFINE_SIZE_STATIC (30);
};

struct ResourceForkHeader
{
  inline unsigned int get_face_count () const
  {
    const ResourceMap &resource_map = this+map;
    unsigned int count = resource_map.get_type_count ();
    for (unsigned int i = 0; i < count; ++i)
    {
      const ResourceTypeRecord& type = resource_map.get_type_record (i);
      if (type.is_sfnt ())
	return type.get_resource_count ();
    }
    return 0;
  }

  inline const hb_bytes_t get_data (const ResourceTypeRecord& type,
				    unsigned int idx) const
  {
    const ResourceMap &resource_map = this+map;
    const void *data_base = &(this+data);
    return resource_map.get_resource_record (type, idx).get_data (data_base);
  }

  inline const OpenTypeFontFace& get_face (unsigned int idx,
					   unsigned int *base_offset = nullptr) const
  {
    const ResourceMap &resource_map = this+map;
    for (unsigned int i = 0; i < resource_map.get_type_count (); ++i)
    {
      const ResourceTypeRecord& type = resource_map.get_type_record (i);
      if (type.is_sfnt () && idx < type.get_resource_count ())
      {
	const OpenTypeFontFace &face = *get_data (type, idx).as<OpenTypeFontFace> ();
	if (base_offset)
	  *base_offset = (const char *) &face - (const char *) this;
	return face;
      }
    }
    return Null (OpenTypeFontFace);
  }

  inline bool sanitize (hb_sanitize_context_t *c) const
  {
    TRACE_SANITIZE (this);
    return_trace (c->check_struct (this) &&
		  map.sanitize (c, this, &(this+data)));

    // XXX Sanitize OpenTypeFontFace's
  }

  protected:
  LOffsetTo<UnsizedArrayOf<HBUINT8> >
		data;		/* Offset from beginning of resource fork
				 * to resource data */
  LOffsetTo<ResourceMap>
		map;		/* Offset from beginning of resource fork
				 * to resource map */
  HBUINT32	dataLen;	/* Length of resource data */
  HBUINT32	mapLen;		/* Length of resource map */
  public:
  DEFINE_SIZE_STATIC (16);
};

/*
 * OpenType Font File
 */

struct OpenTypeFontFile
{
  enum {
    CFFTag		= HB_TAG ('O','T','T','O'), /* OpenType with Postscript outlines */
    TrueTypeTag		= HB_TAG ( 0 , 1 , 0 , 0 ), /* OpenType with TrueType outlines */
    TTCTag		= HB_TAG ('t','t','c','f'), /* TrueType Collection */
    DFontTag		= HB_TAG ( 0 , 0 , 1 , 0 ), /* DFont Mac Resource Fork */
    TrueTag		= HB_TAG ('t','r','u','e'), /* Obsolete Apple TrueType */
    Typ1Tag		= HB_TAG ('t','y','p','1')  /* Obsolete Apple Type1 font in SFNT container */
  };

  inline hb_tag_t get_tag (void) const { return u.tag; }

  inline unsigned int get_face_count (void) const
  {
    switch (u.tag) {
    case CFFTag:	/* All the non-collection tags */
    case TrueTag:
    case Typ1Tag:
    case TrueTypeTag:	return 1;
    case TTCTag:	return u.ttcHeader.get_face_count ();
    case DFontTag:	return u.rfHeader.get_face_count ();
    default:		return 0;
    }
  }
  inline const OpenTypeFontFace& get_face (unsigned int i, unsigned int *base_offset = nullptr) const
  {
    if (base_offset)
      *base_offset = 0;
    switch (u.tag) {
    /* Note: for non-collection SFNT data we ignore index.  This is because
     * Apple dfont container is a container of SFNT's.  So each SFNT is a
     * non-TTC, but the index is more than zero. */
    case CFFTag:	/* All the non-collection tags */
    case TrueTag:
    case Typ1Tag:
    case TrueTypeTag:	return u.fontFace;
    case TTCTag:	return u.ttcHeader.get_face (i);
    case DFontTag:	return u.rfHeader.get_face (i, base_offset);
    default:		return Null(OpenTypeFontFace);
    }
  }

  inline bool serialize_single (hb_serialize_context_t *c,
				hb_tag_t sfnt_tag,
			        Supplier<hb_tag_t> &tags,
			        Supplier<hb_blob_t *> &blobs,
			        unsigned int table_count)
  {
    TRACE_SERIALIZE (this);
    assert (sfnt_tag != TTCTag);
    if (unlikely (!c->extend_min (*this))) return_trace (false);
    return_trace (u.fontFace.serialize (c, sfnt_tag, tags, blobs, table_count));
  }

  inline bool sanitize (hb_sanitize_context_t *c) const
  {
    TRACE_SANITIZE (this);
    if (unlikely (!u.tag.sanitize (c))) return_trace (false);
    switch (u.tag) {
    case CFFTag:	/* All the non-collection tags */
    case TrueTag:
    case Typ1Tag:
    case TrueTypeTag:	return_trace (u.fontFace.sanitize (c));
    case TTCTag:	return_trace (u.ttcHeader.sanitize (c));
    case DFontTag:	return_trace (u.rfHeader.sanitize (c));
    default:		return_trace (true);
    }
  }

  protected:
  union {
  Tag			tag;		/* 4-byte identifier. */
  OpenTypeFontFace	fontFace;
  TTCHeader		ttcHeader;
  ResourceForkHeader	rfHeader;
  } u;
  public:
  DEFINE_SIZE_UNION (4, tag);
};


} /* namespace OT */


#endif /* HB_OPEN_FILE_HH */
